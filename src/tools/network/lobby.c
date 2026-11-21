#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/options.h"
#include "client/diagnostics/runtime_network.h"
#include "client/hosts/clock.h"
#include "client/hosts/network.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/network_address.h"
#include "engine/network/network_session.h"

#define CHAT_COLOR_WHITE 0xffffffUL

enum {
	DEFAULT_SECONDS = 30,
	MAX_SECONDS = 86400,
	POLL_WAIT_MS = 10,
	LEAVE_DRAIN_MS = 250
};

struct lobby_options {
	struct mh_net_session_config	config;
	const char			*address, *chat, *rename;
	unsigned long			port, seconds;
	int				host;
};

static void
usage(void)
{
	fprintf(stderr,
		"Usage: mh-net-lobby (--host BIND | --join HOST) [--port N] [--seconds N]\n"
		"  [--network laninet] [--name NAME] [--password TEXT] [--chat TEXT] [--rename NAME]\n"
		"Diagnostic native lobby; no race yet. Default LAN/INET UDP port: 16000.\n");
	mh_debug_print_usage(stderr);
}

static int
read_number(const char *text, unsigned long limit, unsigned long *value)
{
	unsigned long parsed;
	const char *cursor;

	parsed = 0;

	if (*text == 0)
		return 0;

	for (cursor = text; *cursor != 0; cursor++) {
		if (*cursor < '0' || *cursor > '9' ||
			parsed > (limit - (unsigned long)(*cursor - '0')) / 10)
			return 0;

		parsed = parsed * 10 + (unsigned long)(*cursor - '0');
	}

	*value = parsed;

	return 1;
}

static void
print_quoted(const char *text)
{
	const unsigned char *cursor;

	putchar('"');

	for (cursor = (const unsigned char *)text; *cursor != 0; cursor++) {
		if (*cursor >= 32 && *cursor < 127 && *cursor != '"' &&
			*cursor != '\\')
			putchar(*cursor);
		else
			printf("\\x%02x", (unsigned int)*cursor);
	}

	putchar('"');
}

static int
read_options(int argc, char *argv[], struct lobby_options *options)
{
	int arg, port_set;

	port_set = 0;

	memset(&options->config, 0, sizeof(options->config));
	options->config.wire = MH_NET_WIRE_31_LOBBY;
	strcpy(options->config.track, "RedRock");
	options->config.laps = 3;
	options->config.car_count = 10;
	strcpy(options->config.profile.name, "Driver");
	strcpy(options->config.profile.car, "Kobra X3");
	strcpy(options->config.profile.horn, "Honker1");
	options->config.profile.update_rate = 15;
	options->config.profile.gear_mode = 1;
	options->address = options->chat = options->rename = NULL;
	options->port = 0;
	options->seconds = DEFAULT_SECONDS;
	options->host = -1;

	for (arg = 1; arg < argc; arg += 2) {
		if (arg + 1 >= argc) {
			return 0;
		}

		if (strcmp(argv[arg], "--host") == 0 ||
			strcmp(argv[arg], "--join") == 0) {
			if (options->host != -1) {
				return 0;
			}

			options->host = strcmp(argv[arg], "--host") == 0;
			options->address = argv[arg + 1];
		} else if (strcmp(argv[arg], "--network") == 0) {
			if (!mh_net_wire_parse(
				argv[arg + 1], &options->config.wire))
				return 0;
		} else if (strcmp(argv[arg], "--port") == 0) {
			port_set = 1;

			if (!read_number(argv[arg + 1], MH_U16_MAX,
				&options->port)) {
				return 0;
			}
		} else if (strcmp(argv[arg], "--seconds") == 0) {
			if (!read_number(argv[arg + 1], MAX_SECONDS,
				&options->seconds) ||
				options->seconds == 0) {
				return 0;
			}
		} else if (strcmp(argv[arg], "--name") == 0 ||
			strcmp(argv[arg], "--password") == 0) {
			if (strlen(argv[arg + 1]) >=
				(strcmp(argv[arg], "--name") == 0
						? MH_NET_NAME_BYTES
						: MH_NET_PASSWORD_BYTES)) {
				return 0;
			}

			strcpy(strcmp(argv[arg], "--name") == 0
					? options->config.profile.name
					: options->config.password,
				argv[arg + 1]);
		} else if (strcmp(argv[arg], "--chat") == 0) {
			options->chat = argv[arg + 1];

			if (strlen(options->chat) >= MH_NET_CHAT_BYTES) {
				return 0;
			}
		} else if (strcmp(argv[arg], "--rename") == 0) {
			options->rename = argv[arg + 1];

			if (strlen(options->rename) >= MH_NET_NAME_BYTES) {
				return 0;
			}
		} else {
			return 0;
		}
	}

	options->config.provider = options->config.wire == MH_NET_WIRE_31_LOBBY
		? MH_NET_PROVIDER_LANINET
		: MH_NET_PROVIDER_IP;

	if (!port_set)
		options->port = MH_NET_LANINET_PORT;

	if (options->host == -1 || (!options->host && options->port == 0)) {
		return 0;
	}

	return 1;
}

static void
print_roster(const struct mh_net_session *session)
{
	unsigned int i;

	printf("ROSTER revision=%lu\n", session->roster_revision);

	for (i = 0; i < session->roster.player_count; i++) {
		if (!(session->roster.players[i].flags & MH_NET_FLAG_LOBBY))
			continue;

		printf("PLAYER slot=%u name=", i);
		print_quoted(session->roster.players[i].name);
		putchar('\n');
	}
}

static void
print_chat(
	const struct mh_net_session *session, unsigned long previous_revision)
{
	unsigned int i, start;

	if (session->chat_revision - previous_revision < session->chat_count)
		start = session->chat_count -
			(unsigned int)(session->chat_revision -
				previous_revision);
	else
		start = 0;

	for (i = start; i < session->chat_count; i++) {
		printf("CHAT slot=%u text=", session->chat[i].player);
		print_quoted(session->chat[i].text);
		putchar('\n');
	}
}

static enum mh_error_code
send_actions(struct mh_net_session *session, const char *rename,
	const char *chat, struct mh_error *error_out)
{
	struct mh_net_profile profile;
	enum mh_error_code status;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (rename != NULL) {
		profile = session->config.profile;
		strcpy(profile.name, rename);
		status = mh_net_session_profile(session, &profile, error_out);
	}

	if (status == MH_ERROR_NONE && chat != NULL)
		status = mh_net_session_chat(
			session, chat, CHAT_COLOR_WHITE, error_out);

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_debug_options debug;
	struct lobby_options options;
	struct mh_network *network;
	struct mh_net_session *session;
	struct mh_network_issue issue;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long roster_revision, chat_revision;
	enum mh_net_session_phase phase;
	double deadline_ms, now_ms;
	int actions_sent, leaving, result;

	if (mh_debug_parse_arguments(&debug, &argc, argv, NULL) !=
		MH_ERROR_NONE) {
		fprintf(stderr, "Invalid diagnostic option or value.\n");
		return 1;
	}

	if (!read_options(argc, argv, &options)) {
		usage();
		return 2;
	}

	status = mh_network_open_address(options.config.simulation_rate == 0
			? mh_host_legacy_network()
			: mh_host_network(),
		options.host, options.host ? options.address : NULL,
		options.host ? NULL : options.address,
		(unsigned int)options.port, &options.config, &network, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	mh_network_set_trace(network, debug.network_events);
	session = mh_network_session(network);
	phase = MH_NET_CONNECTING;
	roster_revision = chat_revision = 0;
	actions_sent = leaving = result = 0;
	deadline_ms = mh_network_now(network) + options.seconds * 1000.0;
	printf("%s port=%u\n", options.host ? "HOST" : "CLIENT",
		mh_network_port(network));
	fflush(stdout);

	while (mh_network_now(network) < deadline_ms) {
		status = mh_network_step(network,
			options.config.wire == MH_NET_WIRE_31_LOBBY
				? 0
				: POLL_WAIT_MS,
			&issue, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			result = 1;
			break;
		}

		if (issue.reason.code != MH_ERROR_NONE)
			fprintf(stderr, "peer %lu: %s\n", issue.peer,
				issue.reason.message);

		if (session->phase != phase) {
			phase = session->phase;
			printf("PHASE %u slot=%u\n", (unsigned int)phase,
				session->player);

			if (phase == MH_NET_REJECTED) {
				printf("REJECT reason=%u text=",
					session->rejection_reason);
				print_quoted(session->rejection);
				putchar('\n');
				result = 3;
				break;
			}

			if (phase == MH_NET_CLOSED && !leaving) {
				result = 4;
				break;
			}
		}

		if (session->roster_revision != roster_revision) {
			roster_revision = session->roster_revision;
			print_roster(session);
		}

		if (session->chat_revision != chat_revision) {
			print_chat(session, chat_revision);

			chat_revision = session->chat_revision;
		}

		if (!actions_sent && session->phase == MH_NET_LOBBY) {
			status = send_actions(
				session, options.rename, options.chat, &error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "%s\n", error.message);
				result = 1;
				break;
			}

			actions_sent = 1;
		}

		now_ms = mh_network_now(network);

		if (!leaving && now_ms >= deadline_ms - LEAVE_DRAIN_MS &&
			session->phase == MH_NET_LOBBY) {
			status = mh_net_session_leave(session, &error);
			leaving = 1;

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "%s\n", error.message);
				result = 1;
				break;
			}
		}

		fflush(stdout);

		if (options.config.wire == MH_NET_WIRE_31_LOBBY)
			mh_clock_delay(POLL_WAIT_MS);
	}

	if (!actions_sent && result == 0) {
		fprintf(stderr, "lobby admission timed out\n");
		result = 5;
	}

	mh_network_close(network);

	return result;
}
