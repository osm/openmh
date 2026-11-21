#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/ui/lobby_settings.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "lobby-settings:%d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static const char *const cars[4] = {
	"Car Alpha", "Car Beta", "CAR BETA", "Car Delta"};
static const char *const tracks[4] = {
	"Track Alpha", "Track Beta", "TRACK BETA", "Track Delta"};
static const char *const horns[4] = {
	"Honker1", "Honker2", "HONKER2", "Honker4"};
static struct mh_net_session session;
static struct mh_horn_catalog horn_catalog;
static struct mh_lobby_settings_catalog catalog;

static void
init_fixture(struct mh_settings *settings)
{
	unsigned int i;

	memset(&session, 0, sizeof(session));
	memset(&horn_catalog, 0, sizeof(horn_catalog));
	horn_catalog.count = 4;

	for (i = 0; i < 4; i++)
		strcpy(horn_catalog.horns[i].name, horns[i]);

	catalog.cars.names = cars;
	catalog.cars.count = 4;
	catalog.tracks.names = tracks;
	catalog.tracks.count = 4;
	catalog.horns = &horn_catalog;
	mh_settings_init(
		settings, "Before", 100, 80, MH_SETTINGS_NO_RENDERER, NULL);
	strcpy(settings->team_name, "OLD");
	settings->record_race = 1;
	settings->game_type = 2;
	settings->catch_up = 1;
	session.host = 1;
	session.phase = MH_NET_CLOSED;
	session.config.laps = 20;
	session.config.profile.update_rate = 37;
	session.config.prediction.minimum = 8;
	session.config.prediction.maximum = 2;
	strcpy(session.config.track, "tRaCk BeTa");
	strcpy(session.config.profile.car, "cAr BeTa");
	strcpy(session.config.profile.horn, "hOnKeR2");
	strcpy(session.config.profile.name, "After");
	strcpy(session.config.profile.team, "NEW");

	for (i = 0; i < 4; i++)
		session.config.profile.values[i] = 0xabcdefUL + i;
}

static int
round_trip_laps(struct mh_settings *settings, unsigned long *seed)
{
	struct mh_settings restored;
	struct mh_settings_catalog settings_catalog;
	unsigned int laps;
	char encoded[MH_SETTINGS_TEXT_CAPACITY];
	size_t size;

	memset(&settings_catalog, 0, sizeof(settings_catalog));
	settings_catalog.cars = catalog.cars;
	settings_catalog.tracks = catalog.tracks;
	settings_catalog.horns.names = horns;
	settings_catalog.horns.count = 4;

	for (laps = MH_RACE_MIN_LAPS; laps <= MH_RACE_MAX_LAPS; laps++) {
		settings->laps = laps;
		CHECK(mh_settings_encode(settings, &settings_catalog, encoded,
			sizeof(encoded), &size, NULL) == MH_ERROR_NONE);
		CHECK(mh_settings_init(&restored, "Before", 100, 80,
			MH_SETTINGS_NO_RENDERER, NULL) == MH_ERROR_NONE);
		CHECK(mh_settings_parse((const unsigned char *)encoded, size,
			&settings_catalog, seed, &restored,
			NULL) == MH_ERROR_NONE);
		CHECK(restored.laps == laps);
	}

	return 0;
}

static int
local_profile_preferences(void)
{
	struct mh_settings settings, before, restored;
	struct mh_settings_catalog names;
	char text[MH_SETTINGS_TEXT_CAPACITY];
	unsigned long seed;
	size_t size;

	init_fixture(&settings);
	before = settings;
	seed = 123;
	session.config.profile.gear_mode = 1;
	CHECK(mh_lobby_settings_profile(&session, &catalog, &seed, &settings,
		NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(settings.player_name, "After") &&
		!strcmp(settings.team_name, "NEW"));
	CHECK(settings.car == 1 && settings.horn == 1 &&
		settings.automatic == 1);
	CHECK(settings.player_color == 0xabcdefUL &&
		settings.car_colors[2] == 0xabcdf2UL);
	CHECK(settings.tcp_rate == 37 && settings.prediction.minimum == 8);
	CHECK(settings.track == before.track && settings.laps == before.laps &&
		settings.game_type == before.game_type &&
		settings.record_race == before.record_race);
	CHECK(seed == 123);

	memset(&names, 0, sizeof(names));
	names.cars = catalog.cars;
	names.tracks = catalog.tracks;
	names.horns.names = horns;
	names.horns.count = 4;
	CHECK(mh_settings_encode(&settings, &names, text, sizeof(text), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_init(&restored, "Before", 100, 80,
		MH_SETTINGS_NO_RENDERER, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_parse((const unsigned char *)text, size, &names,
		&seed, &restored, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(restored.player_name, "After") &&
		!strcmp(restored.team_name, "NEW"));
	CHECK(restored.player_color == settings.player_color &&
		restored.car == settings.car &&
		restored.horn == settings.horn &&
		restored.automatic == settings.automatic);
	before = settings;
	session.config.profile.values[0] = 0x1000000UL;
	CHECK(mh_lobby_settings_profile(&session, &catalog, &seed, &settings,
		NULL) == MH_ERROR_INVALID);
	CHECK(!memcmp(&settings, &before, sizeof(settings)));

	return 0;
}

int
main(void)
{
	struct mh_settings settings, saved_settings, expected;
	unsigned long seed;
	unsigned int player, i;

	CHECK(local_profile_preferences() == 0);

	for (player = 0; player <= 8; player++) {
		init_fixture(&settings);
		saved_settings = expected = settings;
		seed = 123;
		session.host = player == 0;
		session.player = player;
		session.roster.player_count = 8;
		session.roster.laps = 7;
		strcpy(session.roster.track, "Track Delta");

		/* An old roster must not overwrite the local edited profile. */
		strcpy(session.roster.players[player < 8 ? player : 0].name,
			"Stale");
		CHECK(mh_lobby_settings_return(&session, &catalog, &seed,
			&settings, NULL) == MH_ERROR_NONE);

		if (player == 8) {
			CHECK(!memcmp(
				&settings, &saved_settings, sizeof(settings)));
			continue;
		}

		strcpy(expected.player_name, "After");
		strcpy(expected.team_name, "NEW");
		expected.car = 1;
		expected.track = player == 0 ? 1 : 3;
		expected.horn = 1;
		expected.automatic = 0;
		expected.laps = player == 0 ? 20 : 7;
		expected.tcp_rate = 37;
		expected.prediction.minimum = 8;
		expected.prediction.maximum = 2;
		expected.player_color = 0xabcdefUL;

		for (i = 0; i < 3; i++)
			expected.car_colors[i] = 0xabcdf0UL + i;

		CHECK(!memcmp(&settings, &expected, sizeof(settings)) &&
			seed == 123);
	}

	/*
	 * Dedicated driver twelve persists settings; unassigned thirteen does
	 * not.
	 */
	init_fixture(&settings);
	session.config.wire = MH_NET_WIRE_31_LOBBY;
	session.host = 0;
	session.player = 12;
	session.roster.player_count = MH_NET_WIRE_PLAYERS;
	session.roster.laps = 7;
	strcpy(session.roster.track, "Track Delta");
	seed = 123;
	CHECK(mh_lobby_settings_return(&session, &catalog, &seed, &settings,
		NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(settings.player_name, "After") && settings.laps == 7);
	saved_settings = settings;
	session.player = mh_net_session_capacity(&session);
	strcpy(session.config.profile.name, "Unassigned");
	CHECK(mh_lobby_settings_return(&session, &catalog, &seed, &settings,
		NULL) == MH_ERROR_NONE);
	CHECK(!memcmp(&settings, &saved_settings, sizeof(settings)));

	init_fixture(&settings);
	saved_settings = settings;
	seed = 123;
	strcpy(session.config.profile.horn, "bad");
	CHECK(mh_lobby_settings_return(&session, &catalog, &seed, &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&settings, &saved_settings, sizeof(settings)) &&
		seed == 123);
	init_fixture(&settings);
	saved_settings = settings;
	memset(session.config.profile.name, 'x', 16);
	CHECK(mh_lobby_settings_return(&session, &catalog, &seed, &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&settings, &saved_settings, sizeof(settings)));
	init_fixture(&settings);
	saved_settings = settings;
	session.config.profile.values[3] = 0x1000000UL;
	CHECK(mh_lobby_settings_return(&session, &catalog, &seed, &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&settings, &saved_settings, sizeof(settings)));
	init_fixture(&settings);
	saved_settings = settings;
	catalog.tracks.count = 0;
	CHECK(mh_lobby_settings_return(&session, &catalog, &seed, &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&settings, &saved_settings, sizeof(settings)));
	init_fixture(&settings);
	session.host = 0;
	session.player = 3;
	CHECK(mh_lobby_settings_return(&session, &catalog, &seed, &settings,
		NULL) == MH_ERROR_NONE);

	/* Accepted, awaiting roster. */
	CHECK(settings.track == 1 && settings.laps == 20);
	init_fixture(&settings);
	strcpy(session.config.profile.car, "Absent");
	strcpy(session.config.track, "Absent");
	settings.car = settings.track = 3;
	CHECK(mh_lobby_settings_return(&session, &catalog, &seed, &settings,
		NULL) == MH_ERROR_NONE);
	CHECK(settings.car == 3 && settings.track == 3);

	if (round_trip_laps(&settings, &seed) != 0)
		return 1;

	puts("Lobby settings: local ownership, return fields, atomic errors and intermediate-lap save/reload passed");

	return 0;
}
