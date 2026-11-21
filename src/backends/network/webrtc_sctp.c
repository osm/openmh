#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_sctp_internal.h"
#include "engine/core/limits.h"

enum mh_error_code
mh_sctp_open(const struct mh_sctp_config *config, struct mh_sctp **output,
	struct mh_error *error_out)
{
	struct mh_sctp *peer;

	if (config == NULL || output == NULL || config->local_port == 0 ||
		config->local_port > MH_U16_MAX || config->remote_port == 0 ||
		config->remote_port > MH_U16_MAX || config->tag == 0 ||
		config->tag > SCTP_MASK || config->initial_tsn > SCTP_MASK)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SCTP configuration");

	peer = calloc(1, sizeof(*peer));

	if (peer == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate SCTP association");

	peer->config = *config;
	peer->next_tsn = config->initial_tsn;
	peer->send_ack = (config->initial_tsn - 1) & SCTP_MASK;
	peer->cwnd = SCTP_INITIAL_CHUNKS * SCTP_CHUNK_BYTES;
	peer->threshold = SCTP_RECEIVE_BYTES;
	peer->rto = SCTP_RTO_INITIAL_MS;
	*output = peer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_sctp_close(struct mh_sctp *peer)
{
	free(peer);
}

int
mh_sctp_connected(const struct mh_sctp *peer)
{
	return peer != NULL && peer->state == SCTP_ESTABLISHED;
}

int
mh_sctp_closed(const struct mh_sctp *peer)
{
	return peer != NULL && peer->state == SCTP_CLOSED;
}

static int
init_input(struct mh_sctp *peer, const struct mh_sctp_chunk *chunk)
{
	size_t offset, length, padded;
	unsigned int type;
	unsigned long tag, tsn;
	int forward;

	if (chunk->flags != 0 || chunk->size < SCTP_INIT_HEADER_BYTES ||
		mh_rtc_read32(chunk->data) == 0 ||
		mh_rtc_read32(chunk->data + SCTP_INIT_WINDOW) <
			SCTP_MIN_REMOTE_WINDOW ||
		mh_rtc_read16(chunk->data + SCTP_INIT_OUT_STREAMS) == 0 ||
		mh_rtc_read16(chunk->data + SCTP_INIT_IN_STREAMS) == 0)
		return 0;

	forward = 0;

	for (offset = SCTP_INIT_HEADER_BYTES; offset < chunk->size;
		offset += padded) {
		if (chunk->size - offset < SCTP_PARAMETER_HEADER_BYTES)
			return 0;

		type = mh_rtc_read16(chunk->data + offset);
		length = mh_rtc_read16(
			chunk->data + offset + SCTP_PARAMETER_LENGTH);

		if (length < SCTP_PARAMETER_HEADER_BYTES ||
			length > chunk->size - offset)
			return 0;

		padded = (length + 3) & ~(size_t)3;

		if (padded > chunk->size - offset) {
			if (length != chunk->size - offset)
				return 0;

			padded = length;
		}

		if (type == SCTP_FORWARD_SUPPORTED) {
			if (length != SCTP_PARAMETER_HEADER_BYTES)
				return 0;

			forward = 1;
		} else if (type == SCTP_IPV4_ADDRESS ||
			type == SCTP_IPV6_ADDRESS ||
			type == SCTP_COOKIE_PRESERVATIVE ||
			type == SCTP_SUPPORTED_ADDRESSES) {
			/*
			 * SCTP-over-DTLS has one path; these do not select
			 * addresses.
			 */
		} else if (!(type & SCTP_UNKNOWN_PARAMETER_SKIP))
			return 0;
	}

	if (!forward)
		return 0;

	tag = mh_rtc_read32(chunk->data);
	tsn = mh_rtc_read32(chunk->data + SCTP_INIT_TSN);

	if (peer->state != SCTP_LISTEN) {
		/* A new association requires a fresh DTLS peer and cookie. */
		if (tag != peer->remote_tag || tsn != peer->remote_initial)
			return 1;
	} else {
		peer->remote_tag = tag;
		peer->remote_initial = tsn;
		peer->receive_tsn = (tsn - 1) & SCTP_MASK;
		peer->remote_window =
			mh_rtc_read32(chunk->data + SCTP_INIT_WINDOW);
		peer->state = SCTP_COOKIE;
	}

	peer->init_ack = 1;

	return 1;
}

static size_t
init_ack_write(const struct mh_sctp *peer, unsigned char *data)
{
	size_t offset;

	mh_rtc_write32(data, peer->config.tag);
	mh_rtc_write32(data + SCTP_INIT_WINDOW, SCTP_RECEIVE_BYTES);
	mh_rtc_write16(data + SCTP_INIT_OUT_STREAMS, 1);
	mh_rtc_write16(data + SCTP_INIT_IN_STREAMS, 1);
	mh_rtc_write32(data + SCTP_INIT_TSN, peer->config.initial_tsn);
	offset = SCTP_INIT_HEADER_BYTES;
	mh_rtc_write16(data + offset, SCTP_STATE_COOKIE);
	mh_rtc_write16(data + offset + SCTP_PARAMETER_LENGTH,
		SCTP_PARAMETER_HEADER_BYTES + MH_SCTP_COOKIE_BYTES);
	memcpy(data + offset + SCTP_PARAMETER_HEADER_BYTES, peer->config.cookie,
		MH_SCTP_COOKIE_BYTES);
	offset += SCTP_PARAMETER_HEADER_BYTES + MH_SCTP_COOKIE_BYTES;
	mh_rtc_write16(data + offset, SCTP_FORWARD_SUPPORTED);
	mh_rtc_write16(data + offset + SCTP_PARAMETER_LENGTH,
		SCTP_PARAMETER_HEADER_BYTES);

	return offset + SCTP_PARAMETER_HEADER_BYTES;
}

static int
cookie_input(struct mh_sctp *peer, const struct mh_sctp_chunk *chunk,
	unsigned long now)
{
	unsigned int i, difference;

	if (chunk->flags != 0 || chunk->size != MH_SCTP_COOKIE_BYTES)
		return 0;

	difference = 0;

	for (i = 0; i < MH_SCTP_COOKIE_BYTES; i++)
		difference |= chunk->data[i] ^ peer->config.cookie[i];

	if (difference != 0)
		return 1;

	if (peer->state == SCTP_COOKIE) {
		peer->state = SCTP_ESTABLISHED;
		peer->last_send = now;
	}

	if (peer->state == SCTP_ESTABLISHED)
		peer->cookie_ack = 1;

	return 1;
}

static int
control_input(struct mh_sctp *peer, const struct mh_sctp_chunk *chunk,
	unsigned long now)
{
	unsigned char data[SCTP_SACK_HEADER_BYTES];
	struct mh_sctp_chunk sack;

	if (chunk->type == SCTP_COOKIE_ECHO)
		return cookie_input(peer, chunk, now);

	if (chunk->type == SCTP_ABORT) {
		if (chunk->flags > 1)
			return 0;

		peer->state = SCTP_CLOSED;

		return 1;
	}

	if (peer->state != SCTP_ESTABLISHED && peer->state != SCTP_SHUTDOWN)
		return 1;

	switch (chunk->type) {
	case SCTP_DATA:
		return peer->state == SCTP_ESTABLISHED
			? mh_sctp_data_input(peer, chunk)
			: 1;

	case SCTP_SACK:
		return mh_sctp_sack_input(peer, chunk, now);
	case SCTP_HEARTBEAT:
		if (chunk->flags != 0 ||
			chunk->size < SCTP_PARAMETER_HEADER_BYTES ||
			chunk->size > sizeof(peer->heartbeat) ||
			mh_rtc_read16(chunk->data) != SCTP_HEARTBEAT_INFO ||
			mh_rtc_read16(chunk->data + SCTP_PARAMETER_LENGTH) !=
				chunk->size)
			return 0;

		memcpy(peer->heartbeat, chunk->data, chunk->size);
		peer->heartbeat_size = chunk->size;
		return 1;

	case SCTP_SHUTDOWN_REQUEST:
		if (chunk->flags != 0 || chunk->size != SCTP_TSN_BYTES)
			return 0;

		memcpy(data, chunk->data, SCTP_TSN_BYTES);
		mh_rtc_write32(data + SCTP_TSN_BYTES, peer->remote_window);
		memset(data + SCTP_SACK_GAPS, 0,
			SCTP_SACK_HEADER_BYTES - SCTP_SACK_GAPS);
		sack.type = SCTP_SACK;
		sack.flags = 0;
		sack.data = data;
		sack.size = sizeof(data);

		if (!mh_sctp_sack_input(peer, &sack, now))
			return 0;

		mh_sctp_send_shutdown(peer);
		peer->state = SCTP_SHUTDOWN;
		peer->shutdown_ack = 1;
		return 1;

	case SCTP_SHUTDOWN_COMPLETE:
		if (chunk->size != 0 || chunk->flags > 1)
			return 0;

		if (peer->state == SCTP_SHUTDOWN)
			peer->state = SCTP_CLOSED;

		return 1;

	case SCTP_FORWARD_TSN:
		return mh_sctp_forward_input(peer, chunk);
	case SCTP_HEARTBEAT_ACK:
	case SCTP_ERROR:
	case SCTP_PADDING:
		return 1;
	default:
		return 1;
	}
}

enum mh_error_code
mh_sctp_input(struct mh_sctp *peer, const unsigned char *data, size_t size,
	unsigned long now, struct mh_error *error_out)
{
	struct mh_sctp_packet packet;
	struct mh_sctp_chunk chunk;
	size_t offset;
	int known;

	if (peer == NULL || now > SCTP_MASK)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid SCTP input");

	if (peer->state == SCTP_CLOSED)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"SCTP association is closed");

	if (mh_sctp_packet_read(data, size, &packet, error_out) !=
		MH_ERROR_NONE)
		return MH_ERROR_INVALID;

	if (packet.source != peer->config.remote_port ||
		packet.destination != peer->config.local_port)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	offset = MH_SCTP_HEADER_BYTES;
	mh_sctp_chunk_next(&packet, &offset, &chunk);

	if (chunk.type == SCTP_INIT) {
		if (packet.tag != 0 || offset != size)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (!init_input(peer, &chunk))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unsupported SCTP initialization");

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (packet.tag !=
			((chunk.type == SCTP_ABORT && (chunk.flags & 1))
					? peer->remote_tag
					: peer->config.tag) ||
		peer->state == SCTP_LISTEN)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	offset = MH_SCTP_HEADER_BYTES;

	while (mh_sctp_chunk_next(&packet, &offset, &chunk) == 1) {
		known = chunk.type == SCTP_DATA || chunk.type == SCTP_SACK ||
			chunk.type == SCTP_HEARTBEAT ||
			chunk.type == SCTP_HEARTBEAT_ACK ||
			chunk.type == SCTP_ABORT ||
			chunk.type == SCTP_SHUTDOWN_REQUEST ||
			chunk.type == SCTP_ERROR ||
			chunk.type == SCTP_COOKIE_ECHO ||
			chunk.type == SCTP_SHUTDOWN_COMPLETE ||
			chunk.type == SCTP_PADDING ||
			chunk.type == SCTP_FORWARD_TSN;

		if (!known && (chunk.type & SCTP_UNKNOWN_CHUNK_REPORT) &&
			chunk.size <= sizeof(peer->report) -
					SCTP_PARAMETER_HEADER_BYTES -
					MH_SCTP_CHUNK_HEADER_BYTES) {
			/*
			 * Unrecognized Chunk Type cause includes the complete
			 * chunk.
			 */
			mh_rtc_write16(peer->report, SCTP_CAUSE_UNKNOWN_CHUNK);
			mh_rtc_write16(peer->report + SCTP_PARAMETER_LENGTH,
				(unsigned int)chunk.size +
					SCTP_PARAMETER_HEADER_BYTES +
					MH_SCTP_CHUNK_HEADER_BYTES);
			peer->report[SCTP_PARAMETER_HEADER_BYTES] =
				(unsigned char)chunk.type;
			peer->report[SCTP_PARAMETER_HEADER_BYTES +
				MH_SCTP_CHUNK_FLAGS] =
				(unsigned char)chunk.flags;
			mh_rtc_write16(peer->report +
					SCTP_PARAMETER_HEADER_BYTES +
					MH_SCTP_CHUNK_LENGTH,
				(unsigned int)chunk.size +
					MH_SCTP_CHUNK_HEADER_BYTES);
			memcpy(peer->report + SCTP_PARAMETER_HEADER_BYTES +
					MH_SCTP_CHUNK_HEADER_BYTES,
				chunk.data, chunk.size);
			peer->report_size = chunk.size +
				SCTP_PARAMETER_HEADER_BYTES +
				MH_SCTP_CHUNK_HEADER_BYTES;
		}

		if (!known && !(chunk.type & SCTP_UNKNOWN_CHUNK_SKIP))
			break;

		if (!control_input(peer, &chunk, now)) {
			peer->state = SCTP_CLOSED;
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid SCTP chunk for negotiated channel");
		}

		if (peer->state == SCTP_CLOSED)
			break;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sctp_poll(struct mh_sctp *peer, unsigned long now,
	struct mh_sctp_datagram *output, struct mh_error *error_out)
{
	struct mh_sctp_datagram packet;
	unsigned char data[SCTP_CONTROL_BYTES];
	size_t size;
	unsigned int type, flags;

	if (peer == NULL || output == NULL || now > SCTP_MASK)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid SCTP poll");

	if (peer->state == SCTP_CLOSED)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"SCTP association is closed");

	if (peer->state == SCTP_ESTABLISHED || peer->state == SCTP_SHUTDOWN)
		mh_sctp_send_tick(peer, now);

	size = 0;
	flags = 0;

	if (peer->init_ack) {
		type = SCTP_INIT_ACK;
		size = init_ack_write(peer, data);
		peer->init_ack = 0;
	} else if (peer->cookie_ack) {
		type = SCTP_COOKIE_ACK;
		peer->cookie_ack = 0;
	} else if (peer->shutdown_ack && peer->flight_count == 0) {
		type = SCTP_SHUTDOWN_ACK;
		peer->shutdown_ack = 0;
	} else if (peer->heartbeat_size != 0) {
		type = SCTP_HEARTBEAT_ACK;
		size = peer->heartbeat_size;
		memcpy(data, peer->heartbeat, size);
		peer->heartbeat_size = 0;
	} else if (peer->report_size != 0) {
		type = SCTP_ERROR;
		size = peer->report_size;
		memcpy(data, peer->report, size);
		peer->report_size = 0;
	} else if (peer->sack) {
		type = SCTP_SACK;
		size = mh_sctp_sack_write(peer, data);
		peer->sack = 0;
	} else if (peer->forward_due) {
		type = SCTP_FORWARD_TSN;
		size = SCTP_TSN_BYTES;
		mh_rtc_write32(data, peer->forward_tsn);
		peer->forward_due = 0;
		peer->forward_sent = now;
	} else if (peer->state == SCTP_ESTABLISHED &&
		mh_sctp_data_write(peer, now, data, &size, &flags))
		type = SCTP_DATA;
	else {
		output->size = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mh_sctp_packet_begin(peer->config.local_port,
		peer->config.remote_port, peer->remote_tag, packet.data,
		sizeof(packet.data), &packet.size,
		error_out) != MH_ERROR_NONE ||
		mh_sctp_chunk_append(packet.data, sizeof(packet.data),
			&packet.size, type, flags, data, size,
			error_out) != MH_ERROR_NONE ||
		mh_sctp_packet_finish(packet.data, packet.size, error_out) !=
			MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"SCTP packet construction failed");

	output->size = packet.size;
	memcpy(output->data, packet.data, packet.size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
