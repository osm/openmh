#include <stdio.h>
#include <string.h>

#include "../support/net_virtual_io.h"
#include "client/diagnostics/runtime_network.h"
#include "client/session/network_race.h"
#include "engine/core/binary.h"

#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "return line %d: %s\n", __LINE__,      \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_context context;
static struct mh_net_race race;
static struct mh_net_session saved;
static const unsigned int awards[MH_NET_PLAYERS] = {12, 10, 8, 6, 4, 3, 2, 1};

/* Patched/host/mode/local finished/viewed entry/waiting. */
static int
observe(struct mh_network *network, const unsigned int input[6],
	unsigned char output[64])
{
	struct mh_net_session *session;
	struct mh_network_race driving;
	struct mh_net_session_config config;
	unsigned int entry, slot, local, rank, scored;

	session = mh_network_session(network);
	config = session->config;
	config.wire = input[0] ? MH_NET_WIRE_31_LOBBY : MH_NET_WIRE_F12;
	config.mode = input[2];
	CHECK(mh_net_session_init(1, &config, session, NULL) == MH_ERROR_NONE);
	session->host = input[1];
	local = session->player = input[1] ? 0 : 3;
	session->phase = input[5] ? MH_NET_WAITING : MH_NET_RACING;
	session->replication.configured = 1;
	session->roster.player_count = MH_NET_PLAYERS;
	memset(&race, 0, sizeof(race));
	race.field.network = 1;
	race.field.count = race.layout.count = MH_NET_PLAYERS;
	race.field.active_mask = (1U << MH_NET_PLAYERS) - 1;
	race.field.viewed_entry = input[4];
	race.layout.player[0] = local;
	entry = 1;

	for (slot = 0; slot < MH_NET_PLAYERS; slot++) {
		if (slot != local)
			race.layout.player[entry++] = slot;

		session->roster.players[slot] = config.profile;
		session->roster.players[slot].flags = MH_NET_FLAG_RACING;
		session->roster.players[slot].values[MH_NET_PROFILE_RACES] =
			slot;
		session->roster.players[slot].values[MH_NET_PROFILE_POINTS] =
			100 * slot;
		session->lobby.slots[slot].profile =
			session->roster.players[slot];
		session->lobby.slots[slot].phase = MH_NET_IN_RACE;
		session->lobby.slots[slot].peer = slot + 1;
	}

	for (entry = 0; entry < race.field.count; entry++) {
		slot = race.layout.player[entry];
		race.layout.entry[slot] = entry;
		race.field.standings.cars[entry].position =
			MH_NET_PLAYERS - slot;
		race.field.cars[entry].race.lap = 2 + slot;
		race.field.cars[entry].race.finished =
			entry == 0 ? input[3] : !input[3];
	}

	context.results.count = MH_NET_PLAYERS;
	context.scene.assets.view_width = 640;
	CHECK(mh_network_race_init(network, &race, &context, &driving, NULL) ==
		MH_ERROR_NONE);
	CHECK(driving.race_hooks.restart != NULL);

	/* A failed send must not award scores or mark completion. */
	session->out_count = MH_NET_OUTBOX;

	for (slot = 0; slot < MH_NET_OUTBOX; slot++)
		session->outgoing[slot].reliable = 1;

	saved = *session;
	CHECK(driving.race_hooks.restart(&driving, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(session, &saved, sizeof(saved)) && !race.ended);
	session->out_count = 0;
	CHECK(driving.race_hooks.restart(&driving, NULL) == MH_ERROR_NONE);
	CHECK(session->phase == MH_NET_LOBBY);
	scored = input[0] && input[1] && input[3] && !input[5];
	CHECK(race.ended == (int)scored);

	for (slot = 0; slot < MH_NET_PLAYERS; slot++) {
		const struct mh_net_profile *profile;

		profile = &session->roster.players[slot];
		rank = MH_NET_PLAYERS - slot;
		CHECK(profile->values[MH_NET_PROFILE_RACES] == slot + scored);
		CHECK(profile->values[MH_NET_PROFILE_POINTS] ==
			100 * slot + (scored ? awards[rank - 1] : 0));
		mh_write_u32le(output + slot * 8,
			profile->values[MH_NET_PROFILE_RACES]);
		mh_write_u32le(output + slot * 8 + 4,
			profile->values[MH_NET_PROFILE_POINTS]);
	}

	CHECK(session->out_count != 0);
	saved = *session;
	CHECK(driving.race_hooks.restart(&driving, NULL) == MH_ERROR_NONE);
	CHECK(!memcmp(session, &saved, sizeof(saved)));

	return 0;
}

int
main(void)
{
	struct mh_net_session_config config;
	struct mh_network *network;
	unsigned char output[64];
	unsigned int values[6], sample, i;

	int result;

	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.car_count = 10;
	config.laps = 10;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.name, "Driver");
	strcpy(config.profile.car, "Kobra");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	CHECK(mh_network_open(1, "127.0.0.1", NULL, 1998, &config, &network,
		NULL) == MH_ERROR_NONE);
	result = 0;

	{
		for (sample = 0; sample < 64 && result == 0; sample++) {
			for (i = 0; i < 6; i++)
				values[i] = (sample >> i) & 1;

			result = observe(network, values, output);
		}
	}

	mh_network_close(network);

	return result;
}
