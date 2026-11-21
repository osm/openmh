#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/udp_provider.h"
#include "backends/network/webrtc_provider.h"
#include "interfaces/clock/backend.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "WebRTC server:%d: %s\n", __LINE__,    \
				#condition);                                   \
			exit(1);                                               \
		}                                                              \
	} while (0)

static const char offer[] =
	"v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
	"m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
	"a=ice-ufrag:test\r\na=ice-pwd:abcdefghijklmnopqrstuv\r\n"
	"a=fingerprint:sha-256 00:01:02:03:04:05:06:07:08:09:0A:0B:0C:0D:0E:0F:10:11:12:13:14:15:16:17:18:19:1A:1B:1C:1D:1E:1F\r\n"
	"a=setup:actpass\r\na=mid:0\r\na=sctp-port:5000\r\n";

static unsigned long ticks;
static unsigned int filtered;

static unsigned long
clock_ticks(void *context)
{
	(void)context;

	return ticks;
}

static int
accept_client(
	void *context, const struct mh_transport_address *address, double now)
{
	(void)context;

	CHECK(now >= 0);
	filtered++;

	return strcmp(address->host, "203.0.113.7") == 0;
}

static enum mh_error_code
submit(struct mh_webrtc_server *server,
	const struct mh_transport_address *client, char *answer,
	size_t capacity, size_t *written, unsigned long *id)
{
	return mh_webrtc_server_offer(server, (const unsigned char *)offer,
		strlen(offer), client, answer, capacity, written, id, NULL);
}

int
main(void)
{
	static const struct mh_clock_ops clock_ops = {
		NULL, NULL, clock_ticks, NULL, NULL};

	static const struct mh_clock_source clock = {&clock_ops, NULL};
	struct mh_webrtc_server_config config;
	struct mh_udp_wait wait;
	struct mh_webrtc_server *server;
	struct mh_transport *transport;
	struct mh_transport_filter filter;
	struct mh_transport_address client;
	struct mh_transport_event event;
	char answer[2048], before[2048], previous[2048];
	unsigned long id;
	unsigned int i;
	size_t written;

	memset(&config, 0, sizeof(config));
	memset(&wait, 0, sizeof(wait));
	config.wait = &wait;
	config.clock = &clock;
	config.bind_ipv4 = 0x7f000001UL;
	config.advertised_ipv4 = 0x7f000001UL;
	config.advertised_port = 40000;
	filter.accept = accept_client;
	filter.context = NULL;
	filter.rejection = "Synthetic rejection";
	config.filter = &filter;
	ticks = 0xfffffff0UL;
	CHECK(mh_webrtc_server_open(&config, &server, &transport, NULL) ==
		MH_ERROR_NONE);
	CHECK(wait.count == 1);
	CHECK(mh_transport_port(transport) != 0 &&
		mh_transport_now(transport) == 0);
	memset(answer, '#', sizeof(answer));
	memcpy(before, answer, sizeof(before));
	id = 99;
	written = 88;
	CHECK(submit(server, NULL, answer, 1, &written, &id) == MH_ERROR_RANGE);
	CHECK(id == 99 && written == 88 &&
		memcmp(answer, before, sizeof(answer)) == 0);
	memset(&client, 0, sizeof(client));
	strcpy(client.host, "203.0.113.8");
	CHECK(submit(server, &client, answer, sizeof(answer), &written, &id) ==
		MH_ERROR_PERMISSION);
	CHECK(filtered == 1 && id == 99 &&
		memcmp(answer, before, sizeof(answer)) == 0);
	strcpy(client.host, "203.0.113.7");

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++) {
		CHECK(submit(server, &client, answer, sizeof(answer), &written,
			&id) == MH_ERROR_NONE);
		CHECK(id == i + 1 && written == strlen(answer));
		CHECK(strstr(answer, "a=ice-lite\r\n") != NULL &&
			strstr(answer, "127.0.0.1 40000 typ host\r\n") != NULL);

		if (i != 0)
			CHECK(strcmp(strstr(answer, "a=ice-ufrag:"),
				strstr(previous, "a=ice-ufrag:")) != 0);

		strcpy(previous, answer);
	}

	CHECK(submit(server, NULL, answer, sizeof(answer), &written, &id) ==
		MH_ERROR_RANGE);
	CHECK(mh_transport_send(transport, id, (const unsigned char *)"a", 1, 0,
		NULL) == MH_ERROR_STATE);
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE &&
		event.type == MH_TRANSPORT_NONE);

	/*
	 * Pending offers expire across the 32-bit clock wrap and reclaim slots.
	 */
	ticks = 14984;
	CHECK(mh_transport_now(transport) == 15000);
	CHECK(submit(server, NULL, answer, sizeof(answer), &written, &id) ==
		MH_ERROR_NONE);
	CHECK(id == MH_WEBRTC_SERVER_PEERS + 1);
	mh_webrtc_server_cancel(server, id);
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE &&
		event.type == MH_TRANSPORT_NONE);
	CHECK(submit(server, NULL, answer, sizeof(answer), &written, &id) ==
			MH_ERROR_NONE &&
		id == MH_WEBRTC_SERVER_PEERS + 2);
	mh_transport_close(transport);
	CHECK(wait.count == 0);

	return 0;
}
