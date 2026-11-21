#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/field.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "field tuning line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_field field, saved;
static struct mh_track track;
static struct mh_ai_path path;
static struct mh_ai_point points[100];

static int
check_invalid_grid(void)
{
	struct mh_drive first;
	const struct mh_car *opponents[2];
	struct mh_error error;
	enum mh_error_code status;
	unsigned int slots[3];

	memset(&first, 0, sizeof(first));
	memset(opponents, 0, sizeof(opponents));
	memset(&field, 0xa5, sizeof(field));
	saved = field;
	first.slot = 3;
	slots[0] = 2;
	slots[1] = 0;
	slots[2] = 1;
	status = mh_field_start_grid(
		&first, opponents, 2, &path, 2, slots, &field, &error);
	CHECK(status != MH_ERROR_NONE &&
		strcmp(error.message, "grid does not match the local car") ==
			0);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0);
	slots[0] = 3;
	slots[2] = 0;
	status = mh_field_start_grid(
		&first, opponents, 2, &path, 2, slots, &field, &error);
	CHECK(status != MH_ERROR_NONE &&
		strcmp(error.message, "duplicate field grid slot") == 0);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0);
	slots[2] = 64;
	status = mh_field_start_grid(
		&first, opponents, 2, &path, 2, slots, &field, &error);
	CHECK(status != MH_ERROR_NONE &&
		strcmp(error.message, "invalid field grid slot") == 0);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0);
	first.slot = slots[0] = 64;
	status = mh_field_start_grid(
		&first, NULL, 0, NULL, 0, slots, &field, &error);
	CHECK(status != MH_ERROR_NONE &&
		strcmp(error.message, "invalid field grid slot") == 0);
	CHECK(memcmp(&field, &saved, sizeof(field)) == 0);

	return 0;
}

static void
prepare_field(void)
{
	unsigned int i, j;

	memset(&field, 0, sizeof(field));
	memset(&track, 0, sizeof(track));
	memset(&path, 0, sizeof(path));
	memset(points, 0, sizeof(points));
	track.laps = 2;
	track.has_ai[1] = 1;
	track.ai[1][10] = 20;
	path.points = points;
	path.count = 100;
	field.path = &path;
	field.count = field.standings.count = 8;
	field.active_mask = 255;

	for (i = 0; i < 8; i++) {
		field.cars[i].track = &track;
		field.cars[i].countdown = 1;
		field.cars[i].race.lap_limit = 2;
		field.cars[i].race.lap = 1;
		field.cars[i].config.gears = 4 + i % 3;
		field.cars[i].config.acceleration = 3000 + 100 * i;
		field.cars[i].config.options.role_two = i != 0;
		field.standings.cars[i].position =
			(i + 3) % 8 + 1; /* Local rank four. */

		for (j = 0; j < 7; j++)
			field.cars[i].config.ratio[j] = 50 + 50 * j + .1 * i;

		field.point[i] = 90 - 10 * i;
		field.cars[i].race.splits[1][2] = 1;
	}
}

int
main(void)
{
	struct mh_engine_config base[8], previous[8];
	struct mh_race_tuning curve;
	unsigned int i, j, rank;
	float expected;

	CHECK(check_invalid_grid() == 0);
	prepare_field();
	CHECK(mh_field_enable_catchup(&field, 2, NULL) == MH_ERROR_NONE);
	CHECK(field.tuning.catchup && field.tuning.strength == 20);
	field.tuning.catchup = 0;

	for (i = 0; i < 8; i++)
		base[i] = field.cars[i].config;

	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE &&
		field.tuning.initialized);

	for (i = 0; i < 8; i++) {
		CHECK(field.cars[i].config.options.suppress_role_two_spin);
		CHECK(field.cars[i].config.acceleration ==
			base[i].acceleration);
		CHECK(memcmp(&field.tuning.base[i], &base[i],
			sizeof(base[i])) == 0);
	}

	field.tuning.ticks = 1440;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].config.acceleration == base[0].acceleration);

	/* Rank eight is entry four; rank one is entry five. */
	CHECK(field.cars[4].config.acceleration ==
		(float)(base[4].acceleration * .75));
	CHECK(field.cars[5].config.acceleration ==
		(float)(base[5].acceleration * 1.25));

	for (i = 0; i < 8; i++)
		previous[i] = field.cars[i].config;

	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		CHECK(memcmp(&previous[i], &field.cars[i].config,
			sizeof(previous[i])) == 0);

	field.tuning.ticks = 1860;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[4].config.acceleration ==
		(float)(base[4].acceleration * .875));

	for (i = 0; i < 8; i++) {
		CHECK(field.cars[i].config.ratio[0] == base[i].ratio[0]);

		for (j = field.cars[i].config.gears + 1; j < 7; j++)
			CHECK(field.cars[i].config.ratio[j] ==
				base[i].ratio[j]);
	}

	field.tuning.ticks = 2278;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		previous[i] = field.cars[i].config;

	field.tuning.ticks = 2280;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		CHECK(memcmp(&previous[i], &field.cars[i].config,
			sizeof(previous[i])) == 0);

	/*
	 * The first enabled pass uses the previous zero progress, then saves
	 * new progress. The next pass must use that retained table, not current
	 * points.
	 */
	field.tuning.catchup = 1;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++) {
		CHECK(field.cars[i].config.acceleration ==
			(float)base[i].acceleration);
		rank = field.standings.cars[i].position - 1;
		CHECK(field.tuning.curve.progress[rank] == field.point[i]);
	}

	for (i = 0; i < 8; i++)
		field.tuning.curve.progress[i] = (float)(100 - 10 * i);

	curve = field.tuning.curve;
	CHECK(mh_race_tuning_curve(&curve, 8, 4, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		field.point[i] = i;

	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(curve.bias, field.tuning.curve.bias, sizeof(curve.bias)) ==
		0);
	CHECK(field.cars[0].config.acceleration == 3000);
	expected = (float)(1 +
		(float)((float)(curve.bias[7] - curve.local_bias) *
			(float)(20 * (float).01)));
	CHECK(field.cars[4].config.acceleration ==
		(float)((float)base[4].acceleration * expected));

	/*
	 * Reject a late-car failure without changing ANY earlier
	 * car/base/table.
	 */
	field.point[7] = path.count;
	saved = field;
	CHECK(mh_field_tuning_frame(&field, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	field.point[7] = 7;
	field.cars[7].config.gears = 3;
	saved = field;
	CHECK(mh_field_tuning_frame(&field, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	prepare_field();
	field.time_attack = 1;
	saved = field;
	CHECK(mh_field_enable_catchup(&field, 2, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	field.time_attack = 0;
	field.network = 1;
	saved = field;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	field.network = 0;
	field.playback = 6;
	saved = field;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	field.playback = 3;
	saved = field;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	prepare_field();
	field.cars[0].countdown = 0;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE &&
		field.cars[4].config.acceleration == 2550);
	prepare_field();
	field.count = field.standings.count = field.active_mask = 1;
	CHECK(mh_field_enable_catchup(&field, 2, NULL) == MH_ERROR_NONE);
	saved = field;
	CHECK(mh_field_enable_time_attack(&field, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	prepare_field();
	field.cars[7].ticks = 1;
	saved = field;
	CHECK(mh_field_enable_catchup(&field, 2, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);

	/*
	 * The real caller tunes before its GO/menu physics gates. GO remains
	 * zero on the held grid, including after a lifetime-clock restart.
	 */
	prepare_field();
	field.cars[0].frame.active = 1;
	field.cars[0].frame.seconds = 3.5;
	CHECK(!mh_drive_racing(&field.cars[0]));
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[4].config.acceleration == (float)(3400 * .875));
	CHECK(field.cars[5].config.acceleration == (float)(3500 * 1.125));
	field.cars[0].frame.seconds = 20;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[4].config.acceleration == (float)(3400 * .875));
	field.tuning.catchup = 1;
	field.tuning.strength = 20;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[4].config.acceleration == 3400);

	for (i = 0; i < 8; i++) {
		rank = field.standings.cars[i].position - 1;
		CHECK(field.tuning.curve.progress[rank] == field.point[i]);
	}

	/*
	 * Mode 5 keeps the same launch handicap, including the slot-zero
	 * exclusion even though that entrant is AI. Catch-up never follows.
	 */
	prepare_field();
	saved = field;
	CHECK(mh_field_enable_attract(&field, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);

	for (i = 0; i < 8; i++) {
		field.cars[i].ai_path = &path;
		field.cars[i].config.options.role_two = 1;
	}

	CHECK(mh_field_enable_attract(&field, NULL) == MH_ERROR_NONE &&
		field.attract);
	saved = field;
	CHECK(mh_field_enable_catchup(&field, 2, NULL) != MH_ERROR_NONE &&
		memcmp(&field, &saved, sizeof(field)) == 0);
	field.cars[0].frame.active = 1;
	field.cars[0].frame.time.go_ms = 12000;
	field.cars[0].frame.seconds = 12;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);
	CHECK(field.cars[0].config.acceleration == 3000);
	CHECK(field.cars[4].config.acceleration == 2550 &&
		field.cars[5].config.acceleration == 4375);

	for (i = 0; i < 8; i++) {
		CHECK(field.cars[i].config.options.suppress_role_two_spin);
		previous[i] = field.cars[i].config;
	}

	field.cars[0].frame.seconds = 19;
	field.tuning.catchup = 1;
	field.tuning.strength = 20;
	CHECK(mh_field_tuning_frame(&field, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 8; i++)
		CHECK(memcmp(&previous[i], &field.cars[i].config,
			sizeof(previous[i])) == 0);

	puts("field tuning: held/GO/7s boundaries, bases, old progress, mode exclusions and late-failure rollback passed");

	return 0;
}
