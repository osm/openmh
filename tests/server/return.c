#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/network/session.h"
#include "interfaces/network/backend.h"
#include "server/session/grid.h"
#include "server/session/host.h"
#include "server/session/results.h"
#include "server/session/setup.h"
#include "server/session/standings.h"

#define CAPTURE_LIMIT 32
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "return line %d: %s\n", __LINE__, #e); \
			return 1;                                              \
		}                                                              \
	} while (0)

struct capture {
	unsigned long			peer;
	unsigned int			type, pending;
	int				reliable;
	struct mh_transport_event	event;
};

struct fixture {
	struct mh_server_host		host;
	struct mh_server_config		config;
	struct mh_transport_provider	provider;
	struct mh_transport		*transport;
	struct mh_transport_event	incoming;
	struct capture			sent[CAPTURE_LIMIT];
	unsigned int			count, fail_send;
	unsigned long			disconnected;
	double				now_ms;
};

static struct fixture fixture;
static struct mh_net_session client;

static enum mh_error_code
open_endpoint(void *context, int server, const void *local, void **output,
	struct mh_error *error_out)
{
	(void)server;

	(void)local;
	*output = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_endpoint(void *context, void *token)
{
	(void)context;
	(void)token;
}

static enum mh_error_code
connect_endpoint(void *context, void *token, const void *target,
	struct mh_error *error_out)
{
	(void)context;

	(void)token;
	(void)target;

	return mh_error_set(
		error_out, MH_ERROR_UNSUPPORTED, "unused connection");
}

static enum mh_error_code
poll_endpoint(void *context, void *token, struct mh_transport_event *output,
	struct mh_error *error_out)
{
	struct fixture *state;

	(void)token;
	state = context;
	*output = state->incoming;
	state->incoming.type = MH_TRANSPORT_NONE;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_endpoint(void *context, void *token, unsigned long peer,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct fixture *state;
	struct capture *capture;

	(void)token;
	state = context;

	if (state->fail_send == state->count + 1 ||
		state->count == CAPTURE_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_IO, "fixture send failed");

	capture = &state->sent[state->count++];
	capture->peer = peer;
	capture->type = (unsigned int)mh_read_u16le(data);
	capture->reliable = reliable;
	capture->pending = state->host.lobby.return_pending;
	capture->event.size = size;
	memcpy(capture->event.data, data, size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
flush_endpoint(void *context, void *token, struct mh_error *error_out)
{
	(void)context;
	(void)token;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
disconnect_endpoint(void *context, void *token, unsigned long peer,
	struct mh_error *error_out)
{
	struct fixture *state;

	(void)token;
	state = context;
	state->disconnected = peer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
port_endpoint(void *context, const void *token)
{
	(void)context;
	(void)token;

	return 16000;
}

static double
now_endpoint(void *context, void *token)
{
	struct fixture *state;

	(void)token;
	state = context;

	return state->now_ms;
}

static enum mh_error_code
advertise(void *context, void *token,
	const struct mh_discovery_advertisement *game,
	struct mh_error *error_out)
{
	(void)game;

	return flush_endpoint(context, token, error_out);
}

static enum mh_error_code
search(void *context, void *token, const void *target, unsigned long id,
	struct mh_error *error_out)
{
	(void)id;

	return connect_endpoint(context, token, target, error_out);
}

static enum mh_error_code
search_poll(void *context, void *token, int *done, struct mh_error *error_out)
{
	*done = 1;

	return flush_endpoint(context, token, error_out);
}

static const struct mh_discovery_catalog *
search_results(void *context, const void *token)
{
	(void)context;
	(void)token;

	return NULL;
}

static const struct mh_transport_ops ops = {open_endpoint, close_endpoint,
	connect_endpoint, poll_endpoint, send_endpoint, flush_endpoint,
	disconnect_endpoint, port_endpoint, now_endpoint, NULL, NULL};
static const struct mh_transport_discovery_ops discovery = {
	advertise, search, search_poll, search_results};

static int
initialize(void)
{
	unsigned int i;

	memset(&fixture, 0, sizeof(fixture));
	mh_server_config_defaults(&fixture.config);
	fixture.config.network_legacy = 1;
	CHECK(mh_transport_provider_init(&fixture.provider, &ops, &discovery,
		NULL, &fixture, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_open_from(&fixture.provider, 1, NULL,
		&fixture.transport, NULL) == MH_ERROR_NONE);
	CHECK(mh_server_host_init(&fixture.host, &fixture.config, "RedRock",
		fixture.transport, NULL) == MH_ERROR_NONE);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
		fixture.host.lobby.peers[i] = 100 + i;

	return 0;
}

static int
queue_pong(unsigned int player, unsigned int type, unsigned long peer,
	unsigned long stamp)
{
	struct mh_net_message message;

	memset(&message, 0, sizeof(message));
	message.type = type;
	message.player = player;
	message.stamp = stamp;
	fixture.incoming.type = MH_TRANSPORT_RECEIVE;
	fixture.incoming.peer = peer;
	fixture.incoming.reliable = 0;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message,
		fixture.incoming.data, sizeof(fixture.incoming.data),
		&fixture.incoming.size, NULL) == MH_ERROR_NONE);

	return 0;
}

static int
client_refresh(unsigned int player)
{
	struct mh_net_session_config config;
	struct capture *capture;
	unsigned int i;

	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.profile.update_rate = 15;
	config.laps = 3;
	config.car_count = 10;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.name, "Driver");
	strcpy(config.profile.car, "Kobra X3");
	CHECK(mh_net_session_init(0, &config, &client, NULL) == MH_ERROR_NONE);
	client.player = player;
	client.phase = MH_NET_LOBBY;
	client.roster = fixture.host.lobby.roster;
	client.roster.players[0].flags &= ~MH_NET_FLAG_PRESENT;

	for (i = 0; i < fixture.count; i++) {
		capture = &fixture.sent[i];
		CHECK(mh_net_session_receive(&client, 1, capture->event.data,
			capture->event.size, capture->reliable,
			fixture.now_ms, NULL) == MH_ERROR_NONE);
	}

	CHECK(client.statistics_revision == 1 &&
		client.last_race.rows[0].top_speed_kmh == 321 &&
		!strcmp(client.last_race.rows[0].name, "Winner"));
	CHECK(client.roster.players[player].value_a == player &&
		client.roster.players[player].values[MH_NET_PROFILE_POINTS] ==
			fixture.host.lobby.roster.players[player]
				.values[MH_NET_PROFILE_POINTS]);

	return 0;
}

static int
refresh_twelve(void)
{
	struct mh_server_lobby before;
	struct mh_server_setup setup, saved;
	struct mh_server_result results[MH_NET_WIRE_PLAYERS];
	struct mh_server_standings standings;
	unsigned int i;
	unsigned long seed;

	CHECK(initialize() == 0);
	memset(results, 0, sizeof(results));
	fixture.now_ms = 10;
	fixture.host.lobby.roster.players[0].flags = MH_NET_FLAG_RACING;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		fixture.host.lobby.roster.players[i].flags = i == 12
			? MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS
			: MH_NET_FLAG_RACING | MH_NET_FLAG_RECORDING;
		fixture.host.lobby.roster.players[i].value_a = i;
		fixture.host.lobby.roster.players[i]
			.values[MH_NET_PROFILE_POINTS] = 1000 + i;
		results[i].progress = MH_NET_WIRE_PLAYERS - i;
		results[i].lap = 1;
	}

	strcpy(fixture.host.lobby.roster.players[1].name, "Winner");
	results[1].finished = 1;
	results[1].elapsed_ms = 123456;
	results[1].top_speed_kmh = 321;
	CHECK(mh_server_standings_rank(&fixture.host.lobby, results, &standings,
		NULL) == MH_ERROR_NONE);
	CHECK(standings.count == 11 && standings.finished_count == 1);
	CHECK(mh_server_results_apply(&fixture.host.lobby, results, 0,
		&fixture.host.last_race, NULL) == MH_ERROR_NONE);
	CHECK(results[1].best_lap_ms == 123456 &&
		fixture.host.last_race.rows[0].race_ms == 123456 &&
		fixture.host.lobby.roster.players[1]
				.values[MH_NET_PROFILE_POINTS] == 1013 &&
		fixture.host.lobby.roster.players[11]
				.values[MH_NET_PROFILE_RACES] == 1 &&
		fixture.host.lobby.roster.players[12]
				.values[MH_NET_PROFILE_RACES] == 0);
	fixture.host.lobby.clear_votes = 8191;

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		fixture.host.lobby.kick_votes[i] = 1;

	mh_server_lobby_resume(&fixture.host.lobby);

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		CHECK(fixture.host.lobby.kick_votes[i] == 0);

	CHECK(fixture.host.lobby.clear_votes == 0);
	CHECK(fixture.host.lobby.return_pending == 8191);
	CHECK(!mh_server_lobby_synchronized(&fixture.host.lobby));
	CHECK(fixture.host.lobby.roster.players[12].flags ==
		(MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS));

	/* A late loading/racing pong must not release a lobby refresh. */
	CHECK(queue_pong(12, MH_NET_PONG, 112, 0) == 0);
	CHECK(mh_server_host_poll(&fixture.host, NULL) == MH_ERROR_NONE);
	CHECK(fixture.count == 0 && fixture.host.lobby.return_pending == 8191);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		fixture.count = 0;
		CHECK(queue_pong(i, MH_NET_LOBBY_PONG, 100 + i, 0) == 0);
		CHECK(mh_server_host_poll(&fixture.host, NULL) ==
			MH_ERROR_NONE);
		CHECK(fixture.count == 2 &&
			fixture.sent[0].type == MH_NET_ROSTER &&
			fixture.sent[1].type == MH_NET_LAST_RACE);
		CHECK(fixture.sent[0].peer == 100 + i &&
			fixture.sent[1].peer == 100 + i &&
			fixture.sent[0].reliable && fixture.sent[1].reliable);
		CHECK((fixture.sent[0].pending & (1U << i)) &&
			(fixture.sent[1].pending & (1U << i)) &&
			!(fixture.host.lobby.return_pending & (1U << i)));
		CHECK(client_refresh(i) == 0);
		CHECK(mh_server_lobby_synchronized(&fixture.host.lobby) ==
			(i == 12));
	}

	/* Pending pass slot blocks setup even though it won't drive. */
	fixture.host.lobby.roster.players[1].flags |= MH_NET_FLAG_READY;
	fixture.host.lobby.return_pending |= 1U << 12;
	seed = 123;
	CHECK(mh_server_grid_assign(&fixture.host.lobby,
		MH_SERVER_GRID_LAST_RACE, NULL, &seed,
		NULL) == MH_ERROR_NONE);
	before = fixture.host.lobby;
	memset(&setup, 0xa5, sizeof(setup));
	saved = setup;
	CHECK(mh_server_setup_begin(&fixture.host.lobby, &setup, NULL) ==
		MH_ERROR_STATE);
	CHECK(!memcmp(&before, &fixture.host.lobby, sizeof(before)) &&
		!memcmp(&saved, &setup, sizeof(saved)));
	fixture.count = 0;
	CHECK(queue_pong(12, MH_NET_LOBBY_PONG, 112, 0) == 0);
	CHECK(mh_server_host_poll(&fixture.host, NULL) == MH_ERROR_NONE);
	CHECK(mh_server_setup_begin(&fixture.host.lobby, &setup, NULL) ==
		MH_ERROR_NONE);
	CHECK(setup.racers == 2);
	mh_transport_close(fixture.transport);

	return 0;
}

static int
heartbeat_and_failure(void)
{
	unsigned int i, pings, statuses, attempt;

	CHECK(initialize() == 0);
	fixture.host.lobby.roster.players[1].flags = MH_NET_FLAG_LOBBY;
	fixture.host.lobby.roster.players[12].flags = MH_NET_FLAG_LOBBY;
	fixture.host.lobby.return_pending = 1U << 12;
	fixture.now_ms = 1001;
	CHECK(mh_server_host_poll(&fixture.host, NULL) == MH_ERROR_NONE);
	pings = statuses = 0;

	for (i = 0; i < fixture.count; i++) {
		if (fixture.sent[i].type == MH_NET_PING)
			pings++;
		else {
			CHECK(fixture.sent[i].type == MH_NET_STATUS &&
				fixture.sent[i].peer == 101);
			statuses++;
		}
	}

	CHECK(pings == 2 && statuses == 1);

	for (attempt = 1; attempt <= 2; attempt++) {
		fixture.count = 0;
		fixture.fail_send = attempt;
		CHECK(queue_pong(12, MH_NET_LOBBY_PONG, 112, 1001) == 0);
		CHECK(mh_server_host_poll(&fixture.host, NULL) == MH_ERROR_IO);
		CHECK(fixture.host.lobby.return_pending == (1U << 12));
	}

	fixture.count = fixture.fail_send = 0;
	CHECK(queue_pong(12, MH_NET_LOBBY_PONG, 112, 1001) == 0);
	CHECK(mh_server_host_poll(&fixture.host, NULL) == MH_ERROR_NONE);
	CHECK(fixture.count == 2 && fixture.host.lobby.return_pending == 0);
	fixture.count = 0;
	CHECK(queue_pong(12, MH_NET_LOBBY_PONG, 112, 1001) == 0);
	CHECK(mh_server_host_poll(&fixture.host, NULL) == MH_ERROR_NONE);
	CHECK(fixture.count == 0);

	/* Another peer cannot release or receive the pending player's data. */
	fixture.host.lobby.return_pending = 1U << 12;
	CHECK(queue_pong(12, MH_NET_LOBBY_PONG, 101, 1001) == 0);
	CHECK(mh_server_host_poll(&fixture.host, NULL) == MH_ERROR_NONE);
	CHECK(fixture.disconnected == 101 &&
		fixture.host.lobby.return_pending == (1U << 12));

	for (i = 0; i < fixture.count; i++)
		CHECK(fixture.sent[i].type != MH_NET_LAST_RACE);

	CHECK(mh_server_lobby_disconnect(&fixture.host.lobby, 112));
	CHECK(fixture.host.lobby.return_pending == 0 &&
		mh_server_lobby_synchronized(&fixture.host.lobby));
	mh_transport_close(fixture.transport);

	return 0;
}

static int
clear_scores(void)
{
	struct mh_server_operator_output output;
	struct mh_net_message message;
	struct mh_net_frame frame;
	struct mh_net_profile *profile;
	unsigned int i;

	CHECK(initialize() == 0);
	fixture.host.lobby.clear_votes = (1U << 1) | (1U << 2);
	strcpy(fixture.host.last_race.rows[0].name, "Winner");
	fixture.host.last_race.rows[0].race_ms = 123456;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		profile = &fixture.host.lobby.roster.players[i];
		profile->flags = i <= 3 ? MH_NET_FLAG_LOBBY : 0;
		profile->values[MH_NET_PROFILE_RACES] = 7;
		profile->values[MH_NET_PROFILE_POINTS] = 40 + i;
		profile->value_a = i;
		profile->value_b = 2;
		fixture.host.race.results[i].position = i;
		fixture.host.race.results[i].lap = 2;
		fixture.host.race.results[i].best_lap_ms = 30000 + i;
	}

	fixture.host.lobby.roster.players[1].flags |= MH_NET_FLAG_READY;
	CHECK(mh_server_host_poll(&fixture.host, NULL) == MH_ERROR_NONE);
	CHECK(fixture.count == 6 && fixture.host.lobby.clear_votes == 0);
	CHECK(fixture.host.lobby.roster.players[1].flags ==
		(MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY));
	CHECK(fixture.host.last_race.rows[0].race_ms == 123456 &&
		!strcmp(fixture.host.last_race.rows[0].name, "Winner"));

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		profile = &fixture.host.lobby.roster.players[i];
		CHECK(profile->values[MH_NET_PROFILE_RACES] == 0 &&
			profile->values[MH_NET_PROFILE_POINTS] == 0 &&
			profile->value_a == 0 && profile->value_b == 0);
		CHECK(fixture.host.race.results[i].position == 0 &&
			fixture.host.race.results[i].lap == 0 &&
			fixture.host.race.results[i].best_lap_ms == 30000 + i);
	}

	for (i = 0; i < fixture.count; i++) {
		CHECK(fixture.sent[i].reliable);
		frame.data = fixture.sent[i].event.data;
		frame.size = fixture.sent[i].event.size;
		frame.type = fixture.sent[i].type;
		CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &message,
			NULL) == MH_ERROR_NONE);
		CHECK(message.type == (i < 3 ? MH_NET_CHAT : MH_NET_ROSTER));

		if (i < 3)
			CHECK(message.color == 0xe06820 &&
				!strcmp(message.text,
					"Score and gridstats cleared by majority vote..."));
	}

	fixture.count = 0;
	fixture.host.lobby.clear_votes = 1U << 1;
	CHECK(mh_server_host_command(&fixture.host, "/clear", &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.action == MH_SERVER_OPERATOR_CLEAR &&
		!strcmp(output.text.lines[0],
			"Score and gridstats cleared...") &&
		fixture.count == 6 && fixture.host.lobby.clear_votes == 0);
	mh_transport_close(fixture.transport);

	return 0;
}

int
main(void)
{
	return refresh_twelve() || heartbeat_and_failure() || clear_scores();
}
