#define _POSIX_C_SOURCE 200112L

#include <sys/select.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "backends/network/webrtc_dtls.h"
#include "backends/network/webrtc_sctp.h"
#include "backends/network/webrtc_sctp_packet.h"
#include "backends/network/webrtc_sdp.h"
#include "backends/network/webrtc_stun_auth.h"
#include "backends/network/webrtc_wire.h"
#include "engine/core/text.h"

static int
same_address(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
	return a->sin_addr.s_addr == b->sin_addr.s_addr &&
		a->sin_port == b->sin_port;
}

static int
run_peer(int socket, struct mh_dtls_peer *peer,
	const struct mh_rtc_offer *offer, const struct mh_rtc_answer *answer,
	struct mh_sctp *association)
{
	struct mh_sctp_datagram outgoing;
	struct mh_sctp_message message;
	struct mh_dtls_packet plaintext, packet;
	struct mh_stun_message stun;
	struct mh_sctp_packet sctp;
	struct mh_sctp_chunk chunk;
	struct sockaddr_in source, selected;
	struct timeval timeout;
	struct mh_error error;
	unsigned char address[4], response[80];
	char username[MH_RTC_ICE_BYTES + 34];
	socklen_t length;
	size_t written, offset;
	ssize_t received;

	fd_set readable;
	time_t deadline;
	int nominated, messages, dropped, forward_seen, forward_acked, finished;
	unsigned long forward_tsn;
	unsigned long now;
	struct timespec monotonic;
	enum mh_error_code status;

	memset(&selected, 0, sizeof(selected));

	if (mh_text_copy(username, sizeof(username), answer->ufrag, NULL) !=
			MH_ERROR_NONE ||
		mh_text_append(username, sizeof(username), ":", NULL) !=
			MH_ERROR_NONE ||
		mh_text_append(username, sizeof(username), offer->ufrag,
			NULL) != MH_ERROR_NONE)
		return 0;

	deadline = time(NULL) + 15;
	nominated = 0;
	messages = 0;
	dropped = forward_seen = forward_acked = finished = 0;
	forward_tsn = 0;
	outgoing.size = 0;

	while (time(NULL) < deadline) {
		FD_ZERO(&readable);
		FD_SET(socket, &readable);
		timeout.tv_sec = 0;
		timeout.tv_usec = 20000;

		if (select(socket + 1, &readable, NULL, NULL, &timeout) < 0 &&
			errno != EINTR)
			return 0;

		length = sizeof(source);
		received = recvfrom(socket, packet.data, sizeof(packet.data), 0,
			(struct sockaddr *)&source, &length);

		if (received > 0) {
			if (mh_webrtc_stun_check(packet.data, (size_t)received,
				username, answer->password, &stun,
				NULL) == MH_ERROR_NONE) {
				memcpy(address, &source.sin_addr.s_addr, 4);

				if (mh_webrtc_stun_reply(&stun, address, 4,
					ntohs(source.sin_port),
					answer->password, response,
					sizeof(response), &written,
					NULL) != MH_ERROR_NONE)
					return 0;

				if (sendto(socket, response, written, 0,
					(const struct sockaddr *)&source,
					sizeof(source)) != (ssize_t)written)
					return 0;

				if (stun.nominated && stun.has_priority &&
					stun.role != NULL && stun.controlling &&
					!nominated) {
					selected = source;
					nominated = 1;
				}
			} else if (nominated &&
				same_address(&source, &selected) &&
				packet.data[0] >= 20 && packet.data[0] < 64) {
				if (mh_dtls_input(peer, packet.data,
					(size_t)received,
					NULL) != MH_ERROR_NONE)
					return 0;
			}
		}

		if (!nominated)
			continue;

		if (mh_dtls_poll(peer, &plaintext, &error) != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			ERR_print_errors_fp(stderr);
			return 0;
		}

		while (mh_dtls_outgoing(peer, &packet))
			if (sendto(socket, packet.data, packet.size, 0,
				(const struct sockaddr *)&selected,
				sizeof(selected)) != (ssize_t)packet.size)
				return 0;

		if (association != NULL) {
			if (clock_gettime(CLOCK_MONOTONIC, &monotonic) != 0)
				return 0;

			now = ((unsigned long)monotonic.tv_sec * 1000UL +
				(unsigned long)monotonic.tv_nsec /
					1000000UL) &
				0xffffffffUL;

			if (plaintext.size != 0 &&
				mh_sctp_input(association, plaintext.data,
					plaintext.size, now,
					&error) != MH_ERROR_NONE) {
				fprintf(stderr, "%s\n", error.message);
				return 0;
			}

			if (plaintext.size != 0 && forward_seen &&
				mh_sctp_packet_read(plaintext.data,
					plaintext.size, &sctp,
					NULL) == MH_ERROR_NONE) {
				offset = MH_SCTP_HEADER_BYTES;

				while (mh_sctp_chunk_next(
					&sctp, &offset, &chunk) == 1)
					if (chunk.type == 3 &&
						chunk.size >= 12 &&
						((mh_rtc_read32(chunk.data) -
							forward_tsn) &
							0xffffffffUL) <
							0x80000000UL)
						forward_acked = 1;
			}

			while (mh_sctp_receive(association, &message)) {
				if (messages == 5 && message.size == 1 &&
					message.data[0] == 0xdd) {
					finished = 1;
					continue;
				}

				if (mh_sctp_send(association, message.data,
					message.size,
					&error) != MH_ERROR_NONE) {
					fprintf(stderr, "%s\n", error.message);
					return 0;
				}

				messages++;
			}

			if (finished && dropped && forward_acked) {
				puts("SCTP VERIFIED; message exchange and loss recovery passed");
				return 1;
			}

			if (outgoing.size == 0 &&
				mh_sctp_poll(association, now, &outgoing,
					&error) != MH_ERROR_NONE) {
				fprintf(stderr, "%s\n", error.message);
				return 0;
			}

			if (outgoing.size != 0) {
				if (mh_sctp_packet_read(outgoing.data,
					outgoing.size, &sctp,
					NULL) != MH_ERROR_NONE)
					return 0;

				offset = MH_SCTP_HEADER_BYTES;

				while (mh_sctp_chunk_next(
					&sctp, &offset, &chunk) == 1) {
					if (!dropped && chunk.type == 0 &&
						(chunk.flags & 2) &&
						chunk.size > 12 &&
						chunk.data[12] == 0xee) {
						/*
						 * Lose a fragment after SCTP
						 * accounts for its
						 * transmission.
						 */
						outgoing.size = 0;
						dropped = 1;
					}

					if (chunk.type == 192 &&
						chunk.size == 4) {
						forward_tsn = mh_rtc_read32(
							chunk.data);
						forward_seen = 1;
					}
				}

				if (outgoing.size == 0)
					continue;

				status = mh_dtls_send(peer, outgoing.data,
					outgoing.size, &error);

				if (status == MH_ERROR_NONE)
					outgoing.size = 0;
				else if (status != MH_ERROR_RANGE) {
					fprintf(stderr, "%s\n", error.message);
					return 0;
				}
			}
		} else if (plaintext.size != 0) {
			offset = MH_SCTP_HEADER_BYTES;

			if (!mh_dtls_connected(peer) ||
				mh_sctp_packet_read(plaintext.data,
					plaintext.size, &sctp,
					NULL) != MH_ERROR_NONE ||
				sctp.source != offer->sctp_port ||
				sctp.destination != answer->sctp_port ||
				sctp.tag != 0 ||
				mh_sctp_chunk_next(&sctp, &offset, &chunk) !=
					1 ||
				chunk.type != 1)
				return 0;

			puts("DTLS VERIFIED; received browser SCTP INIT");

			return 1;
		}
	}

	fprintf(stderr,
		"browser DTLS handshake timed out (nominated=%d, connected=%d)\n",
		nominated, mh_dtls_connected(peer));

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_dtls_identity *identity;
	struct mh_dtls_peer *peer;
	struct mh_sctp *association;
	struct mh_sctp_config config;
	unsigned char random[40];
	struct mh_rtc_offer offer;
	struct mh_rtc_answer answer;
	struct sockaddr_in local;
	unsigned char input[MH_RTC_SDP_BYTES + 1];
	char output[2048];
	socklen_t length;
	size_t size;
	int socket_fd, success;

	size = fread(input, 1, sizeof(input), stdin);

	if (ferror(stdin) ||
		mh_rtc_offer_read(input, size, &offer, NULL) != MH_ERROR_NONE)
		return 1;

	identity = NULL;
	peer = NULL;
	association = NULL;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;

	if ((argc != 2 && (argc != 3 || strcmp(argv[2], "--data") != 0)) ||
		inet_pton(AF_INET, argv[1], &local.sin_addr) != 1)
		return 1;

	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (socket_fd < 0)
		return 1;

	if (socket_fd >= FD_SETSIZE) {
		close(socket_fd);
		return 1;
	}

	length = sizeof(local);
	success = 0;

	if (bind(socket_fd, (const struct sockaddr *)&local, sizeof(local)) !=
			0 ||
		getsockname(socket_fd, (struct sockaddr *)&local, &length) !=
			0 ||
		fcntl(socket_fd, F_SETFL, O_NONBLOCK) < 0 ||
		mh_dtls_identity_open(&identity, NULL) != MH_ERROR_NONE ||
		mh_dtls_peer_open(identity, 1, offer.fingerprint, &peer,
			NULL) != MH_ERROR_NONE)
		goto done;

	memset(&answer, 0, sizeof(answer));
	strcpy(answer.ufrag, "syntheticServer");
	strcpy(answer.password, "syntheticFixturePassword0123456789");
	memcpy(answer.fingerprint, mh_dtls_fingerprint(identity), 32);
	answer.address_size = 4;
	memcpy(answer.address, &local.sin_addr.s_addr, 4);
	answer.udp_port = ntohs(local.sin_port);
	answer.sctp_port = 5000;
	answer.session_id = 1;

	if (argc == 3) {
		if (mh_webrtc_random(random, sizeof(random), NULL) !=
			MH_ERROR_NONE)
			goto done;

		memset(&config, 0, sizeof(config));
		config.local_port = answer.sctp_port;
		config.remote_port = offer.sctp_port;
		config.tag = mh_rtc_read32(random) | 1;
		config.initial_tsn = mh_rtc_read32(random + 4);
		memcpy(config.cookie, random + 8, 32);

		if (mh_sctp_open(&config, &association, NULL) != MH_ERROR_NONE)
			goto done;
	}

	if (mh_rtc_answer_write(&offer, &answer, output, sizeof(output), &size,
		NULL) != MH_ERROR_NONE)
		goto done;

	if (fwrite(output, 1, size, stdout) != size ||
		puts("--READY--") == EOF || fflush(stdout) == EOF)
		goto done;

	success = run_peer(socket_fd, peer, &offer, &answer, association);

done:
	mh_sctp_close(association);
	mh_dtls_peer_close(peer);
	mh_dtls_identity_close(identity);
	close(socket_fd);

	return success ? 0 : 1;
}
