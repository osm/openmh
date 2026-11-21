#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/runtime_network.h"
#include "client/hosts/clock.h"
#include "client/hosts/network.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "interfaces/network/address.h"
#include "interfaces/network/network.h"

enum {
	POLL_WAIT_MS = 10
};

static void
print_label(const char *text)
{
	unsigned int i, byte;

	for (i = 0; i < MH_DISCOVERY_LABEL_BYTES && text[i] != 0; i++) {
		byte = (unsigned char)text[i];
		putchar(byte >= 32 && byte <= 126 ? (int)byte : '?');
	}
}

static void
print_catalog(const struct mh_discovery_catalog *catalog)
{
	const struct mh_discovery_entry *entry;
	unsigned long ip;
	unsigned int i;

	for (i = 0; i < catalog->count; i++) {
		entry = &catalog->entries[i];
		ip = entry->ipv4;
		printf("%lu.%lu.%lu.%lu:%u  ", (ip >> 24) & 255,
			(ip >> 16) & 255, (ip >> 8) & 255, ip & 255,
			entry->port);
		print_label(entry->game.name);
		printf("  %u/%u  ", entry->game.players, MH_NET_PLAYERS);
		print_label(entry->game.track);
		printf("  %s%s\n", entry->game.racing ? "racing" : "lobby",
			entry->game.password == MH_DISCOVERY_PASSWORD_UNKNOWN
				? "  password status unknown"
				: entry->game.password ? "  password required"
						: "");
	}

	printf("%u session%s found.\n", catalog->count,
		catalog->count == 1 ? "" : "s");
}

static void
usage(void)
{
	puts("Usage: mh-net-discover [--network laninet] [--host HOST] [--port PORT]");
	puts("LAN/INET requires --host; default UDP port 16000.");
}

int
main(int argc, char *argv[])
{
	struct mh_transport *query;
	struct mh_transport_event event;
	const struct mh_discovery_catalog *catalog;
	const char *host;
	enum mh_net_wire wire;
	struct mh_error error;
	enum mh_error_code status;
	char *port_end;
	unsigned long port;
	int arg, done;

	host = NULL;
	port = 0;
	wire = MH_NET_WIRE_31_LOBBY;
	query = NULL;

	for (arg = 1; arg < argc; arg++) {
		if (strcmp(argv[arg], "--help") == 0) {
			usage();
			return 0;
		}

		if (arg + 1 >= argc) {
			usage();
			return 2;
		}

		if (strcmp(argv[arg], "--host") == 0)
			host = argv[++arg];
		else if (strcmp(argv[arg], "--network") == 0 &&
			mh_net_wire_parse(argv[arg + 1], &wire))
			arg++;
		else if (strcmp(argv[arg], "--port") == 0) {
			arg++;
			errno = 0;
			port = strtoul(argv[arg], &port_end, 10);

			if (errno != 0 || argv[arg][0] < '0' ||
				argv[arg][0] > '9' || *port_end != 0 ||
				port == 0 || port > MH_U16_MAX) {
				usage();
				return 2;
			}
		} else {
			usage();
			return 2;
		}
	}

	if (port == 0)
		port = MH_NET_LANINET_PORT;

	status = mh_transport_open_address(
		mh_host_network(), 0, NULL, 0, &query, &error);

	if (status == MH_ERROR_NONE)
		status = mh_transport_search_address(
			query, host, (unsigned int)port, 1, &error);

	done = 0;

	while (status == MH_ERROR_NONE && !done) {
		status = mh_transport_search_poll(query, &done, &error);

		if (status == MH_ERROR_NONE && !done)
			status = mh_transport_poll(query,
				wire == MH_NET_WIRE_31_LOBBY ? 0 : POLL_WAIT_MS,
				&event, &error);

		if (wire == MH_NET_WIRE_31_LOBBY && !done &&
			status == MH_ERROR_NONE)
			mh_clock_delay(POLL_WAIT_MS);
	}

	if (status == MH_ERROR_NONE) {
		catalog = mh_transport_search_results(query);

		print_catalog(catalog);
	} else
		fprintf(stderr, "Discovery: %s\n", error.message);

	mh_transport_close(query);

	return status != MH_ERROR_NONE || ferror(stdout);
}
