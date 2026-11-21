#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_dtls.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "DTLS:%d: %s\n", __LINE__,             \
				#condition);                                   \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void
transfer(struct mh_dtls_peer *from, struct mh_dtls_peer *to)
{
	struct mh_dtls_packet packet;

	while (mh_dtls_outgoing(from, &packet)) {
		CHECK(packet.size <= 1200);
		CHECK(mh_dtls_input(to, packet.data, packet.size, NULL) ==
			MH_ERROR_NONE);
	}
}

static void
handshake(int mismatch)
{
	struct mh_dtls_identity *client_identity, *server_identity;
	struct mh_dtls_peer *client, *server;
	struct mh_dtls_packet packet, encrypted;
	unsigned char fingerprint[32], data[MH_DTLS_SEND_BYTES];
	unsigned int i, received;
	enum mh_error_code status;

	CHECK(mh_dtls_identity_open(&client_identity, NULL) == MH_ERROR_NONE);
	CHECK(mh_dtls_identity_open(&server_identity, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(mh_dtls_fingerprint(client_identity),
		mh_dtls_fingerprint(server_identity), 32) != 0);
	memcpy(fingerprint, mh_dtls_fingerprint(server_identity), 32);

	if (mismatch == 1)
		fingerprint[0] ^= 1;

	CHECK(mh_dtls_peer_open(client_identity, 0, fingerprint, &client,
		NULL) == MH_ERROR_NONE);
	memcpy(fingerprint, mh_dtls_fingerprint(client_identity), 32);

	if (mismatch == 2)
		fingerprint[0] ^= 1;

	CHECK(mh_dtls_peer_open(server_identity, 1, fingerprint, &server,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_dtls_send(client, (const unsigned char *)"early", 5, NULL) ==
		MH_ERROR_STATE);
	status = MH_ERROR_NONE;

	for (i = 0; i < 64 &&
		(!mh_dtls_connected(client) || !mh_dtls_connected(server));
		i++) {
		status = mh_dtls_poll(client, &packet, NULL);

		if (status != MH_ERROR_NONE)
			break;

		CHECK(packet.size == 0);
		transfer(client, server);
		status = mh_dtls_poll(server, &packet, NULL);

		if (status != MH_ERROR_NONE)
			break;

		CHECK(packet.size == 0);
		transfer(server, client);
	}

	if (mismatch) {
		CHECK(status != MH_ERROR_NONE);
		CHECK(!mh_dtls_connected(client) || !mh_dtls_connected(server));
	} else {
		CHECK(status == MH_ERROR_NONE && mh_dtls_connected(client) &&
			mh_dtls_connected(server));
		memset(data, 42, sizeof(data));
		CHECK(mh_dtls_send(client, data, sizeof(data), NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_dtls_outgoing(client, &encrypted));
		CHECK(encrypted.size > sizeof(data) && encrypted.size <= 1200);
		packet = encrypted;
		packet.data[packet.size - 1] ^= 1;
		CHECK(mh_dtls_input(server, packet.data, packet.size, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_dtls_poll(server, &packet, NULL) == MH_ERROR_NONE &&
			packet.size == 0);
		CHECK(mh_dtls_input(server, encrypted.data, encrypted.size,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_dtls_poll(server, &packet, NULL) == MH_ERROR_NONE &&
			packet.size == sizeof(data));
		CHECK(memcmp(packet.data, data, sizeof(data)) == 0);
		CHECK(mh_dtls_input(server, encrypted.data, encrypted.size,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_dtls_poll(server, &packet, NULL) == MH_ERROR_NONE &&
			packet.size == 0);

		/*
		 * Four encrypted records plus one copied pending write are
		 * bounded.
		 */
		for (i = 0; i < 5; i++) {
			data[0] = (unsigned char)i;
			CHECK(mh_dtls_send(server, data, sizeof(data), NULL) ==
				MH_ERROR_NONE);
		}

		CHECK(mh_dtls_send(server, data, sizeof(data), NULL) ==
			MH_ERROR_RANGE);
		memset(data, 0, sizeof(data));

		/*
		 * Polling a full output queue must preserve the pending write.
		 */
		for (i = 0; i < 3; i++)
			CHECK(mh_dtls_poll(server, &packet, NULL) ==
					MH_ERROR_NONE &&
				packet.size == 0);

		transfer(server, client);

		for (received = 0; received < 4; received++) {
			CHECK(mh_dtls_poll(client, &packet, NULL) ==
					MH_ERROR_NONE &&
				packet.size == sizeof(data));
			CHECK(packet.data[0] == received &&
				packet.data[1] == 42);
		}

		CHECK(mh_dtls_poll(server, &packet, NULL) == MH_ERROR_NONE);
		transfer(server, client);
		CHECK(mh_dtls_poll(client, &packet, NULL) == MH_ERROR_NONE &&
			packet.size == sizeof(data));
		CHECK(packet.data[0] == 4 && packet.data[1] == 42);
		CHECK(mh_dtls_poll(client, &packet, NULL) == MH_ERROR_NONE &&
			packet.size == 0);
	}

	mh_dtls_peer_close(client);
	mh_dtls_peer_close(server);
	mh_dtls_identity_close(client_identity);
	mh_dtls_identity_close(server_identity);
}

int
main(void)
{
	handshake(0);
	handshake(1);
	handshake(2);

	return 0;
}
