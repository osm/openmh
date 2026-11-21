#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/signaling_provider.h"
#include "backends/network/signaling_socket.h"
#include "backends/network/webrtc_signaling.h"
#include "engine/core/text.h"
#include "interfaces/clock/backend.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "signaling server:%d: %s\n", __LINE__, \
				#condition);                                   \
			exit(1);                                               \
		}                                                              \
	} while (0)

struct mh_signal_socket {
	int	pending, closed, fail_write;
	size_t	received, sent, write_limit;
	char	input[2048], output[MH_SIGNAL_RESPONSE_BYTES + 1];
};

static struct mh_signal_socket listener, client;
static unsigned long ticks, offered, canceled;
static int reject_offer;
static char expected_address[MH_TRANSPORT_ADDRESS_BYTES];

enum mh_error_code
mh_signal_socket_listen(unsigned long ipv4, unsigned int port,
	struct mh_signal_socket **output, unsigned int *bound_port,
	struct mh_error *error_out)
{
	(void)error_out;

	CHECK(ipv4 == 0x7f000001UL && port == 0);
	memset(&listener, 0, sizeof(listener));
	*output = &listener;
	*bound_port = 12345;

	return MH_ERROR_NONE;
}

enum mh_error_code
mh_signal_socket_accept(struct mh_signal_socket *socket,
	struct mh_signal_socket **output, struct mh_transport_address *address,
	unsigned long *ipv4, int *accepted, struct mh_error *error_out)
{
	(void)error_out;

	CHECK(socket == &listener && !listener.closed);
	*accepted = client.pending;

	if (client.pending) {
		client.pending = 0;
		*output = &client;
		memset(address, 0, sizeof(*address));
		strcpy(address->host, "127.0.0.1");
		address->port = 9876;
		*ipv4 = 0x7f000001UL;
	}

	return MH_ERROR_NONE;
}

enum mh_error_code
mh_signal_socket_read(struct mh_signal_socket *socket, unsigned char *data,
	size_t capacity, size_t *size, int *eof, struct mh_error *error_out)
{
	(void)error_out;

	CHECK(socket == &client && !client.closed && capacity > 0);
	*eof = 0;
	*size = 0;

	if (client.input[client.received] != 0) {
		*data = (unsigned char)client.input[client.received++];
		*size = 1;
	}

	return MH_ERROR_NONE;
}

enum mh_error_code
mh_signal_socket_write(struct mh_signal_socket *socket,
	const unsigned char *data, size_t size, size_t *written,
	struct mh_error *error_out)
{
	CHECK(socket == &client && !client.closed);

	if (client.fail_write)
		return mh_error_set(
			error_out, MH_ERROR_IO, "synthetic write failure");

	if (size > client.write_limit)
		size = client.write_limit;

	CHECK(size < sizeof(client.output) - client.sent);
	memcpy(client.output + client.sent, data, size);
	client.sent += size;
	client.output[client.sent] = 0;
	*written = size;

	return MH_ERROR_NONE;
}

void
mh_signal_socket_close(struct mh_signal_socket *socket)
{
	if (socket != NULL) {
		CHECK(!socket->closed);
		socket->closed = 1;
	}
}

int
mh_signal_address_normalize(const char *text, char *output, size_t capacity)
{
	if (strcmp(text, "2001:db8::1234") != 0)
		return 0;

	return mh_text_copy(output, capacity, text, NULL) == MH_ERROR_NONE;
}

static unsigned long
clock_ticks(void *context)
{
	(void)context;

	return ticks;
}

static enum mh_error_code
offer(void *context, const unsigned char *data, size_t size,
	const struct mh_transport_address *address, char *answer,
	size_t capacity, size_t *written, unsigned long *peer,
	struct mh_error *error_out)
{
	(void)context;

	CHECK(size == 5 && memcmp(data, "offer", size) == 0);
	CHECK(strcmp(address->host, expected_address) == 0);

	if (reject_offer)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "synthetic peer limit");

	CHECK(mh_text_copy(answer, capacity, "answer", NULL) == MH_ERROR_NONE);
	*written = 6;
	*peer = ++offered;

	return MH_ERROR_NONE;
}

static void
cancel(void *context, unsigned long peer)
{
	(void)context;
	CHECK(peer > 0 && peer <= offered);
	canceled++;
}

static void
request(const char *origin, const char *proxy)
{
	memset(&client, 0, sizeof(client));
	client.pending = 1;
	client.write_limit = 3;
	sprintf(client.input,
		"POST / HTTP/1.1\r\nHost: test.invalid\r\nOrigin: %s\r\n"
		"Content-Type: application/sdp\r\nContent-Length: 5\r\n%s\r\noffer",
		origin, proxy);
}

static void
pump(struct mh_signaling_server *server)
{
	unsigned int i;

	for (i = 0; i < 1000 && !client.closed; i++)
		CHECK(mh_signaling_poll(server, NULL) == MH_ERROR_NONE);
}

int
main(void)
{
	static const struct mh_clock_ops ops = {
		NULL, NULL, clock_ticks, NULL, NULL};

	static const struct mh_clock_source clock = {&ops, NULL};
	struct mh_signaling_config config;
	struct mh_signaling_server *server;
	unsigned long count;

	memset(&config, 0, sizeof(config));
	config.clock = &clock;
	config.bind_ipv4 = 0x7f000001UL;
	config.origin = "https://game.invalid";
	config.offer = offer;
	config.cancel = cancel;
	CHECK(mh_signaling_open(&config, &server, NULL) == MH_ERROR_NONE);
	CHECK(mh_signaling_port(server) == 12345);
	strcpy(expected_address, "127.0.0.1");
	request(config.origin, "X-Real-IP: 2001:db8::1234\r\n");
	pump(server);
	CHECK(client.closed && offered == 1 && canceled == 0);
	CHECK(strstr(client.output, "200 OK\r\n") != NULL &&
		strstr(client.output, "\r\n\r\nanswer") != NULL);
	request("https://other.invalid", "");
	pump(server);
	CHECK(client.closed && offered == 1 && canceled == 0);
	CHECK(strstr(client.output, "403 Forbidden\r\n") != NULL &&
		strstr(client.output, "Access-Control-Allow-Origin") == NULL);
	reject_offer = 1;
	request(config.origin, "");
	pump(server);
	CHECK(client.closed && offered == 1 && canceled == 0);
	CHECK(strstr(client.output, "503 Service Unavailable\r\n") != NULL);
	reject_offer = 0;
	request(config.origin, "");
	client.fail_write = 1;
	pump(server);
	CHECK(client.closed && offered == 2 && canceled == 1);

	/* A blocked response expires across the unsigned clock wrap. */
	ticks = 0xfffffff0UL;
	request(config.origin, "");
	client.write_limit = 0;
	pump(server);
	CHECK(!client.closed && offered == 3 && canceled == 1);
	ticks = 4984;
	CHECK(mh_signaling_poll(server, NULL) == MH_ERROR_NONE);
	CHECK(client.closed && canceled == 2);
	mh_signaling_close(server);
	CHECK(listener.closed);
	config.trust_proxy = 1;
	config.proxy_ipv4 = 0x7f000001UL;
	CHECK(mh_signaling_open(&config, &server, NULL) == MH_ERROR_NONE);
	request(config.origin, "");
	pump(server);
	CHECK(client.closed && offered == 3 && canceled == 2);
	CHECK(strstr(client.output, "400 Bad Request\r\n") != NULL);
	strcpy(expected_address, "2001:db8::1234");
	request(config.origin, "X-Real-IP: 2001:db8::1234\r\n");
	pump(server);
	CHECK(client.closed && offered == 4 && canceled == 2);
	request(config.origin, "X-Real-IP: 2001:db8::1234\r\n");
	client.write_limit = 0;
	pump(server);
	CHECK(!client.closed && offered == 5 && canceled == 2);
	mh_signaling_close(server);
	CHECK(client.closed && listener.closed && canceled == 3);

	/* Incomplete HTTP requests never allocate a transport peer. */
	CHECK(mh_signaling_open(&config, &server, NULL) == MH_ERROR_NONE);
	request(config.origin, "");
	client.input[10] = 0;
	count = offered;
	pump(server);
	CHECK(!client.closed && offered == count);
	ticks += 5000;
	CHECK(mh_signaling_poll(server, NULL) == MH_ERROR_NONE);
	CHECK(client.closed && offered == count && canceled == 3);
	mh_signaling_close(server);

	return 0;
}
