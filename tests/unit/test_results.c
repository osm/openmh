#include <stdio.h>
#include <string.h>

#include "engine/assets/profile.h"
#include "engine/catalog/profile.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/results.h"
#include "interfaces/audio/horn.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static enum mh_error_code
initialize_adp(const unsigned char *text, size_t size,
	const struct mh_horn_catalog *horns, unsigned long *random,
	struct mh_profile *output, struct mh_error *error_out)
{
	struct mh_profile definition;
	enum mh_error_code status;

	status = mh_profile_adp_decode(text, size, &definition, error_out);

	return status == MH_ERROR_NONE
		? mh_profile_initialize(
			&definition, horns, random, output, error_out)
		: status;
}

static void
initialized_profile(void)
{
	static const char *base =
		"PlayerName Driver\nPlayerNick D\nPlayerPicture driver.tga\n";
	struct mh_horn_catalog horns;
	struct mh_profile profile, saved;
	unsigned long random;
	char text[256];

	memset(&horns, 0, sizeof(horns));
	horns.count = 2;
	strcpy(horns.horns[0].name, "Honker1");
	strcpy(horns.horns[1].name, "Honker2");
	random = 1;
	CHECK(initialize_adp((const unsigned char *)base, strlen(base), &horns,
		&random, &profile, NULL) == MH_ERROR_NONE);
	CHECK(random == 1103527590UL && strcmp(profile.horn, "Honker1") == 0);
	CHECK((profile.league_fields & 16) == 0);
	strcpy(text, base);
	strcat(text, "CarHorn honker2\n");
	random = 1;
	CHECK(initialize_adp((const unsigned char *)text, strlen(text), &horns,
		&random, &profile, NULL) == MH_ERROR_NONE);
	CHECK(random == 1103527590UL && strcmp(profile.horn, "Honker2") == 0);
	CHECK((profile.league_fields & 16) != 0);
	saved = profile;
	random = 1;
	strcpy(text, base);
	strcat(text, "CarHorn bad\n");
	CHECK(initialize_adp((const unsigned char *)text, strlen(text), &horns,
		&random, &profile, NULL) != MH_ERROR_NONE);
	CHECK(random == 1 && memcmp(&profile, &saved, sizeof(profile)) == 0);
	CHECK(initialize_adp((const unsigned char *)"invalid", 7, &horns,
		&random, &profile, NULL) != MH_ERROR_NONE);
	CHECK(random == 1 && memcmp(&profile, &saved, sizeof(profile)) == 0);
}

static void
profile(void)
{
	struct mh_profile profile, before;
	char long_line[1025];
	static const unsigned char good[] =
		"// profile\r\n PlayerName  A Driver \t// name\r\n"
		"pLaYeRnIcK\tAD\nPlayerPicture driver.tga\nSkill 99\n"
		"CarDivision0 Serpent\nCarDivision1 Avalanche\nCarDivision2 ASC II\nCarDivision3 ASC\nCarHorn Horn3\nPlayerColour 32 255 64\nCarColour1 15 122 207\nCarColour2 27 15 142\nCarColour3 240 240 240\nAggressivness 86\nEagerness 85";
	static const char *bad[] = {"PlayerName A\nPlayerNick B",
		"PlayerName A\nPlayerNick B\nPlayerPicture ../x.tga",
		"PlayerName A\nPlayerNick B\nPlayerPicture dir/x.tga",
		"PlayerName A\nPlayerNick B\nPlayerPicture C:\\x.tga",
		"PlayerName A\nPlayerNick 1234567890123456\nPlayerPicture x.tga",
		"PlayerName A\nPlayerNick B\nPlayerPicture x.tga\nPlayerNick C",
		"PlayerName \nPlayerNick B\nPlayerPicture x.tga",
		"PlayerName A\nPlayerNick B\nPlayerPicture x.tga\nCarHorn H\nCarHorn H",
		"PlayerName A\nPlayerNick B\nPlayerPicture x.tga\nCarDivision0 ",
		"PlayerName A\nPlayerNick B\nPlayerPicture x.tga\nPlayerColour 256 0 0",
		"PlayerName A\nPlayerNick B\nPlayerPicture x.tga\nCarColour1 -1 0 0",
		"PlayerName A\nPlayerNick B\nPlayerPicture x.tga\nCarColour2 1 2 3 4",
		"PlayerName A\nPlayerNick B\nPlayerPicture x.tga\nCarColour3 1x 2 3",
		"PlayerName A\nPlayerNick B\nPlayerPicture x.tga\nPlayerColour 1\nPlayerColour 2"};

	unsigned int i;

	CHECK(mh_profile_adp_decode(good, sizeof(good) - 1, &profile, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(profile.name, "A Driver") == 0 &&
		strcmp(profile.nick, "AD") == 0 &&
		strcmp(profile.picture, "driver.tga") == 0);
	CHECK(profile.league_fields == 31 &&
		strcmp(profile.cars[2], "ASC II") == 0 &&
		strcmp(profile.horn, "Horn3") == 0);
	CHECK(profile.tuning_fields == 2 &&
		profile.eagerness == (double)(float)0.85 &&
		profile.aggressiveness == 0.5);
	CHECK(profile.color_fields == 15 && profile.colors[0] == 0x20ff40UL &&
		profile.colors[1] == 0x0f7acfUL &&
		profile.colors[2] == 0x1b0f8eUL &&
		profile.colors[3] == 0xf0f0f0UL);
	before = profile;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(mh_profile_adp_decode((const unsigned char *)bad[i],
			strlen(bad[i]), &profile, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&profile, &before, sizeof(profile)) == 0);
	}

	memset(long_line, 'x', sizeof(long_line));
	CHECK(mh_profile_adp_decode((const unsigned char *)long_line,
		sizeof(long_line), &profile, NULL) != MH_ERROR_NONE);

	/* The supplied byte range includes a NUL. */
	CHECK(mh_profile_adp_decode(good, sizeof(good), &profile, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&profile, &before, sizeof(profile)) == 0);

	for (i = 127; i < 256; i++) {
		sprintf(long_line,
			"PlayerName A%c\nPlayerNick %cB\nPlayerPicture driver.tga\n",
			(int)i, (int)i);
		CHECK(mh_profile_adp_decode((const unsigned char *)long_line,
			strlen(long_line), &profile,
			NULL) == MH_ERROR_NONE);
		CHECK((unsigned char)profile.name[1] == i &&
			(unsigned char)profile.nick[0] == i);
	}
}

static void
profile_tuning(void)
{
	static const char base[] =
		"PlayerName A\nPlayerNick B\nPlayerPicture a.tga\n";
	static const char *values[] = {"-1", "0", "50", "100", "101"};
	static const double expected[] = {0, 0, 0.5, 1, 1};
	static const char *bad[] = {"nan", "inf", "1e999", "50junk", ""};
	struct mh_profile profile, saved;
	char text[256];
	unsigned int i;

	CHECK(mh_profile_adp_decode((const unsigned char *)base, strlen(base),
		&profile, NULL) == MH_ERROR_NONE);
	CHECK(profile.color_fields == 0 && profile.colors[0] == 0xffcc88UL &&
		profile.colors[3] == 0xffff00UL);
	CHECK(profile.tuning_fields == 0 && profile.aggressiveness == 0.5 &&
		profile.eagerness == 0.5);

	for (i = 0; i < 5; i++) {
		sprintf(text, "%sAggressiveness %s\nEagerness %s", base,
			values[i], values[4 - i]);
		CHECK(mh_profile_adp_decode((const unsigned char *)text,
			strlen(text), &profile, NULL) == MH_ERROR_NONE);
		CHECK(profile.tuning_fields == 3 &&
			profile.aggressiveness == expected[i] &&
			profile.eagerness == expected[4 - i]);
	}

	saved = profile;

	for (i = 0; i < 5; i++) {
		sprintf(text, "%sAggressiveness %s", base, bad[i]);
		CHECK(mh_profile_adp_decode((const unsigned char *)text,
			strlen(text), &profile, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&profile, &saved, sizeof(profile)) == 0);
	}

	sprintf(text, "%sEagerness 1\nEagerness 2", base);
	CHECK(mh_profile_adp_decode((const unsigned char *)text, strlen(text),
		&profile, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&profile, &saved, sizeof(profile)) == 0);
}

static void
screen_state(void)
{
	struct mh_standings standings;
	struct mh_standing_sample samples[3];
	struct mh_results_screen screen;
	struct mh_results_row row;

	memset(samples, 0, sizeof(samples));
	memset(&screen, 0, sizeof(screen));
	CHECK(mh_standings_rank(samples, 3, &standings, NULL) == MH_ERROR_NONE);
	mh_results_screen_update(&screen, &standings);
	CHECK(!screen.visible);
	screen.visible = 1;
	CHECK(mh_results_screen_pauses(&screen, &standings));
	mh_results_row(&standings.cars[0], &row);
	CHECK(strcmp(row.race, "Racing") == 0 && strcmp(row.score, "--") == 0 &&
		strcmp(row.best, " --") == 0);
	standings.cars[0].estimated = 1;
	standings.cars[0].best_ms = 103338;
	mh_results_row(&standings.cars[0], &row);
	CHECK(strcmp(row.best, " 1:43,33") == 0);

	samples[1].finished = 1;
	samples[1].total_ms = 214179;
	samples[1].score = 99999;
	samples[1].best_ms = 33909;
	samples[1].best_lap = 2;
	CHECK(mh_standings_rank(samples, 3, &standings, NULL) == MH_ERROR_NONE);

	/* AI winner does not unpause preview. */
	CHECK(mh_results_screen_pauses(&screen, &standings));
	mh_results_row(&standings.cars[1], &row);
	CHECK(strcmp(row.score, "12") == 0 &&
		strcmp(row.race, " 3:34,17") == 0 &&
		strcmp(row.best, " 0:33,90") == 0);
	screen.visible = 0;
	samples[0] = samples[1];
	samples[0].total_ms += 2479;
	CHECK(mh_standings_rank(samples, 3, &standings, NULL) == MH_ERROR_NONE);
	mh_results_screen_update(&screen, &standings);
	CHECK(screen.visible && screen.finish_seen &&
		!mh_results_screen_pauses(&screen, &standings));
	mh_results_row(&standings.cars[0], &row);
	CHECK(strcmp(row.score, "10") == 0 &&
		strcmp(row.race, "+0:02,47") == 0);
	screen.visible = 0;
	mh_results_screen_update(&screen, &standings);
	CHECK(!screen.visible);
	samples[2] = samples[0];
	samples[2].total_ms++;
	CHECK(mh_standings_rank(samples, 3, &standings, NULL) == MH_ERROR_NONE);
	mh_results_screen_update(&screen, &standings);
	CHECK(!screen.visible);
	screen.visible = 1;
	CHECK(mh_results_screen_pauses(&screen, &standings));
	standings.cars[0].gap_ms = -12;
	mh_results_row(&standings.cars[0], &row);
	CHECK(strcmp(row.race, "-0:00,01") == 0);
	memset(&screen, 0, sizeof(screen));
	memset(samples, 0, sizeof(samples));
	CHECK(mh_standings_rank(samples, 3, &standings, NULL) == MH_ERROR_NONE);
	mh_results_screen_update(&screen, &standings);
	CHECK(!screen.visible && !screen.finish_seen);
}

static void
local_finish(void)
{
	struct mh_results_screen screen, saved;
	struct mh_standings standings;

	memset(&screen, 0, sizeof(screen));
	memset(&standings, 0, sizeof(standings));
	standings.count = 2;
	standings.cars[1].finished = 1;
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 50000, NULL) == MH_ERROR_NONE);
	CHECK(!screen.visible && screen.finish_anchor_ms == 0);
	standings.cars[0].finished = 1;
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 50000.75, NULL) == MH_ERROR_NONE);
	CHECK(!screen.visible && screen.finish_anchor_ms == 25000);
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 60000, NULL) == MH_ERROR_NONE &&
		!screen.visible);
	CHECK(mh_results_screen_update_local(&screen, &standings, 1, 60000.01,
		NULL) == MH_ERROR_NONE &&
		screen.visible);
	screen.visible = 0;
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 70000, NULL) == MH_ERROR_NONE &&
		!screen.visible);
	saved = screen;
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, -1, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&screen, &saved, sizeof(screen)) == 0);
	memset(&screen, 0, sizeof(screen));
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 25000, NULL) == MH_ERROR_NONE);
	CHECK(screen.finish_anchor_ms == 0 && !screen.visible);
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 25001, NULL) == MH_ERROR_NONE);
	CHECK(screen.finish_anchor_ms == 1 && !screen.visible);
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 35001, NULL) == MH_ERROR_NONE &&
		!screen.visible);
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 35002, NULL) == MH_ERROR_NONE &&
		screen.visible);
	memset(&screen, 0, sizeof(screen));
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 100.5, NULL) == MH_ERROR_NONE);
	CHECK(screen.finish_anchor_ms == 4294942397UL);
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 10101, NULL) == MH_ERROR_NONE &&
		!screen.visible);
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 1, 10101.1, NULL) == MH_ERROR_NONE &&
		screen.visible);
	memset(&screen, 0, sizeof(screen));
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 0, 50000, NULL) == MH_ERROR_NONE);
	CHECK(screen.finish_anchor_ms == 0 && !screen.visible);
	saved = screen;
	CHECK(mh_results_screen_update_local(
		&screen, &standings, 2, 50000, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&screen, &saved, sizeof(screen)) == 0);
}

static void
local_finish_frame(void)
{
	static struct mh_field field, saved, expected;
	struct mh_results_screen screen, saved_screen;
	struct mh_drive_frame sample;
	unsigned int i;

	memset(&field, 0, sizeof(field));
	memset(&screen, 0, sizeof(screen));
	field.count = field.standings.count = 2;
	field.active_mask = 3;
	field.standings.cars[0].finished = 1;
	field.scene_seconds = 37;
	field.ai_random = 123;
	field.accumulator = .125;

	for (i = 0; i < 2; i++) {
		field.cars[i].frame.active = 1;
		field.cars[i].frame.seconds = field.cars[i].seconds = 50;
		field.cars[i].frame.time.now_ms = 50000;
		field.cars[i].frame.intro_ms = 1;
		field.cars[i].frame.time.go_ms = 12002;
		field.cars[i].countdown = 1;
		field.cars[i].start_ticks = 1680;
		field.cars[i].ticks = 450;
	}

	saved = field;
	CHECK(mh_results_screen_frame(&screen, &field, .25L, NULL) ==
		MH_ERROR_NONE);
	CHECK(screen.finish_anchor_ms == 25250 && !screen.finish_seen);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0);

	/*
	 * Preview alone never consumes the frame. Its owner later commits the
	 * interval (normal physics is deliberately absent in this clock test).
	 */
	CHECK(mh_field_clock_advance(&field, .25L, NULL) == MH_ERROR_NONE);
	CHECK(mh_results_screen_frame(&screen, &field, 0, NULL) ==
			MH_ERROR_NONE &&
		!screen.visible);
	CHECK(mh_field_clock_advance(&field, 10, NULL) == MH_ERROR_NONE);
	saved = field;
	CHECK(mh_results_screen_frame(&screen, &field, 0, NULL) ==
			MH_ERROR_NONE &&
		!screen.visible);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0);

	/*
	 * The deadline sees the new clock and ends this very frame. A late-car
	 * invalid clock must leave both the board and all field state intact.
	 */
	field.cars[1].seconds = -1;
	saved = field;
	saved_screen = screen;
	CHECK(mh_results_screen_frame(&screen, &field, .001L, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0 &&
		memcmp(&screen, &saved_screen, sizeof(screen)) == 0);
	field.cars[1].seconds = field.cars[0].seconds;
	saved = expected = field;

	for (i = 0; i < 2; i++) {
		CHECK(mh_drive_frame_sample(&field.cars[i], .001L, &sample,
			NULL) == MH_ERROR_NONE);
		expected.cars[i].frame = sample;
		expected.cars[i].seconds =
			(double)((long double)expected.cars[i].seconds + .001L);
	}

	CHECK(mh_results_screen_frame(&screen, &field, .001L, NULL) ==
			MH_ERROR_NONE &&
		screen.visible && screen.finish_seen);
	CHECK(screen.finish_clock_ms ==
		(long double)field.cars[0].frame.seconds * 1000.0L);
	CHECK(memcmp(&field, &expected, sizeof(field)) == 0);

	/*
	 * Rendering/dismissing a latched board cannot advance the race twice.
	 */
	saved = field;
	screen.visible = 0;
	CHECK(mh_results_screen_frame(&screen, &field, 0, NULL) ==
			MH_ERROR_NONE &&
		!screen.visible);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0);
}

static void
prepared_results(void)
{
	static struct mh_field field, before;
	struct mh_results_screen screen, saved;
	struct mh_route route;
	struct mh_scene_key keys[2];
	struct mh_standing_sample samples[2];
	struct mh_results_row row;
	unsigned long seed;

	memset(&field, 0, sizeof(field));
	memset(&screen, 0, sizeof(screen));
	memset(samples, 0, sizeof(samples));
	field.count = 2;
	field.active_mask = 3;
	memset(&route, 0, sizeof(route));
	memset(keys, 0, sizeof(keys));
	route.motion.keys = keys;
	route.motion.key_count = 2;
	keys[1].time = 100;
	field.cars[0].race.route = &route;
	field.cars[0].race.lap_limit = 3;
	field.cars[1].race.lap_limit = 3;
	field.cars[1].race.parameter = 25;
	field.cars[0].race.finished = 1;
	field.cars[0].race.lap = 4;
	field.cars[0].race.now_ms = 100000;
	field.cars[0].race.best_ms = 30000;
	field.cars[1].race.lap = 3;
	field.cars[1].race.now_ms = 90000;
	field.cars[1].race.best_ms = 30000;
	samples[0].finished = 1;
	samples[0].total_ms = 100000;
	samples[1].total_ms = 90000;
	CHECK(mh_standings_rank(samples, 2, &field.standings, NULL) ==
		MH_ERROR_NONE);
	screen.finish_seen = screen.visible = 1;
	seed = 1;
	before = field;
	CHECK(mh_results_screen_prepare(&screen, &field, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(screen.prepared && seed == 1103527590UL);
	CHECK(screen.final.cars[1].estimated && !screen.final.cars[1].finished);
	CHECK(screen.final.cars[1].finish_ms == 103338 &&
		screen.final.cars[1].gap_ms == 3338);
	CHECK(!screen.final.complete && screen.final.finished_count == 1);
	CHECK(memcmp(&field, &before, sizeof(field)) == 0);
	CHECK(mh_results_screen_pauses(&screen, &field.standings));
	mh_results_row(&screen.final.cars[1], &row);
	CHECK(strcmp(row.score, "10") == 0 &&
		strcmp(row.race, "+0:03,33") == 0);
	screen.visible = 0;
	saved = screen;
	field.cars[1].race.now_ms = 999999;
	CHECK(mh_results_screen_prepare(&screen, &field, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(seed == 1103527590UL &&
		memcmp(&screen, &saved, sizeof(screen)) == 0);
	CHECK(!mh_results_screen_pauses(&screen, &field.standings));
	memset(&screen, 0, sizeof(screen));
	saved = screen;
	CHECK(mh_results_screen_prepare(&screen, &field, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1103527590UL &&
		memcmp(&screen, &saved, sizeof(screen)) == 0);
}

static void
aborted_results(void)
{
	static struct mh_field field, before;
	struct mh_results_screen screen, saved;
	struct mh_standing_sample samples[3];
	struct mh_results_row row;
	unsigned long seed;
	unsigned int i;

	memset(&field, 0, sizeof(field));
	memset(&screen, 0, sizeof(screen));
	memset(samples, 0, sizeof(samples));
	field.count = 3;
	field.active_mask = 7;

	for (i = 0; i < 3; i++) {
		field.cars[i].race.lap = 1;
		field.cars[i].race.lap_limit = 3;
		field.cars[i].race.now_ms = 90000 + 1000 * i;
		samples[i].total_ms = field.cars[i].race.now_ms;
		samples[i].score = 3 - i;
	}

	CHECK(mh_standings_rank(samples, 3, &field.standings, NULL) ==
		MH_ERROR_NONE);
	before = field;
	seed = 1;
	CHECK(mh_results_screen_abort(&screen, &field, &seed, NULL) ==
		MH_ERROR_NONE);

	/* Abort result fixture. */
	CHECK(screen.visible && screen.finish_seen && screen.prepared &&
		screen.aborted && seed == 2633739833UL);
	CHECK(screen.final.cars[0].position == 3 && screen.final.order[2] == 0);
	CHECK(screen.final.cars[1].position == 2 && screen.final.order[1] == 1);
	CHECK(screen.final.cars[2].position == 1 && screen.final.order[0] == 2);
	CHECK(screen.final.cars[0].finish_ms == 142545 &&
		screen.final.cars[0].gap_ms == 0);
	CHECK(screen.final.cars[1].finish_ms == 145272 &&
		screen.final.cars[1].gap_ms == 2727);
	CHECK(screen.final.cars[2].finish_ms == 142545 &&
		screen.final.cars[2].gap_ms == 0);
	CHECK(screen.final.cars[0].best_ms == 0 &&
		screen.final.cars[1].best_ms == 47914 &&
		screen.final.cars[2].best_ms == 46964);
	CHECK(!screen.final.complete && screen.final.finished_count == 0);

	for (i = 0; i < 3; i++)
		CHECK(screen.final.cars[i].estimated &&
			!screen.final.cars[i].finished);

	CHECK(memcmp(&field, &before, sizeof(field)) == 0);
	CHECK(mh_results_screen_pauses(&screen, &field.standings));
	mh_results_row(&screen.final.cars[0], &row);
	CHECK(strcmp(row.score, "8") == 0 && strcmp(row.race, "+0:00,00") == 0);
	saved = screen;
	CHECK(mh_results_screen_abort(&screen, &field, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_results_screen_prepare(&screen, &field, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(seed == 2633739833UL &&
		memcmp(&screen, &saved, sizeof(screen)) == 0);
	memset(&screen, 0, sizeof(screen));
	saved = screen;
	seed = 1;
	field.cars[1].race.finished = 2;
	before = field;

	/* Failed preparation must preserve the physical field and RNG. */
	CHECK(mh_results_screen_abort(&screen, &field, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(&screen, &saved, sizeof(screen)) == 0);
	CHECK(memcmp(&field, &before, sizeof(field)) == 0);
	field.cars[1].race.finished = 0;
	field.active_mask = 5;
	CHECK(mh_results_screen_abort(&screen, &field, &seed, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(&screen, &saved, sizeof(screen)) == 0);

	/*
	 * Departures leave holes in demo/ghost rosters. Literal original abort
	 * indexing assigns rank one to departed row one, and rank two twice.
	 */
	CHECK(mh_standings_rank_active(samples, 3, 5, &field.standings, NULL) ==
		MH_ERROR_NONE);

	for (i = 3; i <= 6; i += 3) {
		field.playback = (int)i;
		before = field;
		memset(&screen, 0, sizeof(screen));
		seed = 1;
		CHECK(mh_results_screen_abort(&screen, &field, &seed, NULL) ==
			MH_ERROR_NONE);
		CHECK(screen.prepared && seed == 3295386429UL);
		CHECK(screen.final.cars[0].position == 2 &&
			screen.final.cars[2].position == 2);
		CHECK(screen.final.cars[0].finish_ms == 137274 &&
			screen.final.cars[2].finish_ms == 137274);
		CHECK(memcmp(screen.final.order, field.standings.order,
			sizeof(screen.final.order)) == 0);
		CHECK(memcmp(&field, &before, sizeof(field)) == 0);
	}

	/*
	 * Patched abort preparation needs no leader, even with a finished local
	 * car and a departed row assigned rank one. No fallback rank rewrite.
	 */
	field.cars[0].race.finished = 1;
	before = field;
	memset(&screen, 0, sizeof(screen));
	seed = 1;
	CHECK(mh_results_screen_abort(&screen, &field, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(seed == 3295386429UL && screen.prepared);
	CHECK(screen.final.cars[0].position == 2 &&
		screen.final.cars[2].position == 2);
	CHECK(screen.final.cars[0].finish_ms == 137274 &&
		screen.final.cars[2].finish_ms == 137274);
	CHECK(memcmp(&field, &before, sizeof(field)) == 0);
	saved = screen;
	CHECK(mh_results_screen_abort(&screen, &field, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(seed == 3295386429UL &&
		memcmp(&screen, &saved, sizeof(screen)) == 0);
}

static void
prepare_glyphs(struct mh_results_assets *assets)
{
	unsigned int glyph;

	for (glyph = 0; glyph < 256; glyph++) {
		assets->advance[glyph] = 6;
		assets->glyphs[glyph].width = 6;
		assets->glyphs[glyph].height = 19;
		assets->glyphs[glyph].x = glyph % 16 * 6;
		assets->glyphs[glyph].y = 600 + glyph / 16 * 19;
	}
}

static void
layout(void)
{
	struct mh_results_assets assets;
	struct mh_ui_atlas atlas;
	struct mh_standings standings, before;
	static struct mh_ui_list list, scaled;
	unsigned int i, vertex, portraits, entry, x, y;

	memset(&assets, 0, sizeof(assets));
	memset(&atlas, 0, sizeof(atlas));
	memset(&standings, 0, sizeof(standings));
	atlas.image.width = atlas.image.height = 1024;
	assets.count = standings.count = 8;
	assets.background.width = 640;
	assets.background.height = 400;

	prepare_glyphs(&assets);

	for (i = 0; i < 8; i++) {
		assets.portraits[i].x = 90 * i;
		assets.portraits[i].y = 500;
		assets.portraits[i].width = 90;
		assets.portraits[i].height = 56;
		memset(assets.profiles[i].name, 'W', 63);
		memset(assets.profiles[i].nick, 'W', 15);
		standings.order[i] = 7 - i;
		standings.cars[7 - i].position = i + 1;
	}

	before = standings;
	CHECK(mh_results_draw(&assets, &atlas, &standings, 640, 400,
		"Tab/Enter: Back  Home: Restart", &list));
	CHECK(mh_results_draw(&assets, &atlas, &standings, 960, 640,
		"Tab/Enter: Back  Home: Restart", &scaled));
	CHECK(memcmp(&standings, &before, sizeof(standings)) == 0 &&
		list.count == scaled.count);
	CHECK(list.count > 256 * 6 && list.count <= MH_UI_MAX_QUADS * 6);
	portraits = 0;

	for (vertex = 0; vertex < list.count; vertex += 6) {
		CHECK(scaled.vertices[vertex].position[0] ==
			list.vertices[vertex].position[0] * 1.5f);
		CHECK(scaled.vertices[vertex].position[1] ==
			list.vertices[vertex].position[1] * 1.5f + 20);

		if (list.vertices[vertex].uv[1] == 500.0f / 1024) {
			entry = (unsigned int)(list.vertices[vertex].uv[0] *
					1024) /
				90;
			i = 7 - entry;
			x = i % 2 ? 327 : 20;
			y = 22 + i / 2 * 80 + i % 2 * 40;
			CHECK(list.vertices[vertex].position[0] == (float)x &&
				list.vertices[vertex].position[1] == (float)y);
			portraits++;
		}
	}

	CHECK(portraits == 8);
	CHECK(mh_results_draw(
		&assets, &atlas, &standings, 320, 480, NULL, &scaled));
	CHECK(scaled.vertices[0].position[0] == 0 &&
		scaled.vertices[0].position[1] == 140);
	standings.order[0] = standings.order[1];
	CHECK(!mh_results_draw(
		&assets, &atlas, &standings, 640, 400, NULL, &list));
	standings = before;
	standings.order[0] = 8;
	CHECK(!mh_results_draw(
		&assets, &atlas, &standings, 640, 400, NULL, &list));
	standings = before;
	standings.count = 0;
	CHECK(!mh_results_draw(
		&assets, &atlas, &standings, 640, 400, NULL, &list));
}

static void
season_layout(void)
{
	struct mh_results_assets assets, before;
	struct mh_ui_atlas atlas;
	static struct mh_ui_list list, scaled;
	unsigned long points[8];
	unsigned int i, vertex, portraits, digits, entry;

	memset(&assets, 0, sizeof(assets));
	memset(&atlas, 0, sizeof(atlas));
	atlas.image.width = atlas.image.height = 1024;
	assets.count = 8;
	assets.background.width = 640;
	assets.background.height = 400;

	prepare_glyphs(&assets);

	for (i = 0; i < 8; i++) {
		strcpy(assets.profiles[i].name, "Driver");
		strcpy(assets.profiles[i].nick, "Nick");
		assets.portraits[i].x = i * 90;
		assets.portraits[i].y = 500;
		assets.portraits[i].width = 90;
		assets.portraits[i].height = 56;
		points[i] = 12 + i;
	}

	points[0] = MH_U32_MAX;
	before = assets;
	CHECK(mh_results_season_draw(
		&assets, &atlas, points, 640, 400, NULL, &list));
	CHECK(mh_results_season_draw(
		&assets, &atlas, points, 960, 640, NULL, &scaled));
	CHECK(memcmp(&assets, &before, sizeof(assets)) == 0 &&
		list.count == scaled.count);
	portraits = digits = 0;

	for (vertex = 0; vertex < list.count; vertex += 6) {
		CHECK(scaled.vertices[vertex].position[0] ==
			list.vertices[vertex].position[0] * 1.5f);
		CHECK(scaled.vertices[vertex].position[1] ==
			list.vertices[vertex].position[1] * 1.5f + 20);

		if (list.vertices[vertex].uv[1] == 500.0f / 1024) {
			entry = (unsigned int)(list.vertices[vertex].uv[0] *
					1024) /
				90;
			CHECK(entry < 8);
			CHECK(list.vertices[vertex].position[0] ==
				(entry % 2 ? 327.0f : 20.0f));
			CHECK(list.vertices[vertex].position[1] ==
				(float)(22 + entry * 40));
			portraits++;
		}

		if (list.vertices[vertex].position[1] == 40 &&
			list.vertices[vertex].position[0] >= 200) {
			i = digits == 0 ? '-' : '1';
			CHECK(list.vertices[vertex].position[0] ==
				(float)(200 + digits * 6));
			CHECK(list.vertices[vertex].uv[0] ==
				(float)assets.glyphs[i].x / 1024);
			CHECK(list.vertices[vertex].uv[1] ==
				(float)assets.glyphs[i].y / 1024);
			digits++;
		}
	}

	CHECK(portraits == 8 && digits == 2);
	assets.count = 2;
	CHECK(mh_results_season_draw(
		&assets, &atlas, points, 320, 480, NULL, &list));
	CHECK(list.vertices[0].position[1] == 140);
	assets.count = 3;
	CHECK(!mh_results_season_draw(
		&assets, &atlas, points, 640, 400, NULL, &list));
	assets.count = 2;
	memset(assets.profiles[1].nick, 'x', sizeof(assets.profiles[1].nick));
	CHECK(!mh_results_season_draw(
		&assets, &atlas, points, 640, 400, NULL, &list));
}

int
main(void)
{

	profile();
	profile_tuning();
	initialized_profile();
	screen_state();
	local_finish();
	local_finish_frame();
	prepared_results();
	aborted_results();
	layout();
	season_layout();

	return failures != 0;
}
