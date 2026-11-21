#include <string.h>

#include "backends/network/webrtc_server_internal.h"
#include "backends/network/webrtc_stun_auth.h"
#include "backends/network/webrtc_wire.h"

enum {
	DTLS_CONTENT_FIRST = 20,
	DTLS_CONTENT_LIMIT = 64,
	DTLS_RECORD_HEADER_BYTES = 13,
	DTLS_VERSION_MAJOR = 0xfe,
	STUN_COOKIE_OFFSET = 4
};

/* Classification only; STUN authentication and DTLS validation follow. */
int
mh_webrtc_udp_packet(const unsigned char *data, size_t size)
{
	if (size >= MH_STUN_HEADER_BYTES && (data[0] & 0xc0) == 0 &&
		mh_rtc_read32(data + STUN_COOKIE_OFFSET) == MH_STUN_COOKIE)
		return 1;

	return size >= DTLS_RECORD_HEADER_BYTES &&
		data[0] >= DTLS_CONTENT_FIRST && data[0] < DTLS_CONTENT_LIMIT &&
		data[1] == DTLS_VERSION_MAJOR;
}

static enum mh_error_code
send_datagram(struct mh_webrtc_server *server,
	const struct mh_udp_address *remote, const unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	return server->config.mux != NULL
		? mh_udp_mux_send(
			server->config.mux, remote, data, size, error_out)
		: mh_udp_socket_send(
			server->socket, remote, data, size, error_out);
}

static enum mh_error_code
receive_datagram(struct mh_webrtc_server *server, struct mh_udp_address *remote,
	unsigned char *data, size_t capacity, size_t *size, int *received,
	struct mh_error *error_out)
{
	return server->config.mux != NULL
		? mh_udp_mux_receive(server->config.mux, MH_UDP_MUX_SECONDARY,
			remote, data, capacity, size, received, error_out)
		: mh_udp_socket_receive(server->socket, remote, data, capacity,
			size, received, error_out);
}

static void
release_peer(struct mh_webrtc_peer *peer)
{
	mh_reliable_delivery_destroy(peer->delivery);
	mh_sctp_close(peer->sctp);
	mh_dtls_peer_close(peer->dtls);
	peer->delivery = NULL;
	peer->sctp = NULL;
	peer->dtls = NULL;
	peer->outgoing.size = 0;
}

void
mh_webrtc_peer_clear(struct mh_webrtc_peer *peer)
{
	release_peer(peer);
	memset(peer, 0, sizeof(*peer));
}

void
mh_webrtc_peer_finish(struct mh_webrtc_peer *peer, const char *reason)
{
	if (!peer->announced) {
		mh_webrtc_peer_clear(peer);
		return;
	}

	release_peer(peer);
	peer->closing = 1;
	peer->nominated = 0;
	peer->reason = reason;
}

enum mh_error_code
mh_webrtc_peer_send(void *context, const unsigned char *data, size_t size,
	struct mh_error *error_out)
{
	struct mh_webrtc_peer *peer;

	peer = context;

	return mh_sctp_send(peer->sctp, data, size, error_out);
}

static enum mh_error_code
send_encrypted(struct mh_webrtc_server *server, struct mh_webrtc_peer *peer,
	struct mh_error *error_out)
{
	struct mh_dtls_packet packet;
	enum mh_error_code status;

	while (mh_dtls_outgoing(peer->dtls, &packet)) {
		status = send_datagram(server, &peer->remote, packet.data,
			packet.size, error_out);

		/*
		 * UDP congestion/unreachable errors are packet loss. SCTP and
		 * game delivery own retries; authenticated ICE checks bound the
		 * lifetime of an unreachable peer.
		 */
		if (status != MH_ERROR_NONE && status != MH_ERROR_IO &&
			status != MH_ERROR_RANGE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webrtc_peer_flush(struct mh_webrtc_server *server,
	struct mh_webrtc_peer *peer, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	if (peer->dtls == NULL || peer->closing)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = send_encrypted(server, peer, error_out);

	if (status != MH_ERROR_NONE || !mh_dtls_connected(peer->dtls))
		return status;

	for (i = 0; i < RTC_PUMP_RECORDS; i++) {
		if (peer->outgoing.size == 0) {
			status = mh_sctp_poll(peer->sctp, server->clock_word,
				&peer->outgoing, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		if (peer->outgoing.size == 0)
			break;

		status = mh_dtls_send(peer->dtls, peer->outgoing.data,
			peer->outgoing.size, error_out);

		if (status == MH_ERROR_RANGE)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (status != MH_ERROR_NONE)
			return status;

		peer->outgoing.size = 0;
		status = send_encrypted(server, peer, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webrtc_peer_pump(struct mh_webrtc_server *server,
	struct mh_webrtc_peer *peer, struct mh_error *error_out)
{
	struct mh_dtls_packet plaintext;
	unsigned int i;
	enum mh_error_code status;

	if (peer->dtls == NULL || peer->closing)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < RTC_PUMP_RECORDS; i++) {
		status = mh_dtls_poll(peer->dtls, &plaintext, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (plaintext.size != 0) {
			status = mh_sctp_input(peer->sctp, plaintext.data,
				plaintext.size, server->clock_word, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		if (mh_sctp_closed(peer->sctp))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"WebRTC peer closed its data channel");

		if (peer->delivery == NULL && mh_sctp_connected(peer->sctp)) {
			peer->delivery = mh_reliable_delivery_create(
				mh_webrtc_peer_send, peer);

			if (peer->delivery == NULL)
				return mh_error_set(error_out, MH_ERROR_MEMORY,
					"cannot allocate WebRTC game delivery");
		}

		status = mh_webrtc_peer_flush(server, peer, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (plaintext.size == 0)
			break;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
endpoint_available(const struct mh_webrtc_server *server,
	const struct mh_webrtc_peer *peer, const struct mh_udp_address *source)
{
	unsigned int i;
	const struct mh_webrtc_peer *other;

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++) {
		other = &server->peers[i];

		if (other != peer && other->nominated &&
			mh_udp_address_same(&other->remote, source))
			return 0;
	}

	return 1;
}

static void
receive_stun(struct mh_webrtc_server *server,
	const struct mh_udp_address *source, const unsigned char *data,
	size_t size)
{
	struct mh_stun_message message;
	struct mh_webrtc_peer *peer;
	unsigned char response[RTC_STUN_REPLY_BYTES], address[RTC_WORD_BYTES];
	unsigned int i;
	size_t written;

	if (mh_stun_read(data, size, &message, NULL) != MH_ERROR_NONE ||
		message.type != MH_STUN_BINDING_REQUEST ||
		message.username == NULL)
		return;

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++) {
		peer = &server->peers[i];

		if (peer->id == 0 || peer->closing ||
			message.username_size != strlen(peer->username) ||
			memcmp(message.username, peer->username,
				message.username_size) != 0)
			continue;

		if (mh_webrtc_stun_check(data, size, peer->username,
			peer->password, &message, NULL) != MH_ERROR_NONE ||
			(message.role != NULL && !message.controlling))
			return;

		if (peer->nominated &&
			!mh_udp_address_same(&peer->remote, source))
			return;

		mh_rtc_write32(address, source->ipv4);

		if (mh_webrtc_stun_reply(&message, address, sizeof(address),
			source->port, peer->password, response,
			sizeof(response), &written, NULL) != MH_ERROR_NONE)
			return;

		(void)send_datagram(server, source, response, written, NULL);

		if (!peer->nominated && message.nominated &&
			message.has_priority && message.role != NULL &&
			message.controlling &&
			endpoint_available(server, peer, source)) {
			if (mh_dtls_peer_open(server->identity, 1,
				peer->offer.fingerprint, &peer->dtls,
				NULL) != MH_ERROR_NONE ||
				mh_sctp_open(&peer->sctp_config, &peer->sctp,
					NULL) != MH_ERROR_NONE) {
				mh_webrtc_peer_finish(peer,
					"Cannot allocate WebRTC connection");
				return;
			}

			peer->remote = *source;
			peer->nominated = 1;
		}

		/*
		 * ICE-lite answers consent checks; it does not initiate them.
		 */
		if (peer->nominated)
			peer->ice_ms = server->elapsed_ms;

		return;
	}
}

enum mh_error_code
mh_webrtc_server_read(
	struct mh_webrtc_server *server, struct mh_error *error_out)
{
	unsigned char data[MH_DTLS_PACKET_BYTES + 1];
	struct mh_udp_address source;
	struct mh_webrtc_peer *peer;
	unsigned int packet, i;
	size_t size;
	int received;
	enum mh_error_code status;

	for (packet = 0; packet < RTC_POLL_DATAGRAMS; packet++) {
		status = receive_datagram(server, &source, data, sizeof(data),
			&size, &received, error_out);

		if (status != MH_ERROR_NONE || !received)
			return status;

		if (size == 0 || size > MH_DTLS_PACKET_BYTES)
			continue;

		if (data[0] < DTLS_CONTENT_FIRST ||
			data[0] >= DTLS_CONTENT_LIMIT) {
			receive_stun(server, &source, data, size);
			continue;
		}

		for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++) {
			peer = &server->peers[i];

			if (!peer->nominated || peer->closing ||
				!mh_udp_address_same(&source, &peer->remote))
				continue;

			status = mh_dtls_input(peer->dtls, data, size, NULL);

			if (status == MH_ERROR_RANGE)
				break;

			if (status != MH_ERROR_NONE ||
				mh_webrtc_peer_pump(server, peer, NULL) !=
					MH_ERROR_NONE)
				mh_webrtc_peer_finish(
					peer, "WebRTC connection failed");

			break;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
