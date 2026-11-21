#include <stdio.h>
#include <string.h>

#include "backends/network/posix_provider.h"
#include "backends/network/udp_transport_provider.h"
#include "backends/network/windows_provider.h"
#include "engine/network/network_address.h"
#include "engine/network/network_session.h"
#include "interfaces/clock/backend.h"
#include "interfaces/network/address.h"
#include "interfaces/network/network.h"

static int failures;
static unsigned long ticks;

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static unsigned long
clock_ticks(void *context)
{
	(void)context;

	return ticks;
}

static struct mh_clock_counter
clock_counter(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = ticks;
	value.high = 0;

	return value;
}

static struct mh_clock_counter
clock_frequency(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 1000;
	value.high = 0;

	return value;
}

static void
session_config(struct mh_net_session_config *config, const char *name)
{
	memset(config, 0, sizeof(*config));
	config->wire = MH_NET_WIRE_31_LOBBY;
	config->provider = 4;
	strcpy(config->track, "Track 1");
	strcpy(config->password, "Secret");
	config->laps = 5;
	config->car_count = 10;
	config->profile.flags = MH_NET_FLAG_LOBBY;
	config->profile.update_rate = 20;
	strcpy(config->profile.name, name);
	strcpy(config->profile.car, "Car 1");
	strcpy(config->profile.horn, "Horn 1");
}

static void
drain_network(struct mh_network *network)
{
	struct mh_network_issue issue;
	struct mh_error error;

	if (network == NULL)
		return;

	if (mh_network_drain(network, 64, &issue, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "network drain: %s\n", error.message);
		failures++;
	}

	if (issue.reason.code != MH_ERROR_NONE) {
		fprintf(stderr, "peer %lu: %s\n", issue.peer,
			issue.reason.message);
		failures++;
	}
}

static void
pump_peers(struct mh_network *host, struct mh_network **clients,
	unsigned int count)
{
	unsigned int pass, i;

	for (pass = 0; pass < 40; pass++) {
		drain_network(host);

		for (i = 0; i < count; i++)
			drain_network(clients[i]);
	}
}

static void
check_chat(struct mh_net_session *session, const char *text)
{
	CHECK(session->chat_count != 0);

	if (session->chat_count != 0)
		CHECK(strcmp(session->chat[session->chat_count - 1].text,
			text) == 0);
}

static void
race_setup_round(
	struct mh_network *host, struct mh_network **clients, unsigned int mode)
{
	struct mh_net_session *session;
	double positions[MH_NET_PLAYERS][3];
	struct mh_net_vehicle vehicle;
	unsigned int i, j, ranks[MH_NET_PLAYERS];
	unsigned long races[MH_NET_PLAYERS], points[MH_NET_PLAYERS];
	static const unsigned int awards[MH_NET_PLAYERS] = {
		12, 10, 8, 6, 4, 3, 2, 1};

	session = mh_network_session(host);

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		races[i] =
			session->roster.players[i].values[MH_NET_PROFILE_RACES];
		points[i] = session->roster.players[i]
				.values[MH_NET_PROFILE_POINTS];
		ranks[i] = MH_NET_PLAYERS - i;
	}

	session->config.mode = mode;
	CHECK(mh_net_session_begin_race(session, mh_network_now(host), NULL) ==
		MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS - 1);
	memset(positions, 0, sizeof(positions));
	memset(&vehicle, 0, sizeof(vehicle));

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		session = mh_network_session(i == 0 ? host : clients[i - 1]);
		CHECK(session->phase == MH_NET_LOADING &&
			session->roster.mode == mode);
		CHECK(mh_net_session_configure_replication(session, 180,
			(const double (*)[3])positions,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_net_session_publish(session, &vehicle, session->player,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_net_session_loaded(session, session->now_ms, NULL) ==
			MH_ERROR_NONE);
	}

	ticks += 1001;
	pump_peers(host, clients, MH_NET_PLAYERS - 1);
	ticks += 51;
	pump_peers(host, clients, MH_NET_PLAYERS - 1);
	session = mh_network_session(host);
	CHECK(session->ready_mask == 255);
	CHECK(mh_net_session_start_frame(session, 1001, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_start_frame(session, 3002, NULL) == MH_ERROR_NONE);
	CHECK(session->phase == MH_NET_RACING);
	pump_peers(host, clients, MH_NET_PLAYERS - 1);

	for (i = 0; i < MH_NET_PLAYERS - 1; i++) {
		session = mh_network_session(clients[i]);
		CHECK(session->start_pending);
		CHECK(mh_net_session_start_frame(session, 3003, NULL) ==
			MH_ERROR_NONE);
		CHECK(session->phase == MH_NET_RACING);
	}

	/*
	 * Actual progress reaches race status, then survives the scored return.
	 */
	for (i = 0; i < MH_NET_PLAYERS; i++)
		CHECK(mh_net_session_status(mh_network_session(host), i,
			ranks[i], 2 + i % 3, NULL) == MH_ERROR_NONE);

	ticks += 1001;
	pump_peers(host, clients, MH_NET_PLAYERS - 1);

	for (i = 0; i < MH_NET_PLAYERS - 1; i++) {
		session = mh_network_session(clients[i]);

		for (j = 0; j < MH_NET_PLAYERS; j++) {
			CHECK(session->replication.status[j].value_a ==
				ranks[j]);
			CHECK(session->replication.status[j].value_b ==
				2 + j % 3);
		}
	}

	CHECK(mh_net_session_finish_race(
		mh_network_session(host), ranks, NULL) == MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS - 1);
	ticks += 1001;
	pump_peers(host, clients, MH_NET_PLAYERS - 1);
	CHECK(mh_network_session(host)->return_pending == 0);

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		session = mh_network_session(host);
		CHECK(session->roster.players[i].value_a == ranks[i]);
		CHECK(session->roster.players[i].value_b == 2 + i % 3);
		CHECK(session->roster.players[i].values[MH_NET_PROFILE_RACES] ==
			races[i] + 1);
		CHECK(session->roster.players[i]
				.values[MH_NET_PROFILE_POINTS] ==
			points[i] + awards[ranks[i] - 1]);

		if (i == 0)
			continue;

		session = mh_network_session(clients[i - 1]);
		CHECK(session->phase == MH_NET_LOBBY);
		CHECK(!memcmp(&session->roster,
			&mh_network_session(host)->roster,
			sizeof(session->roster)));
		CHECK(session->config.profile.values[MH_NET_PROFILE_RACES] ==
			races[i] + 1);
	}
}

static void
check_discovery(
	const struct mh_transport_provider *provider, struct mh_network *host)
{
	struct mh_transport *query;
	const struct mh_discovery_catalog *catalog;
	unsigned int i;
	int done;

	query = NULL;
	CHECK(mh_transport_open_address(provider, 0, "127.0.0.1", 0, &query,
		NULL) == MH_ERROR_NONE);

	if (query == NULL)
		return;

	CHECK(mh_transport_search_address(query, "127.0.0.1",
		mh_network_port(host), 1, NULL) == MH_ERROR_NONE);
	done = 0;

	for (i = 0; i < 20 && !done; i++) {
		CHECK(mh_transport_search_poll(query, &done, NULL) ==
			MH_ERROR_NONE);
		drain_network(host);
	}

	CHECK(done);
	catalog = mh_transport_search_results(query);
	CHECK(catalog != NULL && catalog->count == 1);

	if (catalog != NULL && catalog->count == 1) {
		CHECK(catalog->entries[0].game.players == 8);
		CHECK(catalog->entries[0].game.password ==
			MH_DISCOVERY_PASSWORD_UNKNOWN);
		CHECK(strcmp(catalog->entries[0].game.roster.players[1].name,
			"Renamed") == 0);
	}

	mh_transport_close(query);
}

static void
check_lobby(const struct mh_transport_provider *provider)
{
	struct mh_net_session_config config;
	struct mh_network *host, *clients[MH_NET_PLAYERS];
	struct mh_net_session *session;
	struct mh_net_profile profile;
	char text[MH_NET_CHAT_BYTES];
	unsigned int i, port;
	unsigned long revision;

	host = NULL;
	memset(clients, 0, sizeof(clients));
	session_config(&config, "Host");
	CHECK(mh_network_open_address(provider, 1, "127.0.0.1", NULL, 0,
		&config, &host, NULL) == MH_ERROR_NONE);

	if (host == NULL)
		goto done;

	port = mh_network_port(host);
	session_config(&config, "Client");
	strcpy(config.password, "sEcReT");

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		CHECK(mh_network_open_address(provider, 0, "127.0.0.1",
			"127.0.0.1", port, &config, &clients[i],
			NULL) == MH_ERROR_NONE);

		if (clients[i] == NULL)
			goto done;

		pump_peers(host, clients, i + 1);
		session = mh_network_session(clients[i]);

		if (i < MH_NET_PLAYERS - 1) {
			CHECK(session->phase == MH_NET_LOBBY &&
				session->player == i + 1);
			CHECK(strcmp(session->roster.players[0].name, "Host") ==
				0);
		} else {
			CHECK(session->phase == MH_NET_REJECTED &&
				session->rejection_reason == 1);
			CHECK(strcmp(session->rejection, "Server full!") == 0);
		}
	}

	CHECK(mh_net_session_chat(mh_network_session(clients[0]), "Client chat",
		0x123456UL, NULL) == MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS);
	check_chat(mh_network_session(host), "Client chat");
	check_chat(mh_network_session(clients[1]), "Client chat");

	/* Exercise the patched producer's complete 1024-byte text buffer. */
	memset(text, 'x', sizeof(text) - 1);
	text[sizeof(text) - 1] = 0;
	CHECK(mh_net_session_chat(mh_network_session(host), text, 0xabcdefUL,
		NULL) == MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS);
	check_chat(mh_network_session(clients[0]), text);
	profile = mh_network_session(clients[0])->config.profile;
	strcpy(profile.name, "Renamed");
	CHECK(mh_net_session_profile(mh_network_session(clients[0]), &profile,
		NULL) == MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS);
	CHECK(strcmp(mh_network_session(host)->roster.players[1].name,
		"Renamed") == 0);
	CHECK(strcmp(mh_network_session(clients[1])->roster.players[1].name,
		"Renamed") == 0);
	session = mh_network_session(clients[0]);
	revision = session->replication.status_revision;
	ticks += 1501;
	pump_peers(host, clients, MH_NET_PLAYERS);
	CHECK(session->replication.status_revision > revision);
	CHECK(session->replication.status_valid == 255);
	CHECK(mh_network_session(host)->connections[0].ping_stamp == 1501);
	CHECK(!mh_network_session(host)->connections[0].ping_pending);
	check_discovery(provider, host);
	CHECK(mh_net_session_leave(session, NULL) == MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS);
	CHECK(mh_network_session(host)->lobby.slots[1].phase == MH_NET_EMPTY);
	mh_network_close(clients[0]);
	clients[0] = NULL;
	CHECK(mh_network_open_address(provider, 0, "127.0.0.1", "127.0.0.1",
		port, &config, &clients[0], NULL) == MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS);
	CHECK(clients[0] != NULL);

	if (clients[0] != NULL) {
		CHECK(mh_network_session(clients[0])->phase == MH_NET_LOBBY);
		CHECK(mh_network_session(clients[0])->player == 1);
	}

	CHECK(mh_net_session_kick(mh_network_session(host), 2, NULL) ==
		MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS);
	CHECK(mh_network_session(clients[1])->phase == MH_NET_CLOSED &&
		mh_network_session(clients[1])->kicked);
	CHECK(mh_network_session(host)->lobby.slots[2].phase == MH_NET_EMPTY);
	CHECK(mh_network_session(clients[0])->roster.players[2].flags == 0);
	mh_network_close(clients[1]);
	clients[1] = NULL;
	CHECK(mh_network_open_address(provider, 0, "127.0.0.1", "127.0.0.1",
		port, &config, &clients[1], NULL) == MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS);
	CHECK(clients[1] != NULL);

	if (clients[1] != NULL)
		CHECK(mh_network_session(clients[1])->phase == MH_NET_LOBBY &&
			mh_network_session(clients[1])->player == 2);

	race_setup_round(host, clients, MH_NET_MODE_NORMAL);
	race_setup_round(host, clients, MH_NET_MODE_ELIMINATION);
	CHECK(mh_net_session_leave(mh_network_session(host), NULL) ==
		MH_ERROR_NONE);
	pump_peers(host, clients, MH_NET_PLAYERS);

	/*
	 * No timeout-clock advance: the original type-17 packet closes clients.
	 */
	for (i = 0; i < MH_NET_PLAYERS - 1; i++)
		CHECK(mh_network_session(clients[i])->phase == MH_NET_CLOSED);

done:
	for (i = 0; i < MH_NET_PLAYERS; i++)
		mh_network_close(clients[i]);

	mh_network_close(host);
}

static void
check_wrong_password(const struct mh_transport_provider *provider)
{
	struct mh_net_session_config config;
	struct mh_network *host, *client;
	struct mh_net_session *session;

	host = client = NULL;
	session_config(&config, "Host");
	CHECK(mh_network_open_address(provider, 1, "127.0.0.1", NULL, 0,
		&config, &host, NULL) == MH_ERROR_NONE);

	if (host == NULL)
		return;

	strcpy(config.password, "Wrong");
	CHECK(mh_network_open_address(provider, 0, "127.0.0.1", "127.0.0.1",
		mh_network_port(host), &config, &client,
		NULL) == MH_ERROR_NONE);

	if (client != NULL) {
		pump_peers(host, &client, 1);
		session = mh_network_session(client);
		CHECK(session->phase == MH_NET_REJECTED &&
			session->rejection_reason == 2);
		CHECK(strcmp(session->rejection, "Invalid password!") == 0);
	}

	mh_network_close(client);
	mh_network_close(host);
}

int
main(void)
{
	static const struct mh_clock_ops ops = {
		clock_counter, clock_frequency, clock_ticks, NULL, NULL};

	static const struct mh_clock_source clock = {&ops, NULL};
	struct mh_udp_transport_config config;
	struct mh_transport_provider provider;

	memset(&config, 0, sizeof(config));
	config.clock = &clock;
#ifdef _WIN32
	config.resolver = mh_windows_resolver_provider();
#else
	config.resolver = mh_posix_resolver_provider();
#endif
	strcpy(config.name, "Fixture");
	ticks = 100;
	CHECK(mh_udp_transport_init(&provider, &config, NULL) == MH_ERROR_NONE);
	check_lobby(&provider);
	check_wrong_password(&provider);

	return failures != 0;
}
