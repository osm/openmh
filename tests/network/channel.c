#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "engine/network/channel.h"
#include "interfaces/network/backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "channel:%d: %s\n", __LINE__, #e);     \
			return 1;                                              \
		}                                                              \
	} while (0)

struct endpoint {
	struct mh_transport_event	incoming;
	unsigned char			sent[MH_NET_WIRE_PACKET_LIMIT];
	size_t				sent_size;
	unsigned int			sends;
	double				now;
};

static void
packet(struct mh_channel *channel, double now, int reliable,
	unsigned char *bytes)
{
	mh_channel_header(channel, now, bytes);

	mh_channel_sent(channel, now, reliable);
}

static int
measurements(void)
{
	struct mh_channel a, b, saved;
	unsigned char first[MH_CHANNEL_HEADER_BYTES],
		second[MH_CHANNEL_HEADER_BYTES];
	unsigned int i;

	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	packet(&a, 0, 0, first);
	CHECK(mh_channel_receive(&b, first, sizeof(first), 0, 10, NULL) ==
		MH_ERROR_NONE);
	packet(&b, 40, 0, second);
	CHECK(mh_channel_receive(&a, second, sizeof(second), 0, 50, NULL) ==
		MH_ERROR_NONE);

	/* 10 ms in each direction; the 30 ms ACK delay is not network RTT. */
	CHECK(a.sample_count == 1 && a.latest_ms == 20 && a.sum_ms == 20);
	CHECK(mh_channel_receive(&a, second, sizeof(second), 0, 75, NULL) ==
		MH_ERROR_NONE);
	CHECK(a.sample_count == 1);

	/* Reliable delivery/retries do not provide an unambiguous send time. */
	packet(&a, 100, 1, first);
	CHECK(mh_channel_receive(&b, first, sizeof(first), 0, 110, NULL) ==
		MH_ERROR_NONE);
	packet(&b, 120, 0, second);
	CHECK(mh_channel_receive(&a, second, sizeof(second), 0, 130, NULL) ==
		MH_ERROR_NONE);
	CHECK(a.sample_count == 1);

	/* Reliable ACK delivery must not turn its retries into apparent RTT. */
	packet(&a, 140, 0, first);
	CHECK(mh_channel_receive(&b, first, sizeof(first), 0, 150, NULL) ==
		MH_ERROR_NONE);
	packet(&b, 160, 1, second);
	CHECK(mh_channel_receive(&a, second, sizeof(second), 1, 180, NULL) ==
		MH_ERROR_NONE);
	CHECK(a.sample_count == 1);
	packet(&b, 185, 0, second);
	CHECK(mh_channel_receive(&a, second, sizeof(second), 0, 195, NULL) ==
		MH_ERROR_NONE);
	CHECK(a.sample_count == 2 && a.latest_ms == 20);

	/*
	 * Late data still reaches its caller but cannot move the ACK backwards.
	 */
	packet(&a, 200, 0, first);
	packet(&a, 201, 0, second);
	CHECK(mh_channel_receive(&b, second, sizeof(second), 0, 210, NULL) ==
		MH_ERROR_NONE);
	saved = b;
	CHECK(mh_channel_receive(&b, first, sizeof(first), 0, 211, NULL) ==
		MH_ERROR_NONE);
	CHECK(b.received_sequence == saved.received_sequence &&
		b.received_ms == saved.received_ms);

	/* Keep a bounded rolling mean, including valid zero-ms samples. */
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));

	for (i = 0; i < MH_CHANNEL_HISTORY + 2; i++) {
		packet(&a, i * 100, 0, first);
		CHECK(mh_channel_receive(&b, first, sizeof(first), 0, i * 100,
			NULL) == MH_ERROR_NONE);
		packet(&b, i * 100, 0, second);
		CHECK(mh_channel_receive(&a, second, sizeof(second), 0,
			i * 100 + (i & 1) * 20, NULL) == MH_ERROR_NONE);
	}

	CHECK(a.sample_count == MH_CHANNEL_HISTORY &&
		a.sum_ms / a.sample_count == 10);

	/*
	 * ACKs for overwritten history, unsent sequences and malformed frames.
	 */
	packet(&a, 10000, 0, first);
	CHECK(mh_channel_receive(&b, first, sizeof(first), 0, 10010, NULL) ==
		MH_ERROR_NONE);
	packet(&b, 10020, 0, second);

	for (i = 0; i < MH_CHANNEL_HISTORY; i++)
		packet(&a, 10021 + i, 0, first);

	saved = a;
	CHECK(mh_channel_receive(&a, second, sizeof(second), 0, 10100, NULL) ==
		MH_ERROR_NONE);
	CHECK(a.sum_ms == saved.sum_ms);
	mh_write_u32le(second + 8, MH_U32_MAX);
	CHECK(mh_channel_receive(&a, second, sizeof(second), 0, 10101, NULL) ==
		MH_ERROR_NONE);
	CHECK(a.sum_ms == saved.sum_ms);
	saved = a;
	second[0] = 0;
	CHECK(mh_channel_receive(&a, second, sizeof(second), 0, 10102, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&a, &saved, sizeof(a)) == 0);
	CHECK(mh_channel_receive(&a, second, 1, 0, 10102, NULL) ==
		MH_ERROR_INVALID);

	/* Sequence zero stays reserved when the 32-bit counter wraps. */
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	a.sent_sequence = MH_U32_MAX - 1;
	packet(&a, 0, 0, first);
	CHECK(mh_channel_receive(&b, first, sizeof(first), 0, 10, NULL) ==
		MH_ERROR_NONE);
	packet(&a, 20, 0, first);
	CHECK(a.sent_sequence == 1);
	CHECK(mh_channel_receive(&b, first, sizeof(first), 0, 30, NULL) ==
		MH_ERROR_NONE);
	CHECK(b.received_sequence == 1);
	packet(&b, 40, 0, second);
	CHECK(mh_channel_receive(&a, second, sizeof(second), 0, 50, NULL) ==
		MH_ERROR_NONE);
	CHECK(a.latest_ms == 20);

	return 0;
}

static enum mh_error_code
open_endpoint(void *context, int server, const void *local, void **output,
	struct mh_error *error_out)
{
	(void)server;

	(void)local;
	*output = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_endpoint(void *context, void *token)
{
	(void)context;
	(void)token;
}

static enum mh_error_code
connect_endpoint(void *context, void *token, const void *target,
	struct mh_error *error_out)
{
	(void)context;

	(void)token;
	(void)target;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
poll_endpoint(void *context, void *token, struct mh_transport_event *output,
	struct mh_error *error_out)
{
	struct endpoint *endpoint;

	(void)context;
	endpoint = token;
	*output = endpoint->incoming;
	memset(&endpoint->incoming, 0, sizeof(endpoint->incoming));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_endpoint(void *context, void *token, unsigned long peer,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct endpoint *endpoint;

	(void)context;
	(void)peer;
	(void)reliable;
	endpoint = token;

	if (size > sizeof(endpoint->sent))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "fixture packet overflow");

	memcpy(endpoint->sent, data, size);
	endpoint->sent_size = size;
	endpoint->sends++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
flush_endpoint(void *context, void *token, struct mh_error *error_out)
{
	(void)context;
	(void)token;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
disconnect_endpoint(void *context, void *token, unsigned long peer,
	struct mh_error *error_out)
{
	(void)peer;

	return flush_endpoint(context, token, error_out);
}

static double
now_endpoint(void *context, void *token)
{
	(void)context;

	return ((struct endpoint *)token)->now;
}

static int
dispatch(void)
{
	static const struct mh_transport_ops ops = {open_endpoint,
		close_endpoint, connect_endpoint, poll_endpoint, send_endpoint,
		flush_endpoint, disconnect_endpoint, NULL, now_endpoint, NULL,
		NULL};

	struct mh_transport_provider provider;
	struct mh_transport *transport;
	struct mh_transport_event event;
	struct mh_transport_timing timing;
	struct mh_channel remote;
	struct endpoint endpoint;
	const unsigned char data[] = "normal gameplay";
	unsigned int sends;

	memset(&endpoint, 0, sizeof(endpoint));
	memset(&remote, 0, sizeof(remote));
	CHECK(mh_transport_provider_init(&provider, &ops, NULL, NULL, &endpoint,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_open_from(&provider, 0, NULL, &transport, NULL) ==
		MH_ERROR_NONE);

	/*
	 * Legacy remains byte-for-byte transparent and does not send
	 * keepalives.
	 */
	CHECK(mh_transport_send(transport, 1, data, sizeof(data), 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(endpoint.sent_size == sizeof(data) &&
		!memcmp(endpoint.sent, data, sizeof(data)));
	CHECK(mh_transport_peer_timing(transport, 1, &timing, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_transport_enable_channel(transport, NULL) == MH_ERROR_NONE);
	endpoint.incoming.type = MH_TRANSPORT_CONNECT;
	endpoint.incoming.peer = 1;
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_peer_timing(transport, 1, &timing, NULL) ==
			MH_ERROR_NONE &&
		timing.samples == 0);
	CHECK(mh_transport_send(transport, 1, data, sizeof(data), 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(endpoint.sent_size == sizeof(data) + MH_CHANNEL_HEADER_BYTES);
	CHECK(mh_channel_receive(&remote, endpoint.sent, endpoint.sent_size, 0,
		10, NULL) == MH_ERROR_NONE);
	packet(&remote, 40, 0, endpoint.incoming.data);
	memcpy(endpoint.incoming.data + MH_CHANNEL_HEADER_BYTES, data,
		sizeof(data));
	endpoint.incoming.type = MH_TRANSPORT_RECEIVE;
	endpoint.incoming.peer = 1;
	endpoint.incoming.size = MH_CHANNEL_HEADER_BYTES + sizeof(data);
	endpoint.now = 50;
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == MH_TRANSPORT_RECEIVE &&
		event.size == sizeof(data) &&
		!memcmp(event.data, data, sizeof(data)));
	CHECK(mh_transport_peer_timing(transport, 1, &timing, NULL) ==
		MH_ERROR_NONE);
	CHECK(timing.samples == 1 && timing.mean_ms == 20 &&
		timing.latest_ms == 20);
	endpoint.now = MH_CHANNEL_IDLE_MS;
	sends = endpoint.sends;
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(endpoint.sends == sends + 1 &&
		endpoint.sent_size == MH_CHANNEL_HEADER_BYTES);
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE &&
		endpoint.sends == sends + 1);
	CHECK(mh_transport_disconnect(transport, 1, NULL) == MH_ERROR_NONE);
	endpoint.now += MH_CHANNEL_IDLE_MS;
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE &&
		endpoint.sends == sends + 1);
	CHECK(mh_transport_peer_timing(transport, 1, &timing, NULL) ==
		MH_ERROR_STATE);
	mh_transport_close(transport);

	return 0;
}

int
main(void)
{
	return measurements() || dispatch();
}
