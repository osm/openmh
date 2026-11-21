#include <stdio.h>
#include <string.h>

#include "../support/net_virtual_io.h"
#include "client/diagnostics/runtime_network.h"
#include "engine/core/error.h"
#include "engine/network/network_session.h"
#include "interfaces/network/network.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)
static struct mh_network *networks[9];

struct chat_observer {
	struct mh_net_session	*session;
	unsigned long		revision;
	unsigned int		calls, messages;
	int			stop_on_message;
};

static void
publish(struct mh_net_session *session, double now)
{
	struct mh_net_vehicle vehicle;
	unsigned int i;

	if (!session->replication.configured ||
		(session->phase != MH_NET_WAITING &&
			session->phase != MH_NET_RACING))
		return;

	memset(&vehicle, 0, sizeof(vehicle));

	for (i = 0; i < 4; i++)
		vehicle.extension[i] = .8;

	vehicle.rpm = 5000;
	vehicle.gear = 3;
	vehicle.position[0] = session->player * 100 + now * .01;
	CHECK(mh_net_session_publish(session, &vehicle, session->player,
		NULL) == MH_ERROR_NONE);
}

static struct mh_net_session_config
session_config(unsigned int i)
{
	struct mh_net_session_config settings;

	memset(&settings, 0, sizeof(settings));
	settings.provider = 2;
	strcpy(settings.password, "hello");
	strcpy(settings.track, "Track1.trk");
	settings.laps = 3;
	settings.car_count = 10;
	strcpy(settings.profile.name, "Driver0");
	settings.profile.name[6] = (char)('0' + i);
	strcpy(settings.profile.car, "Kobra.car");
	strcpy(settings.profile.horn, "Horn1.chf");
	settings.profile.update_rate = 15;
	settings.prediction.minimum = MH_NET_PREDICTION_MIN_DEFAULT;
	settings.prediction.maximum = MH_NET_PREDICTION_MAX_DEFAULT;

	return settings;
}

static void
sample_remotes(struct mh_net_session *session)
{
	struct mh_net_playback playback;
	unsigned int slot;

	if (!session->replication.configured ||
		(session->phase != MH_NET_WAITING &&
			session->phase != MH_NET_RACING))
		return;

	for (slot = 0; slot < session->roster.player_count; slot++) {
		if (slot == session->player ||
			!(session->roster.players[slot].flags & 1))
			continue;

		CHECK(mh_net_session_sample(session, slot, session->now_ms,
			&playback, NULL) == MH_ERROR_NONE);
	}
}

static enum mh_error_code
step_network(struct mh_network *network, struct mh_network_issue *issue,
	struct mh_error *error_out)
{
	mh_test_net_advance(1);

	return mh_network_step(network, 0, issue, error_out);
}

static void
step_all(void)
{
	struct mh_network_issue issue;
	struct mh_net_session *session;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;

	for (i = 0; i < 9; i++)
		if (networks[i] != NULL) {
			session = mh_network_session(networks[i]);
			publish(session, mh_network_now(networks[i]));
			status = step_network(networks[i], &issue, &error);

			if (status != MH_ERROR_NONE)
				fprintf(stderr, "network %u: %s\n", i,
					error.message);

			if (issue.reason.code != MH_ERROR_NONE)
				fprintf(stderr, "network %u peer %lu: %s\n", i,
					issue.peer, issue.reason.message);

			CHECK(status == MH_ERROR_NONE &&
				issue.reason.code == MH_ERROR_NONE);

			/*
			 * No car simulation in this transport fixture:
			 * explicitly supply a synthetic game clock,
			 * independently of transport polling.
			 */
			if (session->phase == MH_NET_WAITING ||
				session->phase == MH_NET_RACING)
				CHECK(mh_net_session_start_frame(session,
					mh_network_now(networks[i]),
					NULL) == MH_ERROR_NONE);

			sample_remotes(session);
		}
}

static int
all_joined(void)
{
	unsigned int i;

	for (i = 1; i < 8; i++)
		if (mh_network_session(networks[i])->phase != MH_NET_LOBBY)
			return 0;

	return 1;
}

static void
wait_phase(unsigned int index, enum mh_net_session_phase phase)
{
	double limit;

	limit = mh_network_now(networks[0]) + 10000;

	while (mh_network_session(networks[index])->phase != phase &&
		mh_network_now(networks[0]) < limit && failures == 0)
		step_all();

	if (mh_network_session(networks[index])->phase != phase)
		fprintf(stderr, "peer %u phase %u expected %u at %.0f\n", index,
			(unsigned int)mh_network_session(networks[index])
				->phase,
			(unsigned int)phase, mh_network_now(networks[0]));

	CHECK(mh_network_session(networks[index])->phase == phase);
}

static long double
key_clock(void *context)
{
	return mh_network_now((struct mh_network *)context);
}

static enum mh_error_code
key_frame(unsigned int peer, struct mh_lobby_keys *keys,
	const unsigned char events[MH_KEY_COUNT],
	struct mh_lobby_key_output *output, struct mh_error *error_out)
{
	static const struct mh_lobby_key_name tracks[] = {
		{"Track1.trk"}, {"Track2.trk"}};

	static const struct mh_lobby_key_name cars[] = {
		{"Kobra.car"}, {"Beta Car"}};

	struct mh_lobby_key_catalog catalog;
	struct mh_lobby_key_input input;

	catalog.tracks = tracks;
	catalog.cars = cars;
	catalog.track_count = catalog.car_count = catalog.car_limit = 2;
	memset(&input, 0, sizeof(input));
	input.events = events;
	input.clock_ms = key_clock;
	input.clock_context = networks[peer];

	return mh_net_session_keys(mh_network_session(networks[peer]), keys,
		&catalog, &input, mh_network_now(networks[peer]), output,
		error_out);
}

static void
race_round(void)
{
	struct mh_net_session *session;
	struct mh_net_playback playback;
	double position[MH_NET_PLAYERS][3];
	unsigned int i, j, slot;
	double limit;
	struct mh_lobby_keys keys;
	struct mh_lobby_key_output output;
	unsigned char events[MH_KEY_COUNT];

	session = mh_network_session(networks[0]);
	memset(&keys, 0, sizeof(keys));
	memset(events, 0, sizeof(events));
	events[0x3f] = 1;
	CHECK(key_frame(0, &keys, events, &output, NULL) == MH_ERROR_NONE &&
		output.actions[0].kind == MH_LOBBY_KEY_START);

	for (i = 1; i < 8; i++)
		wait_phase(i, MH_NET_LOADING);

	memset(position, 0, sizeof(position));

	for (i = 0; i < MH_NET_PLAYERS; i++)
		position[i][0] = i * 100;

	for (i = 0; i < 8; i++) {
		CHECK(mh_net_session_configure_replication(
			mh_network_session(networks[i]), 180,
			(const double (*)[3])position,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_net_session_status(session, i, 100 + i, 200 + i,
			NULL) == MH_ERROR_NONE);
	}

	for (i = 0; i < 7; i++)
		CHECK(mh_net_session_loaded(mh_network_session(networks[i]),
			mh_network_now(networks[i]),
			NULL) == MH_ERROR_NONE);

	limit = mh_network_now(networks[0]) + 3000;

	while (mh_network_now(networks[0]) < limit && failures == 0)
		step_all();

	CHECK(session->phase == MH_NET_WAITING &&
		session->start.anchor_ms == 0);
	CHECK(mh_net_session_loaded(mh_network_session(networks[7]),
		mh_network_now(networks[7]), NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		wait_phase(i, MH_NET_RACING);

	CHECK(session->ready_mask == 255 && session->start.sent_mask == 254);
	limit = mh_network_now(networks[0]) + 3000;

	while (mh_network_now(networks[0]) < limit && failures == 0)
		step_all();

	for (i = 0; i < 8; i++) {
		struct mh_net_session *peer;

		peer = mh_network_session(networks[i]);
		CHECK(peer->replication.history[peer->player].count == 0);

		for (j = 0; j < 8; j++) {
			CHECK(peer->replication.status[j].value_a == 100 + j &&
				peer->replication.status[j].value_b == 200 + j);

			if (j == peer->player)
				continue;

			CHECK(peer->replication.history[j].count == 10);
			CHECK(mh_net_session_sample(peer, j, peer->now_ms,
				&playback, NULL) == MH_ERROR_NONE);
			CHECK(playback.ready && playback.updated &&
				!playback.stale);
			CHECK(playback.vehicle.position[0] > j * 100 &&
				playback.vehicle.position[0] <
					j * 100 + limit * .01 + 10);
			CHECK(playback.world_velocity[0] > 0 &&
				playback.world_velocity[0] < 20);
		}
	}

	CHECK(mh_net_session_return_lobby(session, NULL) == MH_ERROR_NONE);
	CHECK(key_frame(0, &keys, events, &output, NULL) == MH_ERROR_NONE &&
		output.actions[0].kind == MH_LOBBY_KEY_WAIT);

	for (i = 1; i < 8; i++)
		wait_phase(i, MH_NET_LOBBY);

	for (i = 0; i < 8; i++)
		CHECK(!mh_network_session(networks[i])->replication.configured);

	/*
	 * Return acknowledgement precedes the next setup; previous race pongs
	 * cannot satisfy its separate loading/ready barrier.
	 */
	limit = mh_network_now(networks[0]) + 10000;

	while (session->return_pending && mh_network_now(networks[0]) < limit &&
		failures == 0)
		step_all();

	CHECK(session->return_pending == 0);
	CHECK(key_frame(0, &keys, events, &output, NULL) == MH_ERROR_NONE &&
		output.actions[0].kind == MH_LOBBY_KEY_START);

	for (i = 1; i < 8; i++)
		wait_phase(i, MH_NET_LOADING);

	CHECK(mh_net_session_loaded(session, mh_network_now(networks[0]),
		NULL) == MH_ERROR_NONE);
	limit = mh_network_now(networks[0]) + 3000;

	while (mh_network_now(networks[0]) < limit && failures == 0)
		step_all();

	CHECK(session->phase == MH_NET_WAITING &&
		session->start.anchor_ms == 0 && session->ready_mask == 1);
	CHECK(mh_net_session_return_lobby(session, NULL) == MH_ERROR_NONE);

	for (i = 1; i < 8; i++)
		wait_phase(i, MH_NET_LOBBY);

	slot = mh_network_session(networks[3])->player;
	CHECK(mh_net_session_kick(session, slot, NULL) == MH_ERROR_NONE);
	wait_phase(3, MH_NET_CLOSED);
	CHECK(mh_network_session(networks[3])->kicked);
	mh_network_close(networks[3]);
	networks[3] = NULL;
}

static void
lobby_round(void)
{
	static const struct mh_lobby_command_name tracks[] = {
		{"Track1.trk"}, {"Track2.trk"}};

	static const struct mh_lobby_command_name cars[] = {
		{"Kobra.car"}, {"Beta Car"}};

	static const struct mh_lobby_command_name horns[] = {
		{"Horn1.chf"}, {"Two Horn"}};

	struct mh_net_session_config settings;
	struct mh_net_session *session;
	struct mh_lobby_command_catalog catalog;
	struct mh_lobby_command_state console, host_console;
	struct mh_lobby_command_output output;
	struct mh_lobby_keys host_keys, client_keys;
	struct mh_lobby_key_output key_output;
	unsigned char key_events[MH_KEY_COUNT];
	double limit;
	unsigned int i, slot, ready;

	memset(&console, 0, sizeof(console));
	memset(&host_console, 0, sizeof(host_console));
	catalog.tracks = tracks;
	catalog.cars = cars;
	catalog.horns = horns;
	catalog.track_count = catalog.track_limit = catalog.car_count =
		catalog.car_limit = catalog.horn_count = 2;

	for (i = 1; i < 8; i++) {
		settings = session_config(i);
		CHECK(mh_network_open(0, NULL, "127.0.0.1",
			mh_network_port(networks[0]), &settings,
			&networks[i], NULL) == MH_ERROR_NONE);
	}

	/*
	 * Keep admission bounded while servicing all clients. Packet-loss race
	 * coverage lives in the separate network_play integration scenario.
	 */
	limit = mh_network_now(networks[0]) + 30000;

	while (!all_joined() && mh_network_now(networks[0]) < limit &&
		failures == 0)
		step_all();

	CHECK(all_joined());

	if (!all_joined()) {
		for (i = 0; i < 8; i++) {
			session = mh_network_session(networks[i]);
			fprintf(stderr,
				"join timeout peer %u phase %u slot %u clock %.0f rejection %u: %s\n",
				i, (unsigned int)session->phase,
				session->player, session->now_ms,
				session->rejection_reason, session->rejection);
		}

		session = mh_network_session(networks[0]);

		for (i = 0; i < 8; i++)
			fprintf(stderr,
				"host slot %u phase %u peer %lu reserved %.0f\n",
				i, (unsigned int)session->lobby.slots[i].phase,
				session->lobby.slots[i].peer,
				session->lobby.slots[i].reserved_ms);

		return;
	}

	if (mh_network_now(networks[0]) > 15000)
		fprintf(stderr, "all peers joined after %.0f ms\n",
			mh_network_now(networks[0]));

	/* Wait for the final roster to reach every client. */
	limit = mh_network_now(networks[0]) + 2000;

	while (mh_network_now(networks[0]) < limit && failures == 0)
		step_all();

	for (i = 1; i < 8; i++)
		CHECK(memcmp(&mh_network_session(networks[i])->roster,
			&mh_network_session(networks[0])->roster,
			sizeof(struct mh_net_roster)) == 0);

	settings = session_config(8);
	strcpy(settings.password, "wrong");
	CHECK(mh_network_open(0, NULL, "127.0.0.1",
		mh_network_port(networks[0]), &settings, &networks[8],
		NULL) == MH_ERROR_NONE);
	wait_phase(8, MH_NET_REJECTED);
	CHECK(mh_network_session(networks[8])->rejection_reason == 2);
	mh_network_close(networks[8]);
	networks[8] = NULL;
	settings = session_config(8);
	CHECK(mh_network_open(0, NULL, "127.0.0.1",
		mh_network_port(networks[0]), &settings, &networks[8],
		NULL) == MH_ERROR_NONE);
	wait_phase(8, MH_NET_REJECTED);
	CHECK(mh_network_session(networks[8])->rejection_reason == 1);
	mh_network_close(networks[8]);
	networks[8] = NULL;
	session = mh_network_session(networks[3]);
	slot = session->player;
	CHECK(mh_net_session_command(session, &console, &catalog,
		"/name Renamed", &output, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_command(session, &console, &catalog,
		"/colour 255 128 64", &output, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_command(session, &console, &catalog,
		"/car Beta Car", &output, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_command(session, &console, &catalog,
		"/record Round", &output, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_command(session, &console, &catalog,
		"hello racers", &output, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(console.demo_path, "demos\\Round.mde"));
	session = mh_network_session(networks[0]);
	CHECK(mh_net_session_command(session, &host_console, &catalog,
		"/track Track2.trk", &output, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_command(session, &host_console, &catalog,
		"/laps 7", &output, NULL) == MH_ERROR_NONE);
	limit = mh_network_now(networks[0]) + 5000;

	do {
		step_all();
		ready = 0;

		for (i = 0; i < 8; i++) {
			session = mh_network_session(networks[i]);

			if (session->chat_count != 0 &&
				!strcmp(session->chat[session->chat_count - 1]
						.text,
					"Renamed: hello racers") &&
				strcmp(session->roster.players[slot].name,
					"Renamed") == 0 &&
				session->roster.players[slot].values[0] ==
					0xff8040UL &&
				(session->roster.players[slot].flags & 4) &&
				!strcmp(session->roster.players[slot].car,
					"Beta Car") &&
				session->roster.laps == 7 &&
				!strcmp(session->roster.track, "Track2.trk"))
				ready++;
		}
	} while (ready != 8 && mh_network_now(networks[0]) < limit &&
		failures == 0);

	for (i = 0; i < 8; i++) {
		session = mh_network_session(networks[i]);
		CHECK(session->chat_count != 0 &&
			strcmp(session->chat[session->chat_count - 1].text,
				"Renamed: hello racers") == 0);
		CHECK(session->chat_count != 0 &&
			session->chat[session->chat_count - 1].color ==
				0xff8040UL &&
			session->roster.players[slot].values[0] == 0xff8040UL);
		CHECK((session->roster.players[slot].flags & 4) &&
			!strcmp(session->roster.players[slot].car, "Beta Car"));
		CHECK(session->roster.laps == 7 &&
			!strcmp(session->roster.track, "Track2.trk"));
		CHECK(strcmp(session->roster.players[slot].name, "Renamed") ==
			0);
	}

	memset(&host_keys, 0, sizeof(host_keys));
	memset(&client_keys, 0, sizeof(client_keys));
	memset(key_events, 0, sizeof(key_events));
	key_events[0x3b] = key_events[0x3c] = 1;
	CHECK(key_frame(0, &host_keys, key_events, &key_output, NULL) ==
			MH_ERROR_NONE &&
		key_output.count == 0);
	memset(key_events, 0, sizeof(key_events));
	key_events[0x40] = key_events[0x41] = 1;
	CHECK(key_frame(3, &client_keys, key_events, &key_output, NULL) ==
			MH_ERROR_NONE &&
		key_output.count == 0);
	memset(key_events, 0, sizeof(key_events));
	limit = mh_network_now(networks[0]) + 10000;

	do {
		CHECK(key_frame(0, &host_keys, key_events, &key_output, NULL) ==
			MH_ERROR_NONE);
		CHECK(key_frame(3, &client_keys, key_events, &key_output,
			NULL) == MH_ERROR_NONE);
		step_all();
		ready = 0;

		for (i = 0; i < 8; i++) {
			session = mh_network_session(networks[i]);

			if (!strcmp(session->roster.track, "Track1.trk") &&
				session->roster.laps == 5 &&
				!strcmp(session->roster.players[slot].car,
					"Kobra.car") &&
				session->roster.players[slot].gear_mode == 1 &&
				session->replication.status_revision != 0 &&
				session->replication.status_valid == 255)
				ready++;
		}
	} while (ready != 8 && mh_network_now(networks[0]) < limit &&
		failures == 0);

	CHECK(ready == 8);
	CHECK(mh_net_session_leave(mh_network_session(networks[3]), NULL) ==
		MH_ERROR_NONE);
	limit = mh_network_now(networks[0]) + 5000;

	while (mh_network_session(networks[0])->lobby.slots[slot].phase !=
			MH_NET_EMPTY &&
		mh_network_now(networks[0]) < limit && failures == 0)
		step_all();

	CHECK(mh_network_session(networks[0])->lobby.slots[slot].phase ==
		MH_NET_EMPTY);
	mh_network_close(networks[3]);
	networks[3] = NULL;
	settings = session_config(3);
	CHECK(mh_network_open(0, NULL, "127.0.0.1",
		mh_network_port(networks[0]), &settings, &networks[3],
		NULL) == MH_ERROR_NONE);
	wait_phase(3, MH_NET_LOBBY);
	CHECK(mh_network_session(networks[3])->player == slot);
	race_round();
}

static enum mh_error_code
observe_chat(void *context, struct mh_error *error_out)
{
	struct chat_observer *observer;

	observer = (struct chat_observer *)context;
	observer->calls++;

	if (observer->session->chat_revision == observer->revision)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	CHECK(observer->session->chat_revision == observer->revision + 1);
	observer->revision = observer->session->chat_revision;
	observer->messages++;

	return observer->stop_on_message
		? mh_error_set(error_out, MH_ERROR_FAILED, "observer stopped")
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
observed_chat_round(void)
{
	struct chat_observer observer;
	struct mh_network_issue issue;
	struct mh_net_session *host, *peer;
	struct mh_error error;
	enum mh_error_code status;
	double deadline;

	host = mh_network_session(networks[0]);
	peer = mh_network_session(networks[1]);
	memset(&observer, 0, sizeof(observer));
	observer.session = host;
	observer.revision = host->chat_revision;
	observer.stop_on_message = 1;
	CHECK(mh_network_drain_observed(networks[0], 0, &issue, observe_chat,
		&observer, NULL) != MH_ERROR_NONE);
	CHECK(mh_network_drain_observed(networks[0], 257, &issue, observe_chat,
		&observer, NULL) != MH_ERROR_NONE);
	CHECK(mh_network_drain_observed(NULL, 1, &issue, observe_chat,
		&observer, NULL) != MH_ERROR_NONE);
	CHECK(observer.calls == 0);
	CHECK(mh_net_session_chat(peer, "first observed", 0x112233UL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_chat(peer, "second observed", 0x445566UL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_network_flush(networks[1], &issue, NULL) == MH_ERROR_NONE &&
		issue.reason.code == MH_ERROR_NONE);

	/* Stop after the first delivery, leaving the second packet unread. */
	status = MH_ERROR_NONE;
	deadline = mh_network_now(networks[0]) + 5000;

	while (observer.messages == 0 && failures == 0 &&
		mh_network_now(networks[0]) < deadline) {
		CHECK(step_network(networks[1], &issue, NULL) ==
				MH_ERROR_NONE &&
			issue.reason.code == MH_ERROR_NONE);
		status = mh_network_drain_observed(networks[0], 256, &issue,
			observe_chat, &observer, &error);
		CHECK(issue.reason.code == MH_ERROR_NONE);
	}

	CHECK(status != MH_ERROR_NONE &&
		strcmp(error.message, "observer stopped") == 0);
	CHECK(observer.messages == 1 && host->chat_count != 0);

	if (host->chat_count != 0)
		CHECK(strcmp(host->chat[host->chat_count - 1].text,
			"Driver1: first observed") == 0);

	observer.stop_on_message = 0;

	while (observer.messages < 2 && failures == 0 &&
		mh_network_now(networks[0]) < deadline) {
		CHECK(step_network(networks[1], &issue, NULL) ==
				MH_ERROR_NONE &&
			issue.reason.code == MH_ERROR_NONE);
		CHECK(mh_network_drain_observed(networks[0], 256, &issue,
			observe_chat, &observer, NULL) == MH_ERROR_NONE &&
			issue.reason.code == MH_ERROR_NONE);
	}

	CHECK(observer.messages == 2 && host->chat_count != 0);

	if (host->chat_count != 0)
		CHECK(strcmp(host->chat[host->chat_count - 1].text,
			"Driver1: second observed") == 0);
}

static void
flush_round(void)
{
	struct mh_net_session_config settings;
	struct mh_net_session *host, *peer;
	struct mh_network_issue issue;
	enum mh_error_code status;
	double before;
	unsigned long revision;
	unsigned int i;

	settings = session_config(0);
	CHECK(mh_network_open(1, "127.0.0.1", NULL, MH_NET_LANINET_PORT,
		&settings, &networks[0], NULL) == MH_ERROR_NONE);

	if (networks[0] == NULL)
		return;

	settings = session_config(1);
	CHECK(mh_network_open(0, NULL, "127.0.0.1",
		mh_network_port(networks[0]), &settings, &networks[1],
		NULL) == MH_ERROR_NONE);

	if (networks[1] == NULL) {
		mh_network_close(networks[0]);
		networks[0] = NULL;
		return;
	}

	wait_phase(1, MH_NET_LOBBY);
	observed_chat_round();
	host = mh_network_session(networks[0]);
	peer = mh_network_session(networks[1]);
	CHECK(mh_net_session_chat(peer, "incoming during load", 0x112233UL,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_network_flush(networks[1], &issue, NULL) == MH_ERROR_NONE &&
		issue.reason.code == MH_ERROR_NONE);
	CHECK(mh_net_session_begin_race(host, mh_network_now(networks[0]),
		NULL) == MH_ERROR_NONE);
	before = host->now_ms;
	revision = host->chat_revision;
	CHECK(mh_network_flush(NULL, &issue, NULL) != MH_ERROR_NONE &&
		mh_network_flush(networks[0], NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_session_outgoing(host) != NULL);
	CHECK(mh_network_flush(networks[0], &issue, NULL) == MH_ERROR_NONE &&
		issue.reason.code == MH_ERROR_NONE);
	CHECK(mh_net_session_outgoing(host) == NULL);
	CHECK(host->now_ms == before && host->chat_revision == revision &&
		host->phase == MH_NET_LOADING);

	/*
	 * The host does no further polling: setup must already be on the wire.
	 * Its incoming chat also remains unconsumed throughout client loading.
	 */
	for (i = 0; i < 1000 && peer->phase != MH_NET_LOADING && failures == 0;
		i++) {
		status = step_network(networks[1], &issue, NULL);
		CHECK(status == MH_ERROR_NONE &&
			issue.reason.code == MH_ERROR_NONE);
	}

	CHECK(peer->phase == MH_NET_LOADING);
	CHECK(host->now_ms == before && host->chat_revision == revision);
	mh_network_close(networks[1]);
	mh_network_close(networks[0]);
	networks[0] = networks[1] = NULL;
}

/*
 * OpenMH uses the real session/transport stack with deterministic loss and
 * clock advancement. No application PING/PONG is permitted in this mode.
 */
static void
channel_round(void)
{
	struct mh_net_session_config settings;
	struct mh_net_session *host;
	double positions[MH_NET_PLAYERS][3], until;
	unsigned int i;

	settings = session_config(0);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.simulation_rate = 120;
	CHECK(mh_network_open(1, "127.0.0.1", NULL, MH_NET_LANINET_PORT,
		&settings, &networks[0], NULL) == MH_ERROR_NONE);
	settings = session_config(1);
	settings.wire = MH_NET_WIRE_31_LOBBY;
	settings.simulation_rate = 120;
	CHECK(mh_network_open(0, "127.0.0.1", "127.0.0.1", MH_NET_LANINET_PORT,
		&settings, &networks[1], NULL) == MH_ERROR_NONE);
	wait_phase(1, MH_NET_LOBBY);
	host = mh_network_session(networks[0]);

	/* An otherwise idle lobby must remain alive beyond its timeout. */
	until = mh_network_now(networks[0]) + 45000;

	while (mh_network_now(networks[0]) < until && failures == 0)
		step_all();

	CHECK(mh_network_session(networks[1])->phase == MH_NET_LOBBY);
	CHECK(host->connections[0].ping_ms < 100);

	CHECK(mh_net_session_begin_race(host, mh_network_now(networks[0]),
		NULL) == MH_ERROR_NONE);
	wait_phase(1, MH_NET_LOADING);
	memset(positions, 0, sizeof(positions));

	for (i = 0; i < 2; i++) {
		CHECK(mh_net_session_configure_replication(
			mh_network_session(networks[i]), 180,
			(const double (*)[3])positions,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_net_session_loaded(mh_network_session(networks[i]),
			mh_network_now(networks[i]),
			NULL) == MH_ERROR_NONE);
	}

	wait_phase(0, MH_NET_RACING);
	wait_phase(1, MH_NET_RACING);
	mh_test_net_faults(7, 5, 2);
	until = mh_network_now(networks[0]) + 5000;

	while (mh_network_now(networks[0]) < until && failures == 0)
		step_all();

	mh_test_net_faults(0, 0, 0);
	CHECK(mh_net_session_return_lobby(host, NULL) == MH_ERROR_NONE);
	wait_phase(1, MH_NET_LOBBY);
	until = mh_network_now(networks[0]) + 1000;

	while (mh_network_now(networks[0]) < until && failures == 0)
		step_all();

	CHECK(host->return_pending == 0);
	mh_network_close(networks[1]);
	mh_network_close(networks[0]);
}

int
main(int argc, char *argv[])
{
	struct mh_net_session_config settings;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;

	if (argc == 2 && strcmp(argv[1], "--openmh") == 0) {
		channel_round();
		return failures != 0;
	}

	if (argc == 2 && strcmp(argv[1], "--shared-port") == 0)
		mh_test_net_shared_port();
	else if (argc != 1)
		return 1;

	settings = session_config(0);
	status = mh_network_open(1, "127.0.0.1", NULL, MH_NET_LANINET_PORT,
		&settings, &networks[0], &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Loopback unavailable: %s\n", error.message);
		return 77;
	}

	mh_network_close(networks[0]);
	networks[0] = NULL;
	flush_round();

	if (failures == 0) {
		CHECK(mh_network_open(1, "127.0.0.1", NULL, MH_NET_LANINET_PORT,
			&settings, &networks[0], NULL) == MH_ERROR_NONE);
		lobby_round();
	}

	for (i = 0; i < 9; i++)
		mh_network_close(networks[i]);

	return failures != 0;
}
