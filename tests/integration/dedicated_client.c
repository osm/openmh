#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/stdin_ready.h"
#include "backends/clock/system_provider.h"
#include "backends/network/posix_provider.h"
#include "backends/network/udp_transport_provider.h"
#include "backends/network/windows_provider.h"
#include "engine/core/limits.h"
#include "engine/network/network_address.h"
#include "engine/network/network_session.h"
#include "engine/ui/lobby_screen.h"

/* A separate process using the production transport/session and lobby view. */
static enum mh_error_code
report_lobby(struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_lobby_roster roster;
	struct mh_lobby_roster_view view;
	unsigned int advance[256], i, drivers, ready, passing, names;
	unsigned char palette[768];
	enum mh_error_code status;

	if (session->roster.player_count != MH_NET_WIRE_PLAYERS ||
		session->roster.players[0].flags & MH_NET_FLAG_PRESENT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"dedicated server must occupy a hidden slot");

	status = mh_lobby_session_roster(
		session, "Dedicated", 0, &roster, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(palette, 0, sizeof(palette));

	for (i = 0; i < 256; i++)
		advance[i] = 6;

	status = mh_lobby_screen_palette(&roster, palette, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_lobby_roster_view(
			&roster, advance, palette, &view, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	drivers = ready = passing = names = 0;

	for (i = 0; i < session->roster.player_count; i++) {
		if (roster.players[i].lobby)
			drivers++;

		if (session->roster.players[i].flags & MH_NET_FLAG_READY)
			ready |= 1U << i;

		if (session->roster.players[i].flags & MH_NET_FLAG_PASS)
			passing |= 1U << i;
	}

	for (i = 0; i < view.count; i++)
		if (view.lines[i].x == 75)
			names++;

	if (names != drivers || ready != session->ready_mask)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"dedicated lobby view or ready mask lost a player");

	printf("STATE %u %u %u %u %lu %u\n", session->player, drivers, ready,
		passing, session->replication.status_revision,
		session->roster.players[MH_NET_WIRE_PLAYERS - 1].update_rate);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_command(struct mh_net_session *session,
	struct mh_lobby_command_state *console, struct mh_error *error_out)
{
	struct mh_lobby_command_catalog catalog;
	struct mh_lobby_command_output output;
	char text[256];
	int count;

	count = mh_test_stdin_ready();

	if (count < 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "stdin select failed");

	if (count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (fgets(text, sizeof(text), stdin) == NULL)
		return mh_net_session_leave(session, error_out);

	text[strcspn(text, "\r\n")] = 0;
	memset(&catalog, 0, sizeof(catalog));

	return mh_net_session_command(
		session, console, &catalog, text, &output, error_out);
}

static enum mh_error_code
run(struct mh_network *network, struct mh_error *error_out)
{
	struct mh_net_session *session;
	struct mh_network_issue issue;
	struct mh_lobby_command_state console;
	unsigned long roster_revision, status_revision, chat_revision;
	enum mh_error_code status;

	memset(&console, 0, sizeof(console));
	roster_revision = status_revision = chat_revision = 0;
	session = mh_network_session(network);

	while (session->phase != MH_NET_CLOSED) {
		status = mh_network_step(network, 0, &issue, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (issue.reason.code != MH_ERROR_NONE)
			return mh_error_forward(error_out, &issue.reason);

		status = mh_clock_source_delay(
			mh_system_clock_source(), 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (session->phase == MH_NET_REJECTED)
			return mh_error_set(
				error_out, MH_ERROR_FAILED, session->rejection);

		if (chat_revision != session->chat_revision) {
			chat_revision = session->chat_revision;
			printf("CHAT %s\n",
				session->chat[session->chat_count - 1].text);
		}

		if (session->phase != MH_NET_LOBBY)
			continue;

		if (roster_revision != session->roster_revision ||
			status_revision !=
				session->replication.status_revision) {
			status = report_lobby(session, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			roster_revision = session->roster_revision;
			status_revision = session->replication.status_revision;
		}

		status = read_command(session, &console, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_network_flush(network, &issue, error_out);
}

int
main(int argc, char *argv[])
{
	struct mh_udp_transport_config transport_config;
	struct mh_transport_provider provider;
	struct mh_net_session_config config;
	struct mh_network *network;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long port;
	char *end;

	if (argc != 3 || strlen(argv[2]) >= MH_NET_NAME_BYTES)
		return 2;

	port = strtoul(argv[1], &end, 10);

	if (*argv[1] == 0 || *end != 0 || port == 0 || port > MH_U16_MAX)
		return 2;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);
	memset(&transport_config, 0, sizeof(transport_config));
	transport_config.clock = mh_system_clock_source();
#ifdef _WIN32
	transport_config.resolver = mh_windows_resolver_provider();
#else
	transport_config.resolver = mh_posix_resolver_provider();
#endif
	status = mh_udp_transport_init(&provider, &transport_config, &error);
	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.simulation_rate = 0;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.laps = 3;
	config.car_count = 10;
	strcpy(config.track, "Track1.trk");
	strcpy(config.profile.name, argv[2]);
	strcpy(config.profile.car, "Kobra.car");
	strcpy(config.profile.horn, "Horn1.chf");
	config.profile.update_rate = 15;
	config.profile.flags = MH_NET_FLAG_LOBBY;
	network = NULL;

	if (status == MH_ERROR_NONE)
		status = mh_network_open_address(&provider, 0, "127.0.0.1",
			"127.0.0.1", (unsigned int)port, &config, &network,
			&error);

	if (status == MH_ERROR_NONE)
		status = run(network, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_network_close(network);

	return status != MH_ERROR_NONE;
}
