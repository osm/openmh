#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/network/channel.h"
#include "engine/network/session.h"
#include "interfaces/network/backend.h"
#include "server/session/grid.h"
#include "server/session/host.h"
#include "server/session/results.h"
#include "server/session/setup.h"
#include "server/session/standings.h"

enum {
	MESSAGE_TYPE_COUNT = 22,
	ROUTE_KEY_COUNT = 65
};

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "lifecycle line %d: %s\n", __LINE__,   \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_server_host		host;
	struct mh_server_config		config;
	struct mh_transport_provider	provider;
	struct mh_transport		*transport;
	struct mh_transport_event	incoming;
	struct mh_server_race_logger	logger;
	struct mh_server_event_logger	event_logger;
	unsigned int entering, started, returned, eliminated, winners,
		timed_out, departures;
	char				last_event[MH_SERVER_EVENT_BYTES];
	char				previous_event[MH_SERVER_EVENT_BYTES];
	unsigned int			records;
	char				record[MH_SERVER_RACE_RECORD_BYTES];
	unsigned int			sent[MH_NET_WIRE_PLAYERS][MESSAGE_TYPE_COUNT];
	char				last_chat[MH_NET_WIRE_PLAYERS][MH_SERVER_TEXT_BYTES];
	unsigned int	hold_loading, muted, disconnected_mask, congested;
	unsigned long			disconnected;
	double				now_ms;
	int				no_address;
};

static struct fixture fixture;
static struct mh_net_session clients[MH_NET_WIRE_PLAYERS];
static struct mh_server_courses courses;
static struct mh_scene_key keys[ROUTE_KEY_COUNT];
static struct mh_error error;
static struct mh_channel channels[MH_NET_WIRE_PLAYERS];
static int openmh;

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

	if (output->type == MH_TRANSPORT_NONE) {
		unsigned int i;
		const struct mh_net_outgoing *out;

		for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
			out = mh_net_session_outgoing(&clients[i]);

			if (out == NULL &&
				(!openmh ||
					state->now_ms - channels[i].sent_ms <
						MH_CHANNEL_IDLE_MS ||
					((state->muted |
						state->disconnected_mask) &
						(1U << i))))
				continue;

			output->type = MH_TRANSPORT_RECEIVE;
			output->peer = 100 + i;
			output->reliable = out != NULL && out->reliable;
			output->size = out == NULL ? 0 : out->size;

			if (out != NULL)
				memcpy(output->data, out->data, out->size);

			if (openmh) {
				memmove(output->data + MH_CHANNEL_HEADER_BYTES,
					output->data, output->size);
				mh_channel_header(&channels[i], state->now_ms,
					output->data);
				mh_channel_sent(&channels[i], state->now_ms,
					output->reliable);
				output->size += MH_CHANNEL_HEADER_BYTES;
			}

			if (out != NULL)
				mh_net_session_sent(&clients[i]);

			break;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_endpoint(void *context, void *token, unsigned long peer,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct fixture *state;
	unsigned int player, type;
	enum mh_error_code status;

	(void)token;
	state = context;
	player = (unsigned int)(peer - 100);

	if (reliable && peer == state->congested)
		return mh_error_set(error_out, MH_ERROR_BACKPRESSURE,
			"fixture delivery queue full");

	if (openmh) {
		if (player == 0 || player >= MH_NET_WIRE_PLAYERS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"bad channel recipient");

		if (state->muted & (1U << player))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		status = mh_channel_receive(&channels[player], data, size,
			reliable, state->now_ms, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_net_session_contact(
			&clients[player], 1, state->now_ms, 0, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		data += MH_CHANNEL_HEADER_BYTES;
		size -= MH_CHANNEL_HEADER_BYTES;

		if (size == 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	type = (unsigned int)mh_read_u16le(data);

	if (player == 0 || player >= MH_NET_WIRE_PLAYERS || type > 21)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "bad fixture send");

	state->sent[player][type]++;

	if (type == MH_NET_CHAT) {
		if (size < 10 || size - 9 > sizeof(state->last_chat[player]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"bad fixture chat");

		memcpy(state->last_chat[player], data + 9, size - 9);
	}

	if (type == MH_NET_RACE_CONFIG &&
		strcmp(state->last_chat[player], "Entering race.") != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"setup arrived without entering-race notice");

	if (state->muted & (1U << player))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_net_session_receive(&clients[player], 1, data, size,
		reliable, state->now_ms, error_out);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "client %u phase %u rejects type %u: %s\n",
			player, (unsigned int)clients[player].phase, type,
			error_out ? error_out->message : "error");

	return status;
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
	state->disconnected_mask |= 1U << (unsigned int)(peer - 100);

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

static enum mh_error_code
peer_address(void *context, void *token, unsigned long peer,
	struct mh_transport_address *output, struct mh_error *error_out)
{
	(void)context;

	(void)token;

	if (fixture.no_address)
		return mh_error_set(
			error_out, MH_ERROR_UNSUPPORTED, "no client address");

	memset(output, 0, sizeof(*output));
	sprintf(output->host, "192.0.2.%lu", peer - 100);
	output->port = 16000;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_transport_ops ops = {open_endpoint, close_endpoint,
	connect_endpoint, poll_endpoint, send_endpoint, flush_endpoint,
	disconnect_endpoint, port_endpoint, now_endpoint, NULL, peer_address};
static const struct mh_transport_discovery_ops discovery = {
	advertise, search, search_poll, search_results};

static void
capture_record(void *context, const char *record)
{
	struct fixture *state;

	state = context;
	state->records++;
	strcpy(state->record, record);
}

static void
capture_event(void *context, const char *text)
{
	struct fixture *state;

	state = context;
	strcpy(state->previous_event, state->last_event);
	strcpy(state->last_event, text);
	state->entering += strcmp(text, "Entering race.") == 0;
	state->started +=
		strcmp(text, "All players in race. Race started!") == 0;
	state->returned += strcmp(text, "Back to lobby.") == 0;
	state->eliminated += strstr(text, " has been eliminated by ") != NULL;
	state->winners += strstr(text, " is the winner!") != NULL;
	state->timed_out += strstr(text, " timed out!") != NULL;
	state->departures += strstr(text, " disconnected (") != NULL ||
		strstr(text, " timed out!") != NULL;
}

static void
initialize_course_routes(void)
{
	unsigned int i;
	double angle;

	for (i = 0; i < ROUTE_KEY_COUNT; i++) {
		angle = 6.283185307179586 * (i % 64) / 64;
		keys[i].time = i;
		keys[i].linear = 1;
		keys[i].value[0] = 100 * sin(angle);
		keys[i].value[2] = 100 * cos(angle);
	}

	keys[16].value[4] = 1;
	keys[32].value[4] = 2;

	for (i = 0; i < courses.count; i++) {
		courses.routes[i].motion.keys = keys;
		courses.routes[i].motion.key_count = ROUTE_KEY_COUNT;
	}
}

static int
initialize_provider(unsigned int mode, int discovery_enabled)
{
	struct mh_net_session_config config;
	struct mh_transport_event event;
	unsigned int i;

	memset(&fixture, 0, sizeof(fixture));
	memset(&courses, 0, sizeof(courses));
	memset(keys, 0, sizeof(keys));
	mh_server_config_defaults(&fixture.config);
	fixture.config.network_legacy = !openmh;
	memset(channels, 0, sizeof(channels));
	fixture.config.mode =
		mode == 0 ? MH_SERVER_NORMAL : MH_SERVER_ELIMINATION;
	fixture.config.track_count = 2;
	fixture.config.laps = 2;
	fixture.config.step_track = 1;
	fixture.config.grid = MH_SERVER_GRID_SCORE_NORMAL;
	strcpy(fixture.config.tracks[0].name, "RedRock");
	strcpy(fixture.config.tracks[1].name, "Atlantika");
	courses.count = 2;

	initialize_course_routes();

	CHECK(mh_transport_provider_init(&fixture.provider, &ops,
		discovery_enabled ? &discovery : NULL, NULL, &fixture,
		&error) == MH_ERROR_NONE);
	CHECK(mh_transport_open_from(&fixture.provider, 1, NULL,
		&fixture.transport, &error) == MH_ERROR_NONE);
	CHECK(mh_server_host_init(&fixture.host, &fixture.config, "RedRock",
		fixture.transport, &error) == MH_ERROR_NONE);
	CHECK(mh_server_host_attach_courses(&fixture.host, &courses, &error) ==
		MH_ERROR_NONE);
	fixture.logger.write = capture_record;
	fixture.logger.context = &fixture;
	fixture.host.race_logger = &fixture.logger;
	fixture.event_logger.write = capture_event;
	fixture.event_logger.context = &fixture;
	fixture.host.event_logger = &fixture.event_logger;
	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.simulation_rate = openmh ? fixture.config.simulation_rate : 0;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.car_count = 10;
	config.laps = 2;
	config.mode = mode;
	config.profile.update_rate = 15;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.car, "Kobra X3");

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		sprintf(config.profile.name, "Driver%u", i);
		CHECK(mh_net_session_init(0, &config, &clients[i], &error) ==
			MH_ERROR_NONE);

		if (openmh) {
			fixture.incoming.type = MH_TRANSPORT_CONNECT;
			fixture.incoming.peer = 100 + i;
			CHECK(mh_transport_poll(fixture.transport, 0, &event,
				&error) == MH_ERROR_NONE);
		}

		clients[i].player = i;
		clients[i].phase = MH_NET_LOBBY;
		fixture.host.lobby.peers[i] = 100 + i;
		fixture.host.lobby.roster.players[i] = config.profile;
		fixture.host.lobby.roster.players[i].flags = MH_NET_FLAG_LOBBY;
	}

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		clients[i].roster = fixture.host.lobby.roster;
		clients[i].roster.players[0].flags &= ~MH_NET_FLAG_PRESENT;
	}

	return 0;
}

static int
initialize(unsigned int mode)
{
	return initialize_provider(mode, 1);
}

static int
step(double now_ms, unsigned int moving, unsigned int key)
{
	struct mh_net_vehicle vehicle;
	struct mh_net_session *client;
	double positions[MH_NET_WIRE_PLAYERS][3];
	unsigned int i;
	enum mh_error_code status;

	fixture.now_ms = now_ms;
	memset(positions, 0, sizeof(positions));

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if ((fixture.disconnected_mask | fixture.muted) & (1U << i))
			continue;

		client = &clients[i];

		if (client->phase == MH_NET_LOADING &&
			!(fixture.hold_loading & (1U << i))) {
			CHECK(mh_net_session_configure_replication(client, 180,
				(const double (*)[3])positions,
				&error) == MH_ERROR_NONE);
			CHECK(mh_net_session_loaded(client, now_ms, &error) ==
				MH_ERROR_NONE);
		}

		if (client->phase == MH_NET_WAITING ||
			client->phase == MH_NET_RACING) {
			memset(&vehicle, 0, sizeof(vehicle));
			memcpy(vehicle.position,
				keys[moving & (1U << i) ? key : 0].value,
				sizeof(vehicle.position));
			CHECK(mh_net_session_publish(client, &vehicle, i,
				&error) == MH_ERROR_NONE);
			CHECK(mh_net_session_start_frame(client,
				now_ms - fixture.host.race.origin_ms,
				&error) == MH_ERROR_NONE);
		}

		CHECK(mh_net_session_tick(client, now_ms, &error) ==
			MH_ERROR_NONE);
	}

	status = mh_server_host_poll(&fixture.host, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "host at %.0f: %s\n", now_ms, error.message);

	CHECK(status == MH_ERROR_NONE);

	return 0;
}

static int
request_ready(void)
{
	unsigned int i;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		char text[64];

		while (mh_net_session_outgoing(&clients[i]) != NULL)
			mh_net_session_sent(&clients[i]);

		sprintf(text, "%s: /ready", clients[i].config.profile.name);
		CHECK(mh_net_session_chat(&clients[i], text, 0, &error) ==
			MH_ERROR_NONE);
	}

	return 0;
}

static int
ready_and_start(void)
{
	unsigned int i, attempts;
	double now_ms;

	CHECK(request_ready() == 0);
	now_ms = fixture.now_ms;

	for (attempts = 0; attempts < 400; attempts++) {
		now_ms += 50;
		CHECK(step(now_ms, 0, 0) == 0);

		if (fixture.host.race.active &&
			fixture.host.race.clock.race_start_ms != 0)
			break;
	}

	CHECK(attempts < 400 && fixture.host.race.active);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		CHECK(clients[i].phase == MH_NET_RACING);
		CHECK(fixture.sent[i][MH_NET_RACE_CONFIG] != 0);
		CHECK(fixture.sent[i][MH_NET_START] != 0);
		CHECK(clients[i].roster.players[0].flags == 0);
	}

	CHECK(fixture.disconnected_mask == 0);

	return 0;
}

static int
run_race(unsigned int mode, unsigned int round)
{
	unsigned int step_index, i, attempts, moving;
	double now_ms;

	CHECK(ready_and_start() == 0);
	now_ms = fixture.now_ms;

	/* Every normal driver finishes; Elimination leaves eleven behind. */
	moving = mode == MH_NET_MODE_NORMAL ? 8190U : 2U;

	for (step_index = 0; step_index <= 32; step_index++) {
		now_ms += 100;
		CHECK(step(now_ms, moving, (step_index * 4) % 64) == 0);
	}

	CHECK(fixture.host.race.results[1].finished);

	if (mode == MH_NET_MODE_NORMAL)
		for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
			CHECK(fixture.host.race.results[i].finished);

	for (attempts = 0; attempts < 400; attempts++) {
		now_ms += 50;
		CHECK(step(now_ms, moving, 0) == 0);

		if (!fixture.host.race.active &&
			mh_server_lobby_synchronized(&fixture.host.lobby))
			break;
	}

	CHECK(attempts < 400 && fixture.disconnected_mask == 0);
	CHECK(fixture.records == round);
	CHECK(fixture.entering == round && fixture.started == round &&
		fixture.returned == round && fixture.timed_out == 0);
	CHECK(fixture.eliminated ==
		(mode == MH_NET_MODE_ELIMINATION ? 11 * round : 0));
	CHECK(fixture.winners == (mode == MH_NET_MODE_ELIMINATION ? round : 0));
	CHECK(strncmp(fixture.record,
		round == 1 ? "RedRock\t2\t" : "Atlantika\t2\t",
		round == 1 ? 10 : 12) == 0);
	CHECK(strstr(fixture.record, "Driver12") != NULL);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		CHECK(clients[i].phase == MH_NET_LOBBY);
		CHECK(clients[i].statistics_revision == round);
		CHECK(clients[i].roster.players[i]
				.values[MH_NET_PROFILE_RACES] == round);
		CHECK(clients[i].last_race.rows[0].position == 1);
		CHECK(fixture.sent[i][MH_NET_CAR] != 0);

		if (mode == MH_NET_MODE_ELIMINATION)
			CHECK(fixture.sent[i][MH_NET_CAR_FAR] != 0);

		CHECK(fixture.sent[i][MH_NET_LEAVE_RACE] == round);
	}

	CHECK(!strcmp(clients[12].roster.track,
		round == 1 ? "Atlantika" : "RedRock"));

	return 0;
}

static int
membership_and_timeouts(void)
{
	unsigned int i, attempts, notices;
	double now_ms;

	CHECK(initialize(MH_NET_MODE_NORMAL) == 0);
	fixture.hold_loading = 1U << 12;
	CHECK(request_ready() == 0);
	now_ms = 0;

	for (attempts = 0; attempts < 120; attempts++) {
		now_ms += 50;
		CHECK(step(now_ms, 0, 0) == 0);
	}

	CHECK(fixture.host.race.active &&
		fixture.host.race.start.started_ms == 0);
	CHECK(clients[12].phase == MH_NET_LOADING);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
		CHECK(fixture.sent[i][MH_NET_START] == 0);

	fixture.hold_loading = 0;

	for (attempts = 0; attempts < 350; attempts++) {
		now_ms += 50;
		CHECK(step(now_ms, 0, 0) == 0);

		if (fixture.host.race.clock.race_start_ms != 0)
			break;
	}

	CHECK(attempts < 350);

	/* A racing chat must not be treated as an invalid lobby command. */
	CHECK(mh_net_session_chat(&clients[12], "Driver12: hello", 0, &error) ==
		MH_ERROR_NONE);
	CHECK(step(now_ms + 50, 0, 0) == 0);
	now_ms += 50;
	CHECK(fixture.disconnected_mask == 0);
	CHECK(mh_net_session_return_lobby(&clients[12], &error) ==
		MH_ERROR_NONE);

	for (attempts = 0; attempts < 30; attempts++) {
		now_ms += 50;
		CHECK(step(now_ms, 0, 0) == 0);
	}

	CHECK(fixture.host.race.active && clients[12].phase == MH_NET_LOBBY);
	CHECK(!(fixture.host.lobby.return_pending & (1U << 12)));
	CHECK(!(clients[1].roster.players[12].flags & MH_NET_FLAG_RACING));
	CHECK(fixture.host.lobby.roster.players[12]
			.values[MH_NET_PROFILE_RACES] == 0);

	fixture.config.ghost_all = 1;

	for (attempts = 0; attempts < 25; attempts++) {
		now_ms += 50;
		CHECK(step(now_ms, 0, 0) == 0);
	}

	CHECK(!(clients[1].replication.status[1].flags &
		MH_NET_FLAG_UNAVAILABLE));
	CHECK(clients[1].replication.status[2].flags & MH_NET_FLAG_UNAVAILABLE);
	CHECK(!(clients[2].replication.status[2].flags &
		MH_NET_FLAG_UNAVAILABLE));
	fixture.config.ghost_all = 0;
	fixture.muted = 1U << 11;
	fixture.config.race_timeout_seconds = 10;

	for (attempts = 0; attempts < 230; attempts++) {
		now_ms += 50;
		CHECK(step(now_ms, 0, 0) == 0);
	}

	CHECK(fixture.disconnected_mask == (1U << 11));
	CHECK(!(clients[1].roster.players[11].flags & MH_NET_FLAG_PRESENT));
	CHECK(fixture.host.race.active && clients[1].phase == MH_NET_RACING);

	/* A full update outside compact-coordinate range must still relay. */
	keys[1].value[0] = 5000;
	notices = fixture.sent[2][MH_NET_CAR];
	CHECK(step(now_ms + 100, 1U << 1, 1) == 0);
	CHECK(step(now_ms + 200, 1U << 1, 1) == 0);
	CHECK(step(now_ms + 300, 1U << 1, 1) == 0);
	CHECK(fixture.sent[2][MH_NET_CAR] > notices);
	mh_transport_close(fixture.transport);

	return 0;
}

static int
selected_mode_clock(unsigned int mode)
{
	struct mh_server_operator_output output;
	unsigned int step_index;
	double now_ms;

	CHECK(initialize(1 - mode) == 0);
	CHECK(mh_server_host_command(&fixture.host,
		mode == MH_NET_MODE_NORMAL ? "/gamemode normal"
						: "/gamemode elimination",
		&output, &error) == MH_ERROR_NONE);
	CHECK(fixture.host.lobby.roster.mode == mode);
	CHECK((unsigned int)fixture.config.mode == 1 - mode);
	CHECK(ready_and_start() == 0);
	now_ms = fixture.now_ms;

	/*
	 * Only one finisher: Normal starts a countdown, Elimination does not.
	 */
	for (step_index = 0; step_index <= 32; step_index++) {
		now_ms += 100;
		CHECK(step(now_ms, 2U, (step_index * 4) % 64) == 0);
	}

	CHECK(fixture.host.race.results[1].finished);
	CHECK(step(now_ms + 50, 2U, 0) == 0);
	CHECK(fixture.host.race.active &&
		!fixture.host.race.clock.all_finished);
	CHECK((fixture.host.race.clock.finish_anchor_ms != 0) ==
		(mode == MH_NET_MODE_NORMAL));
	mh_transport_close(fixture.transport);

	return 0;
}

static int
wrong_way_ghosts(void)
{
	const struct mh_net_status *statuses;
	unsigned int i, recipient, source;
	double now_ms;
	int ghost;

	CHECK(initialize(MH_NET_MODE_NORMAL) == 0);
	CHECK(ready_and_start() == 0);
	now_ms = fixture.now_ms;

	/* Drive slot twelve backwards while every other car keeps reporting. */
	for (i = 1; i <= 40; i++) {
		now_ms += 100;
		CHECK(step(now_ms, 1U << 12, 64 - i) == 0);
	}

	CHECK(fixture.host.race.progress[12].wrong_way);

	for (ghost = 1; ghost >= 0; ghost--) {
		fixture.config.ghost_reverse = ghost;

		/*
		 * Keep the car stationary and fresh through a complete
		 * heartbeat.
		 */
		for (i = 0; i < 25; i++) {
			now_ms += 50;
			CHECK(step(now_ms, 1U << 12, 24) == 0);
		}

		CHECK(fixture.host.race.progress[12].wrong_way);

		for (recipient = 1; recipient < MH_NET_WIRE_PLAYERS;
			recipient++) {
			statuses = clients[recipient].replication.status;

			for (source = 1; source < MH_NET_WIRE_PLAYERS; source++)
				CHECK(!!(statuses[source].flags &
					MH_NET_FLAG_UNAVAILABLE) ==
					(ghost && source == 12 &&
						recipient != 12));
		}
	}

	fixture.config.ghost_reverse = 1;

	/* Forward travel clears the original backward-time accumulator. */
	for (i = 1; i <= 40; i++) {
		now_ms += 100;
		CHECK(step(now_ms, 1U << 12, (24 + i) % 64) == 0);
	}

	CHECK(!fixture.host.race.progress[12].wrong_way);

	for (i = 0; i < 25; i++) {
		now_ms += 50;
		CHECK(step(now_ms, 0, 0) == 0);
	}

	for (recipient = 1; recipient < MH_NET_WIRE_PLAYERS; recipient++)
		CHECK(!(clients[recipient].replication.status[12].flags &
			MH_NET_FLAG_UNAVAILABLE));

	mh_transport_close(fixture.transport);

	return 0;
}

static int
loading_timeout(void)
{
	unsigned int attempts;
	double now_ms;

	CHECK(initialize(MH_NET_MODE_NORMAL) == 0);
	fixture.config.start_timeout_seconds = 3;
	fixture.hold_loading = 1U << 12;
	fixture.muted = 1U << 12;
	CHECK(request_ready() == 0);

	/* Deliver readiness once; then that loader stops answering pings. */
	fixture.now_ms = 50;
	CHECK(mh_server_host_poll(&fixture.host, &error) == MH_ERROR_NONE);
	now_ms = 50;

	for (attempts = 0; attempts < 160; attempts++) {
		now_ms += 50;
		CHECK(step(now_ms, 0, 0) == 0);
	}

	CHECK(fixture.disconnected_mask == (1U << 12));
	CHECK(fixture.timed_out == 1);
	CHECK(fixture.host.race.start.started_ms != 0);
	CHECK(clients[1].phase == MH_NET_RACING);
	mh_transport_close(fixture.transport);

	return 0;
}

static int
forced_empty_race(unsigned int mode)
{
	struct mh_server_operator_output output;
	unsigned int i;
	double now_ms;

	CHECK(initialize(mode) == 0);
	fixture.config.min_ready = MH_SERVER_RACERS;
	fixture.config.all_ready = 1;
	fixture.now_ms = 1000;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		fixture.host.lobby.roster.players[i]
			.values[MH_NET_PROFILE_RACES] = 7;
		fixture.host.lobby.roster.players[i]
			.values[MH_NET_PROFILE_POINTS] = 42;
	}

	CHECK(mh_server_host_command(&fixture.host, "/ready", &output,
		&error) == MH_ERROR_NONE &&
		output.action == MH_SERVER_OPERATOR_START);
	CHECK(fixture.host.ready.deadline_ms == 21000);

	for (now_ms = 1000; now_ms <= 21000; now_ms += 100) {
		CHECK(step(now_ms, 0, 0) == 0);
		CHECK(!fixture.host.race.active && fixture.host.course == 0);
	}

	CHECK(step(21000.5, 0, 0) == 0);
	CHECK(step(21001, 0, 0) == 0);
	CHECK(!fixture.host.race.active && fixture.host.course == 1 &&
		fixture.host.ready.deadline_ms == 0 &&
		!fixture.host.ready.start_requested);
	CHECK(fixture.records == 0 && fixture.entering == 1 &&
		fixture.returned == 1 && fixture.started == 0);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		CHECK(strcmp(fixture.last_chat[i],
			"You failed to go ready, missed race start...") ==
			0);
		CHECK(clients[i].phase == MH_NET_LOBBY &&
			fixture.sent[i][MH_NET_RACE_CONFIG] == 0 &&
			fixture.host.lobby.roster.players[i]
					.values[MH_NET_PROFILE_RACES] == 7 &&
			fixture.host.lobby.roster.players[i]
					.values[MH_NET_PROFILE_POINTS] == 42);
	}

	mh_transport_close(fixture.transport);

	return 0;
}

static int
operator_kick(void)
{
	struct mh_server_operator_output output;
	unsigned int i;

	CHECK(initialize(MH_NET_MODE_NORMAL) == 0);
	fixture.host.lobby.roster.players[6].values[MH_NET_PROFILE_POINTS] = 42;
	fixture.host.lobby.clear_votes = (1U << 6) | (1U << 2);
	fixture.host.lobby.roster.players[2].flags |= MH_NET_FLAG_READY;
	CHECK(mh_server_host_command(&fixture.host, "/kick 6", &output,
		&error) == MH_ERROR_NONE);
	CHECK(output.action == MH_SERVER_OPERATOR_KICK && output.player == 6);
	CHECK(output.text.count == 1 &&
		strcmp(output.text.lines[0], "Player Driver6 kicked!") == 0);
	CHECK(clients[6].kicked && clients[6].phase == MH_NET_CLOSED);
	CHECK(fixture.disconnected_mask == (1U << 6));
	CHECK(fixture.host.lobby.peers[6] == 0 &&
		fixture.host.lobby.roster.players[6].flags == 0 &&
		fixture.host.lobby.roster.players[6]
				.values[MH_NET_PROFILE_POINTS] == 42);
	CHECK(fixture.host.lobby.clear_votes == (1U << 2));

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		CHECK(fixture.sent[i][MH_NET_KICK] == (i == 6));

		if (i == 6)
			continue;

		CHECK(clients[i].phase == MH_NET_LOBBY && !clients[i].kicked);
		CHECK(clients[i].roster.players[6].flags == 0);
		CHECK(clients[i].roster.players[2].flags & MH_NET_FLAG_READY);
	}

	CHECK(mh_server_host_command(&fixture.host, "/kick 6", &output,
		&error) == MH_ERROR_NONE);
	CHECK(output.action == MH_SERVER_OPERATOR_NONE &&
		strcmp(output.text.lines[0], "No player with number: 6") == 0);
	CHECK(fixture.sent[6][MH_NET_KICK] == 1);
	mh_transport_close(fixture.transport);

	return 0;
}

static int
majority_kick(unsigned long minutes, int full, int no_address)
{
	unsigned int i;
	char notice[MH_SERVER_TEXT_BYTES];

	CHECK(initialize(MH_NET_MODE_NORMAL) == 0);
	fixture.now_ms = 1000;
	fixture.config.ban_minutes = minutes;
	fixture.no_address = no_address;

	if (full)
		for (i = 0; i < MH_SERVER_BANS; i++)
			CHECK(mh_server_bans_add(&fixture.host.bans,
				"192.0.2.100", 0, 0,
				NULL) == MH_ERROR_NONE);

	for (i = 1; i <= 6; i++)
		fixture.host.lobby.kick_votes[i] = 12;

	CHECK(mh_server_host_poll(&fixture.host, &error) == MH_ERROR_NONE);
	CHECK(fixture.disconnected_mask == 0);
	fixture.host.lobby.kick_votes[7] = 12;
	CHECK(mh_server_host_poll(&fixture.host, &error) == MH_ERROR_NONE);
	CHECK(fixture.disconnected_mask == (1U << 12) && clients[12].kicked);
	CHECK(mh_server_bans_contains(&fixture.host.bans, "192.0.2.12") ==
		(!full && !no_address));

	if (!full && !no_address)
		CHECK(fixture.host.bans.entries[0].expires_ms ==
			(minutes == 0 ? 0 : 1000 + minutes * 60000));

	if (!full && !no_address)
		sprintf(notice,
			"Driver12 was kicked and banned(%lu mins) by majority vote...",
			minutes);
	else
		strcpy(notice, "Driver12 was kicked by majority vote...");

	CHECK(strcmp(fixture.previous_event, notice) == 0);
	CHECK(strcmp(fixture.last_event, "Driver12 disconnected (kicked).") ==
		0);
	CHECK(fixture.departures == 1);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		CHECK(fixture.host.lobby.kick_votes[i] == 0);
		CHECK(clients[i].chat_count == (i == 12 ? 2 : 1) &&
			strcmp(clients[i].chat[0].text, notice) == 0);
	}

	CHECK(strcmp(clients[12].chat[1].text,
		"You got kicked by the server!") == 0);

	mh_transport_close(fixture.transport);

	return 0;
}

static int
congested_driver(void)
{
	struct mh_server_operator_output output;

	CHECK(initialize(MH_NET_MODE_NORMAL) == 0);
	fixture.congested = 101;
	CHECK(mh_server_host_command(&fixture.host, "hello", &output, &error) ==
		MH_ERROR_NONE);
	CHECK(!strcmp(fixture.last_chat[2], "console: hello"));
	CHECK(mh_server_host_poll(&fixture.host, &error) == MH_ERROR_NONE);
	CHECK(fixture.host.lobby.peers[1] == 0 &&
		fixture.host.lobby.peers[2] == 102);
	CHECK(fixture.departures == 1);
	mh_transport_close(fixture.transport);

	return 0;
}

static int
disconnect_logging(void)
{
	CHECK(initialize(MH_NET_MODE_NORMAL) == 0);
	fixture.incoming.type = MH_TRANSPORT_DISCONNECT;
	fixture.incoming.peer = 101;
	CHECK(mh_server_host_poll(&fixture.host, &error) == MH_ERROR_NONE);
	CHECK(fixture.departures == 1);
	CHECK(strcmp(fixture.last_event,
		"Driver1 disconnected (connection closed).") == 0);
	CHECK(!(clients[2].roster.players[1].flags & MH_NET_FLAG_PRESENT));

	/* A delayed close notification must not log a second departure. */
	fixture.incoming.type = MH_TRANSPORT_DISCONNECT;
	fixture.incoming.peer = 101;
	CHECK(mh_server_host_poll(&fixture.host, &error) == MH_ERROR_NONE);
	CHECK(fixture.departures == 1);

	CHECK(mh_net_session_leave(&clients[2], &error) == MH_ERROR_NONE);
	CHECK(mh_server_host_poll(&fixture.host, &error) == MH_ERROR_NONE);
	CHECK(fixture.departures == 2);
	CHECK(strcmp(fixture.last_event,
		"Driver2 disconnected (left the server).") == 0);

	/* Reserved connections have not entered the lobby yet. */
	fixture.host.lobby.pending_peers[2] = 102;
	fixture.host.lobby.admission.reserved |= 1U << 2;
	fixture.incoming.type = MH_TRANSPORT_DISCONNECT;
	fixture.incoming.peer = 102;
	CHECK(mh_server_host_poll(&fixture.host, &error) == MH_ERROR_NONE);
	CHECK(fixture.departures == 2);
	CHECK(fixture.host.lobby.pending_peers[2] == 0);
	mh_transport_close(fixture.transport);

	return 0;
}

int
main(int argc, char *argv[])
{
	unsigned int mode, i;

	if (argc == 2 && strcmp(argv[1], "--openmh") == 0) {
		openmh = 1;

		for (mode = 0; mode <= 1; mode++) {
			CHECK(initialize(mode) == 0);
			CHECK(run_race(mode, 1) == 0);
			CHECK(run_race(mode, 2) == 0);

			for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
				CHECK(fixture.sent[i][MH_NET_PING] == 0);
				CHECK(channels[i].sample_count != 0);
			}

			mh_transport_close(fixture.transport);
		}

		return 0;
	}

	CHECK(argc == 1);

	for (mode = 0; mode <= 1; mode++) {
		CHECK(initialize(mode) == 0);
		CHECK(run_race(mode, 1) == 0);
		CHECK(run_race(mode, 2) == 0);
		mh_transport_close(fixture.transport);
		CHECK(forced_empty_race(mode) == 0);
		CHECK(selected_mode_clock(mode) == 0);
		CHECK(initialize_provider(mode, 0) == 0);
		CHECK(run_race(mode, 1) == 0);
		mh_transport_close(fixture.transport);
	}

	CHECK(congested_driver() == 0);
	CHECK(disconnect_logging() == 0);
	CHECK(membership_and_timeouts() == 0);
	CHECK(wrong_way_ghosts() == 0);
	CHECK(loading_timeout() == 0);
	CHECK(operator_kick() == 0);
	CHECK(majority_kick(2, 0, 0) == 0);
	CHECK(majority_kick(0, 0, 0) == 0);
	CHECK(majority_kick(2, 1, 0) == 0);
	CHECK(majority_kick(2, 0, 1) == 0);

	return 0;
}
