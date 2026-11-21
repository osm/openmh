#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/network_capture.h"
#include "engine/demo/race.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "network capture line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_net_race race;
static struct mh_net_session session;
static struct mh_car car;
static struct mh_track track;

enum {
	LEGACY_MASK_LIMIT = 1U << MH_DEMO_PLAYERS,
	DEDICATED_DRIVER_MASK = (1U << MH_DEMO_MAX_PLAYERS) - 2,
	LAST_SLOT_MASK = 1U << (MH_DEMO_MAX_PLAYERS - 1)
};

int
main(void)
{
	struct mh_demo header, held_header;
	struct mh_demo_layout layout;
	struct mh_demo_record_sample sample, held;
	struct mh_demo_player crossings[MH_DEMO_MAX_PLAYERS];
	struct mh_net_vehicle vehicle;
	unsigned int mask, local, i, slot, cases, slots;
	double position[3] = {1, 2, 3};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

	memset(&car, 0, sizeof(car));
	strcpy(car.name, "Kobra");
	memset(&track, 0, sizeof(track));
	strcpy(track.name, "RedRock");
	track.laps = 3;
	cases = 0;

	for (mask = 1; mask < (1U << MH_DEMO_MAX_PLAYERS); mask++) {
		/*
		 * Full dedicated field, sparse high slots and a lone slot
		 * twelve.
		 */
		if (mask >= LEGACY_MASK_LIMIT &&
			mask != DEDICATED_DRIVER_MASK &&
			mask != (LAST_SLOT_MASK | 2) && mask != LAST_SLOT_MASK)
			continue;

		slots = mask < LEGACY_MASK_LIMIT ? MH_DEMO_PLAYERS
						: MH_DEMO_MAX_PLAYERS;

		for (local = 0; local < slots; local++) {
			if (!(mask & (1U << local)))
				continue;

			memset(&race, 0, sizeof(race));
			memset(&session, 0, sizeof(session));
			session.phase = MH_NET_LOADING;
			session.player = local;
			session.roster.player_count = slots;
			session.config.wire = slots == MH_DEMO_MAX_PLAYERS
				? MH_NET_WIRE_31_LOBBY
				: MH_NET_WIRE_F12;
			strcpy(session.roster.track, "RedRock");
			session.roster.laps = 3;

			for (i = 0; i < slots; i++) {
				session.roster.players[i].flags =
					(mask & (1U << i)) ? 1 : 0;
				session.roster.players[i].value_a = i + 1;
				session.roster.players[i].gear_mode = i % 2;
				session.roster.players[i].control_type =
					MH_U32_SIGN_BIT + i;
				sprintf(session.roster.players[i].name,
					"Player%u", i);
				strcpy(session.roster.players[i].team, "SMOB");
				sprintf(session.roster.players[i].horn,
					"Horn%u", i);
				strcpy(session.roster.players[i].car, "Kobra");
				session.roster.players[i].values[0] =
					0xabcdef00UL + i;
			}

			CHECK(mh_net_race_layout(&session, &race.layout,
				NULL) == MH_ERROR_NONE);
			session.phase = MH_NET_WAITING;
			race.field.network = 1;
			race.field.count = race.layout.count;
			race.field.active_mask = (1U << race.field.count) - 1;
			race.field.tuning.catchup = local % 2;

			for (i = 0; i < race.field.count; i++) {
				slot = race.layout.player[i];
				race.field.cars[i].car = &car;
				race.field.cars[i].track = &track;
				race.field.cars[i].remote = i != 0;
				race.field.cars[i].countdown = 1;
				race.field.cars[i].race.lap_limit = 3;
				race.field.cars[i].race.splits[3][2] =
					17000 + slot;
				CHECK(mh_motion_reset(position, rotation,
					&race.field.cars[i]
						.vehicle.motion,
					NULL) == MH_ERROR_NONE);
				race.samples[i].replica.vehicle.position[0] =
					100 + slot;
				race.samples[i].replica.vehicle.rpm = 2000;
				race.samples[i].replica.vehicle.flags = 0xfff;
				race.field.cars[i].sound_pedal = 1;
			}

			CHECK(mh_network_capture_header(&race, &session,
				&header, NULL) == MH_ERROR_NONE);
			CHECK(header.version == MH_DEMO_VERSION_30);
			CHECK(header.player_count == race.field.count &&
				header.local_player == local);
			CHECK(header.flags == local % 2 && header.laps == 3 &&
				strcmp(header.track, "RedRock") == 0);
			CHECK(mh_demo_race_layout(&header, 0, &layout, NULL) ==
					MH_ERROR_NONE &&
				layout.count == header.player_count);
			CHECK(layout.recording[0] == local);
			slot = 1;

			for (i = 0; i < slots; i++)
				if ((mask & (1U << i)) && i != local)
					CHECK(layout.recording[slot++] == i);

			CHECK(mh_demo_race_layout(&header, 1, &layout, NULL) ==
				MH_ERROR_NONE);
			CHECK(layout.recording[0] == MH_DEMO_HUMAN_SLOT &&
				layout.count == header.player_count + 1);
			slot = 1;

			for (i = 0; i < slots && slot < layout.count; i++)
				if (mask & (1U << i))
					CHECK(layout.recording[slot++] == i);

			for (i = 0; i < slots; i++) {
				CHECK(header.players[i].active ==
					((mask & (1U << i)) != 0));
				CHECK(strcmp(header.players[i].name,
					session.roster.players[i].name) ==
					0);
				CHECK(strcmp(header.players[i].horn,
					session.roster.players[i].horn) ==
					0);
				CHECK(header.players[i].colors[0] ==
					0xabcdef00UL + i);
				CHECK(header.players[i].gear_mode == i % 2 &&
					header.players[i].control_type ==
						MH_U32_SIGN_BIT + i);
				CHECK(header.players[i].crossings[3][2] == 0);
			}

			/*
			 * A changed session, including reuse of
			 * departed lobby slots, must not alter the
			 * frozen identity or source of any recorded
			 * body.
			 */
			memset(&session.roster, 0, sizeof(session.roster));
			race.field.active_mask = 1;
			race.field.unavailable_mask =
				(1U << race.field.count) - 1;
			CHECK(mh_network_capture_sample(&race, &header, &sample,
				NULL) == MH_ERROR_NONE);
			CHECK(sample.active_mask == (1U << local) &&
				sample.unavailable_mask == mask);

			for (i = 0; i < slots; i++)
				if (mask & (1U << i)) {
					CHECK(mh_net_vehicle_decode(
						sample.bodies[i], 30,
						&vehicle,
						NULL) == MH_ERROR_NONE);
					CHECK(vehicle.position[0] ==
						(i == local ? 1 : 100 + i));

					if (i != local)
						CHECK((vehicle.flags & 0xc00) ==
							0x400);
				} else {
					unsigned char zero[30];

					memset(zero, 0, sizeof(zero));
					CHECK(memcmp(sample.bodies[i], zero,
						30) == 0);
				}

			CHECK(mh_network_capture_crossings(
				&race, crossings, NULL) == MH_ERROR_NONE);

			for (i = 0; i < slots; i++)
				CHECK(crossings[i].crossings[3][2] ==
					((mask & (1U << i)) ? 17000 + i : 0));

			held = sample;
			held_header = header;
			CHECK(mh_network_capture_header(&race, &session,
				&header, NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&held_header, &header, sizeof(header)) ==
				0);
			race.layout.entry[local] = MH_NET_WIRE_PLAYERS;
			CHECK(mh_network_capture_sample(&race, &header, &sample,
				NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&sample, &held, sizeof(sample)) == 0);
			race.layout.entry[local] = 0;

			if (race.field.count > 1) {
				race.samples[1].replica.vehicle.position[0] =
					1e40;
				CHECK(mh_network_capture_sample(&race, &header,
					&sample, NULL) != MH_ERROR_NONE);
				CHECK(memcmp(&sample, &held, sizeof(sample)) ==
					0);
			}

			cases++;
		}
	}

	CHECK(cases == 1039);
	puts("Network capture: 1039 legacy/dedicated local-slot combinations, frozen identities, masks, bodies, crossings and atomic rejection pass");

	return 0;
}
