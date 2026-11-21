#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/browser_internal.h"
#include "backends/network/reliable_delivery.h"
#include "engine/core/text.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "browser transport:%d: %s\n",          \
				__LINE__, #condition);                         \
			exit(1);                                               \
		}                                                              \
	} while (0)

static int phase, allocated, unavailable, send_result, sends, ice_count;
static unsigned long ticks;
static unsigned char sent[MH_RELIABLE_DATAGRAM_BYTES];
static unsigned char incoming[MH_RELIABLE_DATAGRAM_BYTES];
static size_t sent_size, incoming_size;
static char endpoint[MH_BROWSER_SIGNAL_URL_BYTES];

int
mh_browser_net_open(void)
{
	if (unavailable)
		return 0;

	CHECK(!allocated);
	allocated = 1;
	phase = BROWSER_IDLE;
	send_result = 1;
	ice_count = 0;

	return 1;
}

void
mh_browser_net_close(int handle)
{
	CHECK(handle == 1 && allocated);
	allocated = 0;
}

void
mh_browser_net_ice(int handle, const char *url, const char *username,
	const char *credential)
{
	CHECK(handle == 1 && allocated && phase == BROWSER_IDLE);

	CHECK(strcmp(url, "turn:relay.invalid") == 0);
	CHECK(strcmp(username, "user") == 0 &&
		strcmp(credential, "secret") == 0);
	ice_count++;
}

void
mh_browser_net_connect(int handle, const char *url, int relay_only)
{
	CHECK(handle == 1 && allocated && phase == BROWSER_IDLE && relay_only);
	CHECK(mh_text_copy(endpoint, sizeof(endpoint), url, NULL) ==
		MH_ERROR_NONE);
	phase = BROWSER_CONNECTING;
}

int
mh_browser_net_state(int handle)
{
	CHECK(handle == 1 && allocated);

	return phase;
}

int
mh_browser_net_send(int handle, const unsigned char *data, size_t size)
{
	CHECK(handle == 1 && allocated && size <= sizeof(sent));

	if (send_result == 1) {
		memcpy(sent, data, size);
		sent_size = size;
		sends++;
	}

	return send_result;
}

size_t
mh_browser_net_receive(int handle, unsigned char *data, size_t capacity)
{
	size_t size;

	CHECK(handle == 1 && allocated && incoming_size <= capacity);
	size = incoming_size;
	memcpy(data, incoming, size);
	incoming_size = 0;

	return size;
}

size_t
mh_browser_net_reason(int handle, unsigned char *data, size_t capacity)
{
	static const char reason[] = "Synthetic close";

	CHECK(handle == 1 && allocated && sizeof(reason) <= capacity);
	memcpy(data, reason, sizeof(reason));

	return sizeof(reason);
}

void
mh_browser_net_disconnect(int handle)
{
	CHECK(handle == 1 && allocated);
	phase = BROWSER_CLOSED;
}

double
mh_browser_net_now(void)
{
	return ticks;
}

unsigned long
mh_browser_net_ticks(void)
{
	return ticks;
}

static void
check_event(struct mh_transport *transport, enum mh_transport_event_type type,
	struct mh_transport_event *event)
{
	CHECK(mh_transport_poll(transport, 0, event, NULL) == MH_ERROR_NONE);

	CHECK(event->type == type);

	if (type != MH_TRANSPORT_NONE)
		CHECK(event->peer == BROWSER_SERVER_PEER);
}

int
main(void)
{
	struct mh_browser_network_config config;
	struct mh_browser_ice_server ice;
	struct mh_transport_target *target, *unchanged;
	struct mh_transport *transport;
	struct mh_transport_event event;
	unsigned char data[MH_RELIABLE_DATAGRAM_BYTES - 1];
	char url[] = "https://game.invalid/";
	char oversized[MH_BROWSER_SIGNAL_URL_BYTES + 1];
	unsigned int count;

	memset(&config, 0, sizeof(config));
	config.signaling_url = url;
	ice.url = "turn:relay.invalid";
	ice.username = "user";
	ice.credential = "secret";
	config.ice_servers = &ice;
	config.ice_server_count = 1;
	config.relay_only = 1;
	CHECK(mh_browser_network_target(&config, &target, NULL) ==
		MH_ERROR_NONE);
	unchanged = target;
	memset(oversized, 'a', sizeof(oversized));
	oversized[sizeof(oversized) - 1] = 0;
	config.signaling_url = oversized;
	CHECK(mh_browser_network_target(&config, &unchanged, NULL) ==
		MH_ERROR_RANGE);
	CHECK(unchanged == target);
	config.ice_server_count = MH_BROWSER_ICE_SERVERS + 1;
	CHECK(mh_browser_network_target(&config, &unchanged, NULL) ==
		MH_ERROR_INVALID);
	CHECK(unchanged == target);
	memset(url, '?', sizeof(url) - 1);
	unavailable = 1;
	transport = NULL;
	CHECK(mh_transport_open_from(mh_browser_network_provider(), 0, NULL,
		&transport, NULL) == MH_ERROR_MEMORY &&
		transport == NULL);
	unavailable = 0;
	CHECK(mh_transport_open_from(mh_browser_network_provider(), 1, NULL,
		&transport, NULL) == MH_ERROR_UNSUPPORTED &&
		transport == NULL);
	ticks = 100;
	CHECK(mh_transport_open_from(mh_browser_network_provider(), 0, NULL,
		&transport, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_connect(transport, target, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_connect(transport, target, NULL) == MH_ERROR_STATE);
	CHECK(ice_count == 1 && strcmp(endpoint, "https://game.invalid/") == 0);
	mh_transport_target_close(target);
	check_event(transport, MH_TRANSPORT_NONE, &event);
	data[0] = 42;
	CHECK(mh_transport_send(transport, 1, data, 1, 0, NULL) ==
		MH_ERROR_STATE);
	phase = BROWSER_CONNECTED;
	check_event(transport, MH_TRANSPORT_CONNECT, &event);
	check_event(transport, MH_TRANSPORT_NONE, &event);
	ticks = 200;
	CHECK(mh_transport_now(transport) == 100);
	CHECK(mh_transport_send(transport, 1, data, 1, 0, NULL) ==
		MH_ERROR_NONE);
	data[0] = 99;
	CHECK(sent_size == 2 && sent[0] == 0 && sent[1] == 42);
	send_result = 0;
	CHECK(mh_transport_send(transport, 1, data, 1, 0, NULL) ==
		MH_ERROR_BACKPRESSURE);
	send_result = 1;
	CHECK(mh_transport_send(transport, 1, data, 1, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(sent_size == 2 && sent[0] == 2 && sent[1] == 99);
	count = (unsigned int)sends;
	ticks += 501;
	CHECK(mh_transport_flush(transport, NULL) == MH_ERROR_NONE);
	CHECK(sends == (int)count + 1);
	incoming[0] = 2;
	incoming_size = 1;
	check_event(transport, MH_TRANSPORT_NONE, &event);
	incoming[0] = 9;
	incoming[1] = 37;
	incoming_size = 2;
	check_event(transport, MH_TRANSPORT_RECEIVE, &event);
	CHECK(event.size == 1 && event.data[0] == 37 && event.reliable);
	CHECK(sent_size == 1 && sent[0] == 9);
	incoming_size = 2;
	check_event(transport, MH_TRANSPORT_NONE, &event);

	/*
	 * A blocked channel cannot grow the game reliable queue indefinitely.
	 */
	send_result = 0;
	memset(data, 0xab, sizeof(data));

	for (count = 0; count < 100; count++)
		if (mh_transport_send(transport, 1, data, sizeof(data), 1,
			NULL) != MH_ERROR_NONE)
			break;

	CHECK(count > 0 && count < 100);
	CHECK(mh_transport_disconnect(transport, 1, NULL) == MH_ERROR_NONE);
	check_event(transport, MH_TRANSPORT_DISCONNECT, &event);
	CHECK(event.size == sizeof("Synthetic close") &&
		strcmp((const char *)event.data, "Synthetic close") == 0);
	check_event(transport, MH_TRANSPORT_NONE, &event);
	mh_transport_close(transport);
	CHECK(!allocated);

	/*
	 * Failed asynchronous connections also wake a pending client session.
	 */
	config.signaling_url = "https://game.invalid/";
	config.ice_server_count = 0;
	CHECK(mh_browser_network_target(&config, &target, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_transport_open_from(mh_browser_network_provider(), 0, NULL,
		&transport, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_connect(transport, target, NULL) == MH_ERROR_NONE);
	mh_transport_target_close(target);
	phase = BROWSER_CLOSED;
	check_event(transport, MH_TRANSPORT_DISCONNECT, &event);
	check_event(transport, MH_TRANSPORT_NONE, &event);
	mh_transport_close(transport);

	return 0;
}
