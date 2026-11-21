#include <string.h>

#include "backends/network/udp_challenge.h"
#include "backends/network/udp_transport.h"
#include "engine/core/binary.h"

enum {
	COOKIE_WINDOW_MS = 10000,
	REPLY_WINDOW_MS = 1000,
	REPLIES_PER_WINDOW = 128,
	REPLIES_PER_BUCKET = 4 * MH_UDP_PEERS,
	COOKIE_WORD_BYTES = 4,
	COOKIE_WORDS = 5
};

static unsigned long
cookie_epoch(const struct mh_udp_transport *transport)
{
	return (unsigned long)(transport->elapsed_ms / COOKIE_WINDOW_MS);
}

int
mh_udp_reply_allowed(struct mh_udp_transport *transport, unsigned long ipv4)
{
	unsigned int bucket;

	if (transport->elapsed_ms >= transport->reply_reset_ms) {
		transport->reply_reset_ms =
			transport->elapsed_ms + REPLY_WINDOW_MS;
		transport->reply_count = 0;
		memset(transport->reply_buckets, 0,
			sizeof(transport->reply_buckets));
	}

	bucket = (unsigned int)((ipv4 ^ (ipv4 >> 16)) % MH_UDP_REPLY_BUCKETS);

	if (transport->reply_count >= REPLIES_PER_WINDOW ||
		transport->reply_buckets[bucket] >= REPLIES_PER_BUCKET)
		return 0;

	transport->reply_count++;
	transport->reply_buckets[bucket]++;

	return 1;
}

static void
make_cookie(const struct mh_udp_transport *transport,
	const struct mh_udp_address *source,
	const struct mh_shared_packet *request, unsigned long epoch,
	unsigned char cookie[MH_NET_NONCE_BYTES])
{
	unsigned char digest[MH_SHA256_BYTES],
		words[COOKIE_WORDS * COOKIE_WORD_BYTES];

	mh_write_u32le(words, source->ipv4);
	mh_write_u32le(words + COOKIE_WORD_BYTES, source->port);
	mh_write_u32le(
		words + 2 * COOKIE_WORD_BYTES, request->client_generation);
	mh_write_u32le(words + 3 * COOKIE_WORD_BYTES, epoch);
	mh_write_u32le(words + 4 * COOKIE_WORD_BYTES,
		request->type == MH_SHARED_QUERY ||
			request->type == MH_SHARED_QUERY_CONFIRM);
	mh_sha256_hmac(transport->cookie_key, sizeof(transport->cookie_key),
		words, sizeof(words), digest);
	memcpy(cookie, digest, MH_NET_NONCE_BYTES);
}

int
mh_udp_cookie_valid(const struct mh_udp_transport *transport,
	const struct mh_udp_address *source,
	const struct mh_shared_packet *request)
{
	unsigned char cookie[MH_NET_NONCE_BYTES];
	unsigned long epoch;

	epoch = cookie_epoch(transport);
	make_cookie(transport, source, request, epoch, cookie);

	if (mh_digest_equal(request->data, cookie, sizeof(cookie)))
		return 1;

	if (epoch == 0)
		return 0;

	make_cookie(transport, source, request, epoch - 1, cookie);

	return mh_digest_equal(request->data, cookie, sizeof(cookie));
}

enum mh_error_code
mh_udp_shared_send(struct mh_udp_socket *socket,
	const struct mh_udp_address *remote,
	const struct mh_shared_packet *packet, struct mh_error *error_out)
{
	unsigned char data[MH_SHARED_PACKET_BYTES];
	size_t size;
	enum mh_error_code status;

	status = mh_shared_packet_write(
		packet, data, sizeof(data), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_udp_socket_send(socket, remote, data, size, error_out);
}

void
mh_udp_challenge_reply(struct mh_udp_transport *transport,
	const struct mh_udp_address *source, struct mh_shared_packet packet)
{
	unsigned char cookie[MH_NET_NONCE_BYTES];

	make_cookie(
		transport, source, &packet, cookie_epoch(transport), cookie);
	packet.type = packet.type == MH_SHARED_QUERY ? MH_SHARED_QUERY_CHALLENGE
						: MH_SHARED_CHALLENGE;
	packet.data = cookie;
	packet.size = sizeof(cookie);
	(void)mh_udp_shared_send(transport->socket, source, &packet, NULL);
}
