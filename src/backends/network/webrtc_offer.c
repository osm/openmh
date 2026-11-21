#include <string.h>

#include "backends/network/webrtc_server_internal.h"
#include "backends/network/webrtc_wire.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"

static enum mh_error_code
hex_text(const unsigned char *data, size_t size, char *output, size_t capacity,
	struct mh_error *error_out)
{
	static const char digits[] = "0123456789abcdef";
	size_t i;

	if (capacity == 0 || size > (capacity - 1) / 2)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"WebRTC credential buffer is too small");

	for (i = 0; i < size; i++) {
		output[i * 2] = digits[data[i] >> 4];
		output[i * 2 + 1] = digits[data[i] & 15];
	}

	output[size * 2] = 0;

	return MH_ERROR_NONE;
}

enum mh_error_code
mh_webrtc_server_offer(struct mh_webrtc_server *server,
	const unsigned char *offer, size_t size,
	const struct mh_transport_address *client, char *answer,
	size_t capacity, size_t *written, unsigned long *peer_out,
	struct mh_error *error_out)
{
	struct mh_rtc_offer remote;
	struct mh_rtc_answer local;
	struct mh_webrtc_peer *peer;
	struct mh_webrtc_peer prepared;
	unsigned char random[RTC_ICE_UFRAG_RANDOM_BYTES +
		RTC_ICE_PASSWORD_RANDOM_BYTES + 2 * RTC_WORD_BYTES +
		MH_SCTP_COOKIE_BYTES];
	char encoded[RTC_SDP_ANSWER_BYTES];
	size_t count, offset;
	unsigned int i;
	enum mh_error_code status;

	if (server == NULL || answer == NULL || written == NULL ||
		peer_out == NULL ||
		(client != NULL &&
			(!mh_text_field_valid(
				client->host, sizeof(client->host)) ||
				client->host[0] == 0 ||
				client->port > MH_U16_MAX)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebRTC offer arguments");

	status = mh_rtc_offer_read(offer, size, &remote, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_webrtc_server_time(server);
	mh_webrtc_server_expire(server);

	if (client != NULL && server->config.filter != NULL &&
		!server->config.filter->accept(server->config.filter->context,
			client, server->elapsed_ms))
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			server->config.filter->rejection);

	peer = NULL;

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++)
		if (server->peers[i].id == 0) {
			peer = &server->peers[i];
			break;
		}

	if (peer == NULL || server->last_id == MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"WebRTC connection limit reached");

	status = mh_webrtc_random(random, sizeof(random), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&local, 0, sizeof(local));
	status = hex_text(random, RTC_ICE_UFRAG_RANDOM_BYTES, local.ufrag,
		sizeof(local.ufrag), error_out);
	offset = RTC_ICE_UFRAG_RANDOM_BYTES;

	if (status == MH_ERROR_NONE)
		status = hex_text(random + offset,
			RTC_ICE_PASSWORD_RANDOM_BYTES, local.password,
			sizeof(local.password), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	offset += RTC_ICE_PASSWORD_RANDOM_BYTES;
	memcpy(local.fingerprint, mh_dtls_fingerprint(server->identity),
		sizeof(local.fingerprint));
	mh_rtc_write32(local.address, server->config.advertised_ipv4);
	local.address_size = RTC_WORD_BYTES;
	local.udp_port = server->config.advertised_port == 0
		? mh_webrtc_server_port(server)
		: server->config.advertised_port;

	local.sctp_port = RTC_SCTP_PORT;
	local.session_id = server->last_id + 1;
	status = mh_rtc_answer_write(
		&remote, &local, encoded, sizeof(encoded), &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count >= capacity)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"WebRTC answer buffer is too small");

	memset(&prepared, 0, sizeof(prepared));

	if (mh_text_copy(prepared.username, sizeof(prepared.username),
		local.ufrag, error_out) != MH_ERROR_NONE ||
		mh_text_append(prepared.username, sizeof(prepared.username),
			":", error_out) != MH_ERROR_NONE ||
		mh_text_append(prepared.username, sizeof(prepared.username),
			remote.ufrag, error_out) != MH_ERROR_NONE ||
		mh_text_copy(prepared.password, sizeof(prepared.password),
			local.password, error_out) != MH_ERROR_NONE)
		return MH_ERROR_RANGE;

	/*
	 * Publish the slot only after all fallible preparation has completed.
	 */
	*peer = prepared;
	peer->offer = remote;
	peer->opened_ms = server->elapsed_ms;
	peer->id = ++server->last_id;
	peer->sctp_config.local_port = RTC_SCTP_PORT;
	peer->sctp_config.remote_port = remote.sctp_port;
	peer->sctp_config.tag = mh_rtc_read32(random + offset) | 1;
	offset += RTC_WORD_BYTES;
	peer->sctp_config.initial_tsn = mh_rtc_read32(random + offset);
	offset += RTC_WORD_BYTES;
	memcpy(peer->sctp_config.cookie, random + offset, MH_SCTP_COOKIE_BYTES);

	if (client != NULL) {
		peer->client = *client;
		peer->has_client = 1;
	}

	memcpy(answer, encoded, count + 1);
	*written = count;
	*peer_out = peer->id;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
