#include <string.h>

#include "engine/core/error.h"
#include "engine/demo/race_internal.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/drive_internal.h"

enum mh_error_code
mh_demo_race_layout(const struct mh_demo *demo, int ghost,
	struct mh_demo_layout *output, struct mh_error *error_out)
{
	struct mh_demo_layout layout;
	unsigned int slot, entry, active_players, slots;

	slots = demo == NULL ? 0 : mh_demo_slots(demo->version);

	if (slots == 0 || output == NULL || (ghost != 0 && ghost != 1) ||
		demo->player_count < 1 || demo->player_count > slots ||
		demo->local_player >= slots ||
		!demo->players[demo->local_player].active)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded race layout");

	active_players = 0;

	for (slot = 0; slot < MH_DEMO_MAX_PLAYERS; slot++)
		if (demo->players[slot].active)
			active_players++;

	if (active_players != demo->player_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"inconsistent recorded race slots");

	memset(&layout, 0, sizeof(layout));
	layout.count = demo->player_count;

	if (ghost) {
		if (layout.count < slots)
			layout.count++;

		layout.recording[0] = MH_DEMO_HUMAN_SLOT;
		entry = 1;

		for (slot = 0;
			slot < MH_DEMO_MAX_PLAYERS && entry < layout.count;
			slot++)
			if (demo->players[slot].active)
				layout.recording[entry++] = slot;
	} else {
		layout.recording[0] = demo->local_player;
		entry = 1;

		for (slot = 0; slot < MH_DEMO_MAX_PLAYERS; slot++)
			if (demo->players[slot].active &&
				slot != demo->local_player)
				layout.recording[entry++] = slot;
	}

	*output = layout;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_race_begin(const struct mh_demo *demo, int ghost,
	const struct mh_drive *cars, unsigned int count,
	const struct mh_collision *hulls, struct mh_demo_race *output,
	struct mh_error *error_out)
{
	struct mh_demo_race next;
	struct mh_drive entries[MH_RACE_MAX_CARS];
	enum mh_error_code status;
	unsigned int entry;

	if (cars == NULL || hulls == NULL || output == NULL || demo == NULL ||
		demo->laps < MH_RACE_MIN_LAPS || demo->laps > MH_RACE_MAX_LAPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded race assets");

	memset(&next, 0, sizeof(next));
	status = mh_demo_race_layout(demo, ghost, &next.layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count != next.layout.count)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"recorded race roster size differs");

	status = mh_demo_playback_begin(demo, ghost, &next.playback, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (entry = 0; entry < count; entry++) {
		if (cars[entry].track == NULL ||
			cars[entry].track->laps != demo->laps ||
			!cars[entry].countdown)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"recorded race requires its original laps and countdown");

		entries[entry] = cars[entry];
		entries[entry].remote = !ghost || entry != 0;
		next.best_recorded_ms[entry] = MH_DEMO_INITIAL_BEST_MS;
	}

	status = mh_field_start_playback(
		entries, count, ghost, &next.field, error_out);

	if (status == MH_ERROR_NONE && demo->simulation_rate != 0)
		status = mh_field_simulation_rate(
			&next.field, demo->simulation_rate, error_out);

	if (status == MH_ERROR_NONE)
		next.field.tuning.catchup = (demo->flags & 1) != 0;

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(
			&next.field, hulls, count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_race_apply_samples(&next, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_classify(&next.field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_frame_update(&next.field, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
restore_driver_time(struct mh_drive *drive, const struct mh_drive *previous,
	struct mh_error *error_out)
{
	drive->race.split_hold_ms = previous->race.split_hold_ms;

	drive->seconds = previous->seconds;
	drive->ticks = previous->ticks;
	drive->frame = previous->frame;

	return mh_drive_clock_restart(drive, 0, error_out);
}

enum mh_error_code
mh_demo_race_restart(struct mh_demo_race *race, struct mh_error *error_out)
{
	struct mh_demo_race next;
	struct mh_drive cars[MH_RACE_MAX_CARS];
	struct mh_field_object_source objects[MH_FIELD_OBJECTS];
	const struct mh_drive *previous;
	enum mh_error_code status;
	unsigned int entry, object_index;

	if (race == NULL || race->layout.count < 1 ||
		race->layout.count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded restart");

	for (entry = 0; entry < race->layout.count; entry++) {
		previous = &race->field.cars[entry];
		status = mh_drive_start(previous->car, previous->col,
			previous->materials, previous->route, previous->track,
			previous->slot, previous->config.options.manual, 1,
			&cars[entry], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		cars[entry].camera_motion = previous->camera_motion;
	}

	status = mh_demo_race_begin(race->playback.demo, race->playback.ghost,
		cars, race->layout.count, race->field.hulls[0], &next,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_field_set_cheats(
		&next.field, race->field.cars[0].cheats, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (object_index = 0; object_index < race->field.object_count;
		object_index++)
		objects[object_index] =
			race->field.objects[object_index].source;

	status = mh_field_set_objects(
		&next.field, objects, race->field.object_count, error_out);

	if (status == MH_ERROR_NONE && race->field.records_bound)
		status =
			mh_field_bind_records(&next.field, &race->field.records,
				race->field.record_mask, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (entry = 0; entry < race->layout.count; entry++) {
		status = restore_driver_time(&next.field.cars[entry],
			&race->field.cars[entry], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	next.playback.benchmark = race->playback.benchmark;
	next.ticks = race->ticks;
	next.field.listener_mask = race->field.listener_mask;
	next.field.listener_revision = race->field.listener_revision;
	status = mh_field_restart_listener(&next.field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*race = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
