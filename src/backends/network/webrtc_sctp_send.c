#include <string.h>

#include "backends/network/webrtc_sctp_internal.h"

static struct sctp_flight *
flight_at(struct mh_sctp *peer, unsigned int i)
{
	return &peer->flight[(peer->flight_head + i) % SCTP_WINDOW];
}

static unsigned long
outstanding(struct mh_sctp *peer)
{
	unsigned int i;
	unsigned long bytes;
	struct sctp_flight *flight;

	bytes = 0;

	for (i = 0; i < peer->flight_count; i++) {
		flight = flight_at(peer, i);

		if (!flight->acked && !flight->abandoned)
			bytes += (unsigned long)flight->size +
				MH_SCTP_CHUNK_HEADER_BYTES +
				SCTP_DATA_HEADER_BYTES;
	}

	return bytes;
}

static void
abandon(struct mh_sctp *peer, unsigned long message)
{
	unsigned int i;
	struct sctp_queued *queued;
	struct sctp_flight *flight;
	size_t length;

	for (i = 0; i < peer->flight_count; i++)
		if (flight_at(peer, i)->message == message)
			flight_at(peer, i)->abandoned = 1;

	/* The current message may still have unsent fragments. */
	if (peer->queue_count != 0) {
		queued = &peer->queued[peer->queue_head];

		if (queued->offset != 0 && queued->first_tsn == message) {
			/*
			 * Reserve TSNs for unsent fragments so FORWARD TSN also
			 * invalidates a received prefix whose final fragment
			 * was never transmitted. Starting a message reserves
			 * enough flight slots for all of its fragments.
			 */
			while (queued->offset < queued->message.size) {
				length = queued->message.size - queued->offset;

				if (length > SCTP_PAYLOAD)
					length = SCTP_PAYLOAD;

				flight = flight_at(peer, peer->flight_count++);
				memset(flight, 0, sizeof(*flight));
				flight->tsn = peer->next_tsn;
				flight->message = message;
				flight->abandoned = 1;
				peer->next_tsn =
					(peer->next_tsn + 1) & SCTP_MASK;
				queued->offset += length;
			}

			peer->queue_head = (peer->queue_head + 1) % SCTP_QUEUE;
			peer->queue_count--;
		}
	}
}

static void
advance_forward(struct mh_sctp *peer)
{
	unsigned long tsn;
	unsigned int i;
	struct sctp_flight *flight;
	int abandoned;

	tsn = peer->send_ack;
	abandoned = 0;

	for (i = 0; i < peer->flight_count; i++) {
		flight = flight_at(peer, i);

		if (!flight->acked && !flight->abandoned)
			break;

		tsn = flight->tsn;
		abandoned |= flight->abandoned;
	}

	if (abandoned && (!peer->forward_pending || tsn != peer->forward_tsn)) {
		peer->forward_tsn = tsn;
		peer->forward_pending = peer->forward_due = 1;
	}
}

static void
rtt_sample(struct mh_sctp *peer, unsigned long sample)
{
	unsigned long difference;

	if (sample == 0)
		sample = 1;

	if (sample > SCTP_RTO_MAX_MS)
		return;

	if (peer->srtt == 0) {
		peer->srtt = sample;
		peer->variance = sample / 2;
	} else {
		difference = peer->srtt > sample ? peer->srtt - sample
						: sample - peer->srtt;
		peer->variance = (3 * peer->variance + difference) / 4;
		peer->srtt = (7 * peer->srtt + sample) / 8;
	}

	peer->rto = peer->srtt + 4 * peer->variance;

	if (peer->rto < SCTP_RTO_MIN_MS)
		peer->rto = SCTP_RTO_MIN_MS;

	if (peer->rto > SCTP_RTO_MAX_MS)
		peer->rto = SCTP_RTO_MAX_MS;
}

static int
gap_acked(const struct mh_sctp_chunk *chunk, unsigned long distance)
{
	unsigned int i, gaps;

	gaps = mh_rtc_read16(chunk->data + SCTP_SACK_GAPS);

	for (i = 0; i < gaps; i++)
		if (distance >= mh_rtc_read16(chunk->data +
					SCTP_SACK_HEADER_BYTES +
					i * SCTP_GAP_BYTES) &&
			distance <= mh_rtc_read16(chunk->data +
					SCTP_SACK_HEADER_BYTES +
					SCTP_GAP_END + i * SCTP_GAP_BYTES))
			return 1;

	return 0;
}

int
mh_sctp_sack_input(struct mh_sctp *peer, const struct mh_sctp_chunk *chunk,
	unsigned long now)
{
	struct sctp_flight *flight;
	unsigned long cumulative, highest, bytes, before, distance, old_ack;
	unsigned int gaps, duplicates, i, first, last, previous;
	int acked, sampled, lost, timer_restart, first_outstanding;

	if (chunk->flags != 0 || chunk->size < SCTP_SACK_HEADER_BYTES)
		return 0;

	cumulative = mh_rtc_read32(chunk->data);
	gaps = mh_rtc_read16(chunk->data + SCTP_SACK_GAPS);
	duplicates = mh_rtc_read16(chunk->data + SCTP_SACK_DUPLICATES);

	if ((size_t)gaps + duplicates !=
			(chunk->size - SCTP_SACK_HEADER_BYTES) /
				SCTP_GAP_BYTES ||
		(chunk->size - SCTP_SACK_HEADER_BYTES) % SCTP_GAP_BYTES != 0)
		return 0;

	if (mh_sctp_after(peer->send_ack, cumulative))
		return 1;

	if (mh_sctp_distance(cumulative, peer->send_ack) >
		mh_sctp_distance(
			(peer->next_tsn - 1) & SCTP_MASK, peer->send_ack))
		return 0;

	previous = 0;

	for (i = 0; i < gaps; i++) {
		first = mh_rtc_read16(chunk->data + SCTP_SACK_HEADER_BYTES +
			i * SCTP_GAP_BYTES);
		last = mh_rtc_read16(chunk->data + SCTP_SACK_HEADER_BYTES +
			SCTP_GAP_END + i * SCTP_GAP_BYTES);

		if (first == 0 || first <= previous || last < first ||
			last >= mh_sctp_distance(peer->next_tsn, cumulative))
			return 0;

		previous = last;
	}

	before = outstanding(peer);
	old_ack = peer->send_ack;
	highest = cumulative;
	bytes = 0;
	sampled = lost = timer_restart = 0;
	first_outstanding = 1;

	for (i = 0; i < peer->flight_count; i++) {
		flight = flight_at(peer, i);
		distance = mh_sctp_distance(flight->tsn, cumulative);
		acked = !mh_sctp_after(flight->tsn, cumulative) ||
			gap_acked(chunk, distance);

		if (acked && !flight->acked && !flight->abandoned) {
			bytes += (unsigned long)flight->size +
				MH_SCTP_CHUNK_HEADER_BYTES +
				SCTP_DATA_HEADER_BYTES;

			if (mh_sctp_after(flight->tsn, highest))
				highest = flight->tsn;

			if (!sampled) {
				rtt_sample(peer,
					mh_sctp_distance(now, flight->sent));
				sampled = 1;
			}

			if (first_outstanding)
				timer_restart = 1;
		}

		if (!flight->acked && !flight->abandoned)
			first_outstanding = 0;

		/* Reneged gap acknowledgements become outstanding again. */
		flight->acked = acked;
	}

	for (i = 0; i < peer->flight_count; i++) {
		flight = flight_at(peer, i);

		if (!flight->acked && !flight->abandoned &&
			mh_sctp_after(highest, flight->tsn)) {
			flight->misses++;

			if (flight->misses >= SCTP_FAST_MISSES) {
				abandon(peer, flight->message);
				lost = 1;
			}
		}
	}

	peer->send_ack = cumulative;
	peer->remote_window = mh_rtc_read32(chunk->data + SCTP_SACK_WINDOW);

	while (peer->flight_count != 0 &&
		!mh_sctp_after(flight_at(peer, 0)->tsn, cumulative)) {
		peer->flight_head = (peer->flight_head + 1) % SCTP_WINDOW;
		peer->flight_count--;
	}

	if (peer->forward_pending &&
		!mh_sctp_after(peer->forward_tsn, cumulative))
		peer->forward_pending = peer->forward_due = 0;

	if (peer->recovering && !mh_sctp_after(peer->recovery_end, cumulative))
		peer->recovering = 0;

	if (mh_sctp_after(cumulative, old_ack) && before >= peer->cwnd &&
		!peer->recovering) {
		if (peer->cwnd <= peer->threshold)
			peer->cwnd += bytes < SCTP_CHUNK_BYTES
				? bytes
				: SCTP_CHUNK_BYTES;
		else {
			peer->partial_acked += bytes;

			if (peer->partial_acked >= peer->cwnd) {
				peer->partial_acked -= peer->cwnd;
				peer->cwnd += SCTP_CHUNK_BYTES;
			}
		}

		if (peer->cwnd > SCTP_WINDOW * SCTP_CHUNK_BYTES)
			peer->cwnd = SCTP_WINDOW * SCTP_CHUNK_BYTES;
	}

	if (lost && !peer->recovering) {
		peer->threshold = peer->cwnd / 2;

		if (peer->threshold < SCTP_INITIAL_CHUNKS * SCTP_CHUNK_BYTES)
			peer->threshold =
				SCTP_INITIAL_CHUNKS * SCTP_CHUNK_BYTES;

		peer->cwnd = peer->threshold;
		peer->partial_acked = 0;
		peer->recovering = 1;
		peer->recovery_end = (peer->next_tsn - 1) & SCTP_MASK;
	}

	if (outstanding(peer) == 0)
		peer->timer_running = 0;
	else if (!peer->timer_running || timer_restart) {
		peer->timer_started = now;
		peer->timer_running = 1;
	}

	advance_forward(peer);

	return 1;
}

void
mh_sctp_send_tick(struct mh_sctp *peer, unsigned long now)
{
	unsigned int i;
	struct sctp_flight *flight;

	if (peer->timer_running &&
		mh_sctp_distance(now, peer->timer_started) >= peer->rto) {
		peer->threshold = peer->cwnd / 2;

		if (peer->threshold < SCTP_INITIAL_CHUNKS * SCTP_CHUNK_BYTES)
			peer->threshold =
				SCTP_INITIAL_CHUNKS * SCTP_CHUNK_BYTES;

		peer->cwnd = SCTP_CHUNK_BYTES;
		peer->partial_acked = 0;
		peer->recovering = 0;

		/* No DATA retransmissions: abandon all expired messages. */
		for (i = 0; i < peer->flight_count; i++) {
			flight = flight_at(peer, i);

			if (!flight->acked && !flight->abandoned &&
				mh_sctp_distance(now, flight->sent) >=
					peer->rto)
				abandon(peer, flight->message);
		}

		peer->rto = peer->rto < SCTP_RTO_MAX_MS / 2 ? peer->rto * 2
							: SCTP_RTO_MAX_MS;
		peer->timer_running = outstanding(peer) != 0;
		peer->timer_started = now;
		advance_forward(peer);
	}

	if (peer->forward_pending &&
		mh_sctp_distance(now, peer->forward_sent) >= peer->rto)
		peer->forward_due = 1;
}

void
mh_sctp_send_shutdown(struct mh_sctp *peer)
{
	unsigned int i;

	for (i = 0; i < peer->flight_count; i++)
		abandon(peer, flight_at(peer, i)->message);

	peer->queue_count = 0;
	peer->timer_running = 0;
	advance_forward(peer);
}

int
mh_sctp_data_write(struct mh_sctp *peer, unsigned long now, unsigned char *data,
	size_t *size, unsigned int *flags)
{
	struct sctp_queued *queued;
	struct sctp_flight *flight;
	size_t length;
	unsigned long bytes;

	if (peer->queue_count == 0 || peer->flight_count == SCTP_WINDOW)
		return 0;

	queued = &peer->queued[peer->queue_head];

	if (queued->offset == 0 &&
		peer->flight_count +
				(queued->message.size + SCTP_PAYLOAD - 1) /
					SCTP_PAYLOAD >
			SCTP_WINDOW)
		return 0;

	length = queued->message.size - queued->offset;

	if (length > SCTP_PAYLOAD)
		length = SCTP_PAYLOAD;

	bytes = outstanding(peer);

	if (bytes + length + MH_SCTP_CHUNK_HEADER_BYTES +
			SCTP_DATA_HEADER_BYTES >
		peer->cwnd)
		return 0;

	if (bytes + length > peer->remote_window &&
		(bytes != 0 ||
			mh_sctp_distance(now, peer->last_send) < peer->rto))
		return 0;

	if (bytes == 0 && mh_sctp_distance(now, peer->last_send) >= peer->rto &&
		peer->cwnd > SCTP_INITIAL_CHUNKS * SCTP_CHUNK_BYTES)
		peer->cwnd = SCTP_INITIAL_CHUNKS * SCTP_CHUNK_BYTES;

	if (queued->offset == 0)
		queued->first_tsn = peer->next_tsn;

	*flags = SCTP_DATA_UNORDERED |
		(queued->offset == 0 ? SCTP_DATA_BEGIN : 0) |
		(queued->offset + length == queued->message.size ? SCTP_DATA_END
								: 0);
	mh_rtc_write32(data, peer->next_tsn);
	mh_rtc_write16(data + SCTP_DATA_STREAM, SCTP_CHANNEL);
	mh_rtc_write16(data + SCTP_DATA_SEQUENCE, 0);
	mh_rtc_write32(data + SCTP_DATA_PPID, SCTP_BINARY_PPID);
	memcpy(data + SCTP_DATA_HEADER_BYTES,
		queued->message.data + queued->offset, length);
	*size = length + SCTP_DATA_HEADER_BYTES;
	flight = flight_at(peer, peer->flight_count++);
	memset(flight, 0, sizeof(*flight));
	flight->tsn = peer->next_tsn;
	flight->message = queued->first_tsn;
	flight->sent = now;
	flight->size = length;
	peer->next_tsn = (peer->next_tsn + 1) & SCTP_MASK;
	queued->offset += length;

	if (queued->offset == queued->message.size) {
		peer->queue_head = (peer->queue_head + 1) % SCTP_QUEUE;
		peer->queue_count--;
	}

	if (!peer->timer_running) {
		peer->timer_running = 1;
		peer->timer_started = now;
	}

	peer->last_send = now;

	return 1;
}

enum mh_error_code
mh_sctp_send(struct mh_sctp *peer, const unsigned char *data, size_t size,
	struct mh_error *error_out)
{
	struct sctp_queued *queued;

	if (peer == NULL || data == NULL || size == 0 ||
		size > MH_SCTP_MESSAGE_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid SCTP message");

	if (!mh_sctp_connected(peer))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"SCTP association is not connected");

	if (peer->queue_count == SCTP_QUEUE)
		return mh_error_set(error_out, MH_ERROR_BACKPRESSURE,
			"SCTP send queue is full");

	queued = &peer->queued[(peer->queue_head + peer->queue_count) %
		SCTP_QUEUE];
	memcpy(queued->message.data, data, size);
	queued->message.size = size;
	queued->offset = 0;
	peer->queue_count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
