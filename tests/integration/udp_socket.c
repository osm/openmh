#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/wait.h>

#include <unistd.h>
#endif

#include "backends/clock/system_provider.h"
#include "backends/network/udp_provider.h"
#include "backends/network/udp_socket.h"
#include "engine/core/limits.h"
#include "interfaces/clock/clock.h"

#define LOOPBACK_ADDRESS 0x7f000001UL
#define RECEIVE_TIMEOUT_MS 3000
#define WAIT_TIMEOUT_MS 1000
#define WAKEUP_LIMIT_MS 750
#define SEND_DELAY_MS 30
#define IDLE_TIMEOUT_MS 20
#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "socket line %d: %s\n", __LINE__,      \
				#condition);                                   \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
receive_packet(struct mh_udp_socket *socket,
	const struct mh_udp_address *sender, const unsigned char *expected,
	size_t expected_size, size_t capacity)
{
	const struct mh_clock_source *clock;
	struct mh_udp_address source;
	unsigned char data[64];
	unsigned long started;
	size_t size;
	int received;

	clock = mh_system_clock_source();
	started = mh_clock_source_ticks(clock);

	do {
		CHECK(mh_udp_socket_receive(socket, &source, data, capacity,
			&size, &received, NULL) == MH_ERROR_NONE);

		if (received)
			break;

		CHECK(((mh_clock_source_ticks(clock) - started) & MH_U32_MAX) <
			RECEIVE_TIMEOUT_MS);
		CHECK(mh_clock_source_delay(clock, 1, NULL) == MH_ERROR_NONE);
	} while (!received);

	CHECK(mh_udp_address_same(&source, sender));
	CHECK(size == expected_size);
	CHECK(memcmp(data, expected, size) == 0);

	return 1;
}

static int
exchange_packets(struct mh_udp_socket *sender,
	const struct mh_udp_address *sender_address,
	struct mh_udp_socket *receiver,
	const struct mh_udp_address *receiver_address)
{
	unsigned char data[128];
	struct mh_udp_address source;
	size_t size;
	int received;
	unsigned int i;

	for (i = 0; i < sizeof(data); i++)
		data[i] = (unsigned char)i;

	CHECK(mh_udp_socket_receive(receiver, &source, data, sizeof(data),
		&size, &received, NULL) == MH_ERROR_NONE);
	CHECK(!received && size == 0);
	CHECK(mh_udp_socket_send(sender, receiver_address, data, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(receive_packet(receiver, sender_address, data, 0, 64));
	CHECK(mh_udp_socket_send(sender, receiver_address, data, sizeof(data),
		NULL) == MH_ERROR_NONE);
	CHECK(receive_packet(receiver, sender_address, data, 64, 64));

	/* Truncation consumes one datagram; the next packet remains intact. */
	CHECK(mh_udp_socket_send(sender, receiver_address, data, 17, NULL) ==
		MH_ERROR_NONE);
	CHECK(receive_packet(receiver, sender_address, data, 17, 64));

	return 1;
}

static int
check_timeout(const struct mh_udp_wait *wait)
{
	unsigned long started, elapsed;

	started = mh_clock_source_ticks(mh_system_clock_source());
	CHECK(mh_udp_wait_readable(wait, IDLE_TIMEOUT_MS, NULL) ==
		MH_ERROR_NONE);
	elapsed = (mh_clock_source_ticks(mh_system_clock_source()) - started) &
		MH_U32_MAX;

	/* Tick sampling may straddle either end of the timeout by one ms. */
	CHECK(elapsed + 1 >= IDLE_TIMEOUT_MS);
	CHECK(mh_udp_wait_readable(wait, MH_UDP_WAIT_LIMIT_MS + 1, NULL) ==
		MH_ERROR_INVALID);

	return 1;
}

static int
check_wakeup(const struct mh_udp_wait *wait, struct mh_udp_socket *sender,
	const struct mh_udp_address *sender_address,
	struct mh_udp_socket *receiver,
	const struct mh_udp_address *receiver_address)
{
	unsigned long started, elapsed;
	const unsigned char data[] = "wake";

	CHECK(mh_udp_socket_send(sender, receiver_address, data, sizeof(data),
		NULL) == MH_ERROR_NONE);
	started = mh_clock_source_ticks(mh_system_clock_source());
	CHECK(mh_udp_wait_readable(wait, WAIT_TIMEOUT_MS, NULL) ==
		MH_ERROR_NONE);
	elapsed = (mh_clock_source_ticks(mh_system_clock_source()) - started) &
		MH_U32_MAX;
	CHECK(elapsed < WAKEUP_LIMIT_MS);

	/* Waiting must not consume the datagram. */
	CHECK(receive_packet(receiver, sender_address, data, sizeof(data), 64));
	CHECK(check_timeout(wait));

	return 1;
}

#ifndef _WIN32
static int
delayed_wakeup(const struct mh_udp_wait *wait, struct mh_udp_socket *sender,
	const struct mh_udp_address *sender_address,
	struct mh_udp_socket *receiver,
	const struct mh_udp_address *receiver_address)
{
	const unsigned char data[] = "later";
	pid_t child, joined;
	unsigned long started, elapsed;
	int child_status;
	enum mh_error_code status;

	child = fork();
	CHECK(child >= 0);

	if (child == 0) {
		status = mh_clock_source_delay(
			mh_system_clock_source(), SEND_DELAY_MS, NULL);

		if (status == MH_ERROR_NONE)
			status = mh_udp_socket_send(sender, receiver_address,
				data, sizeof(data), NULL);

		_exit(status == MH_ERROR_NONE ? 0 : 1);
	}

	started = mh_clock_source_ticks(mh_system_clock_source());
	status = mh_udp_wait_readable(wait, WAIT_TIMEOUT_MS, NULL);
	elapsed = (mh_clock_source_ticks(mh_system_clock_source()) - started) &
		MH_U32_MAX;
	joined = waitpid(child, &child_status, 0);
	CHECK(joined == child && WIFEXITED(child_status) &&
		WEXITSTATUS(child_status) == 0);
	CHECK(status == MH_ERROR_NONE && elapsed < WAKEUP_LIMIT_MS);
	CHECK(receive_packet(receiver, sender_address, data, sizeof(data), 64));

	return 1;
}
#endif

static int
check_capacity(void)
{
	struct mh_udp_wait wait;
	struct mh_udp_socket *sockets[MH_UDP_WAIT_SOCKETS], *unchanged;
	struct mh_udp_address local;
	size_t opened, i;
	int success;
	enum mh_error_code status;

	memset(&wait, 0, sizeof(wait));
	memset(sockets, 0, sizeof(sockets));
	success = 0;

	for (opened = 0; opened < MH_UDP_WAIT_SOCKETS; opened++) {
		local.ipv4 = LOOPBACK_ADDRESS;
		local.port = 0;

		if (mh_udp_socket_open(&local, &wait, &sockets[opened], NULL) !=
			MH_ERROR_NONE)
			break;
	}

	if (opened == MH_UDP_WAIT_SOCKETS) {
		local.port = 0;
		unchanged = sockets[0];
		status = mh_udp_socket_open(&local, &wait, &unchanged, NULL);
		success = status == MH_ERROR_RANGE && unchanged == sockets[0] &&
			local.port == 0 && wait.count == MH_UDP_WAIT_SOCKETS;

		/*
		 * A rejected registration must leave existing sockets usable.
		 */
		mh_udp_socket_close(sockets[0]);
		sockets[0] = NULL;
		status = mh_udp_socket_open(&local, &wait, &sockets[0], NULL);
		success = success && status == MH_ERROR_NONE &&
			check_wakeup(
				&wait, sockets[0], &local, sockets[0], &local);
	}

	for (i = 0; i < opened; i++)
		mh_udp_socket_close(sockets[i]);

	CHECK(success && wait.count == 0);

	return 1;
}

static int
exercise_sockets(struct mh_udp_wait *wait, struct mh_udp_socket **first,
	struct mh_udp_socket **second)
{
	struct mh_udp_address a, b, duplicate;
	struct mh_udp_socket *unchanged;
	struct mh_transport_address text;

	a.ipv4 = b.ipv4 = LOOPBACK_ADDRESS;
	a.port = b.port = 0;
	CHECK(mh_udp_socket_open(&a, wait, first, NULL) == MH_ERROR_NONE);
	CHECK(mh_udp_socket_open(&b, wait, second, NULL) == MH_ERROR_NONE);
	CHECK(a.ipv4 == LOOPBACK_ADDRESS && b.ipv4 == LOOPBACK_ADDRESS);
	CHECK(a.port != 0 && b.port != 0 && a.port != b.port);

	duplicate = a;
	unchanged = *second;
	CHECK(mh_udp_socket_open(&duplicate, wait, &unchanged, NULL) !=
		MH_ERROR_NONE);
	CHECK(unchanged == *second && mh_udp_address_same(&a, &duplicate));
	mh_udp_address_export(&a, &text);
	CHECK(strcmp(text.host, "127.0.0.1") == 0 && text.port == a.port);
	CHECK(check_wakeup(wait, *first, &a, *second, &b));
	CHECK(check_wakeup(wait, *second, &b, *first, &a));
#ifndef _WIN32
	CHECK(delayed_wakeup(wait, *first, &a, *second, &b));
#endif
	CHECK(exchange_packets(*first, &a, *second, &b));
	CHECK(exchange_packets(*second, &b, *first, &a));

	/* Releasing one socket must not shut down the surviving socket. */
	mh_udp_socket_close(*first);
	*first = NULL;
	CHECK(check_wakeup(wait, *second, &b, *second, &b));
	CHECK(exchange_packets(*second, &b, *second, &b));

	return 1;
}

int
main(void)
{
	struct mh_udp_socket *first, *second;
	struct mh_udp_wait wait;
	int success;

	memset(&wait, 0, sizeof(wait));
	first = second = NULL;
	success = check_timeout(&wait) &&
		exercise_sockets(&wait, &first, &second);
	mh_udp_socket_close(first);
	mh_udp_socket_close(second);

	if (!success || !check_timeout(&wait) || !check_capacity())
		return 1;

	puts("Native UDP sockets: exclusive bind, endpoint identity, empty/truncated datagrams and independent lifetime and traffic wakeups passed");

	return 0;
}
