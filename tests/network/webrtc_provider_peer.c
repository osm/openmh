#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/clock/system_provider.h"
#include "backends/network/webrtc_provider.h"
#include "backends/network/webrtc_sdp.h"
#include "interfaces/clock/backend.h"
#include "interfaces/clock/clock.h"

enum {
	CLIENTS = 2,
	TEST_TIMEOUT_MS = 60000,
	TEST_ICE_TIMEOUT_STEP_MS = 31000,
	ANSWER_BYTES = 2048,
	EXPECTED_MESSAGES = 5
};

static unsigned long clock_offset;

static unsigned long
test_ticks(void *context)
{
	(void)context;

	return mh_clock_source_ticks(mh_system_clock_source()) + clock_offset;
}

static int
exchange(struct mh_transport *transport, const unsigned long peers[CLIENTS])
{
	struct mh_transport_event event;
	struct mh_transport_address address;
	struct mh_error error;
	unsigned int connected[CLIENTS], messages[CLIENTS], reliable[CLIENTS];
	unsigned int closed[CLIENTS], finished[CLIENTS], i, ended;
	double deadline;
	enum mh_error_code status;

	memset(connected, 0, sizeof(connected));
	memset(messages, 0, sizeof(messages));
	memset(reliable, 0, sizeof(reliable));
	memset(closed, 0, sizeof(closed));
	memset(finished, 0, sizeof(finished));
	ended = 0;
	deadline = mh_transport_now(transport) + TEST_TIMEOUT_MS;

	while (mh_transport_now(transport) < deadline && ended < CLIENTS) {
		status = mh_transport_poll(transport, 0, &event, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			return 0;
		}

		if (event.type == MH_TRANSPORT_NONE) {
			mh_clock_source_delay(
				mh_system_clock_source(), 1, NULL);
			continue;
		}

		for (i = 0; i < CLIENTS && peers[i] != event.peer; i++)
			;

		if (i == CLIENTS || closed[i])
			return 0;

		if (event.type == MH_TRANSPORT_CONNECT) {
			if (connected[i]++)
				return 0;

			status = mh_transport_peer_address(
				transport, event.peer, &address, NULL);

			if ((i == 0 &&
				(status != MH_ERROR_NONE ||
					strcmp(address.host,
						"203.0.113.7") != 0)) ||
				(i == 1 && status != MH_ERROR_UNSUPPORTED))
				return 0;
		} else if (event.type == MH_TRANSPORT_RECEIVE) {
			if (!connected[i])
				return 0;

			messages[i]++;
			reliable[i] += event.reliable != 0;

			if (event.size == 2 && event.data[0] == i &&
				event.data[1] == 0xdd) {
				if (messages[i] != EXPECTED_MESSAGES ||
					reliable[i] != 2)
					return 0;

				finished[i] = 1;

				if (finished[0] && finished[1]) {
					if (mh_transport_disconnect(transport,
						peers[0],
						NULL) != MH_ERROR_NONE)
						return 0;

					clock_offset +=
						TEST_ICE_TIMEOUT_STEP_MS;
					deadline += TEST_ICE_TIMEOUT_STEP_MS;
				}
			} else if (mh_transport_send(transport, event.peer,
					event.data, event.size,
					event.reliable,
					&error) != MH_ERROR_NONE ||
				mh_transport_flush(transport, &error) !=
					MH_ERROR_NONE) {
				fprintf(stderr, "%s\n", error.message);
				return 0;
			}
		} else if (event.type == MH_TRANSPORT_DISCONNECT) {
			if (!connected[i] || messages[i] != EXPECTED_MESSAGES)
				return 0;

			if (event.size == 0 ||
				event.data[event.size - 1] != 0 ||
				strcmp((const char *)event.data,
					i == 0 ? "WebRTC connection closed"
					: "WebRTC ICE checks timed out") !=
					0)
				return 0;

			closed[i] = 1;
			ended++;
		} else
			return 0;
	}

	return ended == CLIENTS;
}

int
main(int argc, char *argv[])
{
	static const struct mh_clock_ops clock_ops = {
		NULL, NULL, test_ticks, NULL, NULL};

	static const struct mh_clock_source clock = {&clock_ops, NULL};

	struct mh_webrtc_server_config config;
	struct mh_webrtc_server *server;
	struct mh_transport *transport;
	struct mh_transport_address client;
	struct mh_error error;
	struct in_addr address;
	unsigned long peers[CLIENTS];
	unsigned char input[CLIENTS * (MH_RTC_SDP_BYTES + 1)];
	char answer[ANSWER_BYTES];
	size_t size, first, written;
	int success;

	if (argc != 2 || inet_pton(AF_INET, argv[1], &address) != 1)
		return 1;

	size = fread(input, 1, sizeof(input), stdin);

	for (first = 0; first < size && input[first] != 0; first++)
		;

	if (ferror(stdin) || first == size)
		return 1;

	memset(&config, 0, sizeof(config));
	config.clock = &clock;
	config.bind_ipv4 = config.advertised_ipv4 = ntohl(address.s_addr);

	if (mh_webrtc_server_open(&config, &server, &transport, &error) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	success = 0;
	memset(&client, 0, sizeof(client));
	strcpy(client.host, "203.0.113.7");

	if (mh_webrtc_server_offer(server, input, first, &client, answer,
		sizeof(answer), &written, &peers[0],
		&error) != MH_ERROR_NONE)
		goto done;

	if (fwrite(answer, 1, written + 1, stdout) != written + 1)
		goto done;

	if (mh_webrtc_server_offer(server, input + first + 1, size - first - 1,
		NULL, answer, sizeof(answer), &written, &peers[1],
		&error) != MH_ERROR_NONE)
		goto done;

	if (fwrite(answer, 1, written, stdout) != written ||
		puts("--READY--") == EOF || fflush(stdout) == EOF)
		goto done;

	success = exchange(transport, peers);

	if (success)
		puts("PROVIDER VERIFIED; shared UDP, reliable delivery and peer lifecycle passed");

done:
	if (!success)
		fprintf(stderr, "WebRTC provider fixture did not complete\n");

	mh_transport_close(transport);

	return success ? 0 : 1;
}
