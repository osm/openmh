#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/field_internal.h"

enum mh_error_code
mh_field_set_cheats(
	struct mh_field *field, unsigned int flags, struct mh_error *error_out)
{
	struct mh_suspension_config suspension[MH_RACE_MAX_CARS];
	enum mh_error_code status;
	unsigned int i;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || field->network)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"cheat setup requires a local race field");

	if (flags != 0 && flags != MH_CHEAT_MOON &&
		flags != MH_CHEAT_IGNITION && flags != MH_CHEAT_SUPERCARS &&
		flags != MH_CHEAT_DEMON && flags != MH_CHEAT_G_RIDE &&
		flags != MH_CHEAT_UNDERWATER && flags != MH_CHEAT_THUNDER &&
		flags != MH_CHEAT_TRON && flags != MH_CHEAT_RETRO)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"this hidden mode is recognized but not connected yet");

	for (i = 0; i < field->count; i++)
		if (field->cars[i].ticks != 0 ||
			field->cars[i].start_ticks != 0)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"cheat setup requires a fresh race");

	for (i = 0; i < field->count; i++) {
		status = mh_suspension_configure(
			field->cars[i].car, flags, &suspension[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < field->count; i++) {
		field->cars[i].cheats = flags;
		field->cars[i].suspension = suspension[i];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_set_active(struct mh_field *field, unsigned int active_mask,
	struct mh_error *error_out)
{
	struct mh_field next;
	enum mh_error_code status;

	if (field == NULL || !field->network || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || !(active_mask & 1) ||
		active_mask >= (1U << field->count) ||
		(active_mask & ~field->active_mask))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network field departure");

	if (active_mask == field->active_mask)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	next = *field;
	next.active_mask = active_mask;
	next.unavailable_mask &= active_mask;

	if (next.viewed_entry >= next.count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid viewed car");

	if (!(active_mask & (1U << next.viewed_entry))) {
		status = mh_field_select_view(&next, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_field_rank(
		&next, !mh_drive_racing(&next.cars[0]), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_start(const struct mh_drive *first,
	const struct mh_car *const *opponents, unsigned int count,
	const struct mh_ai_path *path, unsigned int difficulty,
	struct mh_field *output, struct mh_error *error_out)
{
	return mh_field_start_grid(first, opponents, count, path, difficulty,
		NULL, output, error_out);
}

enum mh_error_code
mh_field_start_grid(const struct mh_drive *first,
	const struct mh_car *const *opponents, unsigned int count,
	const struct mh_ai_path *path, unsigned int difficulty,
	const unsigned int *slots, struct mh_field *output,
	struct mh_error *error_out)
{
	struct mh_field next;
	enum mh_error_code status;
	unsigned int i, j, slot;

	if (first == NULL || output == NULL ||
		count > MH_TRAFFIC_MAX_CARS - 1 ||
		(count != 0 && (opponents == NULL || path == NULL))) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid driving field");
	}

	if (count != 0 &&
		(first->ticks != 0 || first->start_ticks != 0 ||
			(first->ai_path != NULL && first->ai_path != path)))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"field requires a fresh, shared-path session");

	if (slots != NULL) {
		if (slots[0] != first->slot)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"grid does not match the local car");

		for (i = 0; i <= count; i++) {
			if (slots[i] >= MH_FIELD_GRID_SLOTS)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid field grid slot");

			for (j = 0; j < i; j++)
				if (slots[j] == slots[i])
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"duplicate field grid slot");
		}
	}

	memset(&next, 0, sizeof(next));
	next.cars[0] = *first;
	next.count = count + 1;
	next.path = path;

	if (next.path == NULL)
		next.path = first->ai_path;

	next.scene_seconds = first->seconds;
	next.listener_mask = first->camera_mode == MH_CAMERA_TRACK ? 0 : 1;
	next.active_mask = (1U << next.count) - 1;
	slot = 0;

	for (i = 1; i <= count; i++) {
		if (slot == first->slot)
			slot++;

		status = mh_drive_start(opponents[i - 1], first->col,
			first->materials, first->route, first->track,
			slots == NULL ? slot++ : slots[i], 0, first->countdown,
			&next.cars[i], error_out);

		if (status == MH_ERROR_NONE) {
			next.cars[i].cheats = first->cheats;
			status = mh_suspension_configure(next.cars[i].car,
				first->cheats, &next.cars[i].suspension,
				error_out);
		}

		if (status == MH_ERROR_NONE)
			status = mh_drive_enable_ai(
				&next.cars[i], path, difficulty, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < next.count; i++) {
		next.traffic[i].threat = -1;
		status = mh_field_locate(&next, i, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_field_ai_reset(&next, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_rank(&next, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_enable_time_attack(struct mh_field *field, struct mh_error *error_out)
{
	if (field == NULL || field->count != 1 || field->active_mask != 1 ||
		field->network || field->attract || field->tuning.catchup ||
		field->cars[0].remote || field->cars[0].slot != 0 ||
		field->cars[0].ticks != 0 || field->cars[0].start_ticks != 0 ||
		field->cars[0].race.finished ||
		field->cars[0].race.lap_limit < MH_RACE_MIN_LAPS ||
		field->cars[0].race.lap_limit > MH_RACE_MAX_LAPS)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"time attack requires a fresh single-car race at grid slot zero");

	field->time_attack = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_enable_attract(struct mh_field *field, struct mh_error *error_out)
{
	unsigned int i;

	if (field == NULL || field->count != MH_TRAFFIC_MAX_CARS ||
		field->active_mask != (1U << MH_TRAFFIC_MAX_CARS) - 1 ||
		field->network || field->playback || field->time_attack ||
		field->tuning.catchup || field->tuning.initialized ||
		field->records_bound || field->path == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"attract requires a fresh eight-AI race");

	for (i = 0; i < field->count; i++)
		if (field->cars[i].remote ||
			field->cars[i].ai_path != field->path ||
			field->cars[i].ticks != 0 ||
			field->cars[i].start_ticks != 0)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"attract requires fresh local AI entrants");

	field->attract = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_start_network(const struct mh_drive *cars, unsigned int count,
	struct mh_field *output, struct mh_error *error_out)
{
	struct mh_field next;
	unsigned int i, j;
	enum mh_error_code status;

	if (cars == NULL || output == NULL || count < 1 ||
		count > MH_RACE_MAX_CARS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid network field");

	memset(&next, 0, sizeof(next));
	next.network = 1;
	next.count = count;
	next.active_mask = (1U << count) - 1;
	next.listener_mask = 1;

	for (i = 0; i < count; i++) {
		if (cars[i].car == NULL || cars[i].col == NULL ||
			cars[i].track == NULL || cars[i].ticks != 0 ||
			cars[i].start_ticks != 0 || cars[i].ai_path != NULL ||
			cars[i].remote != (i != 0) ||
			cars[i].col != cars[0].col ||
			cars[i].route != cars[0].route ||
			cars[i].materials != cars[0].materials ||
			cars[i].track != cars[0].track ||
			cars[i].countdown != cars[0].countdown)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"network field requires fresh local/remote cars and shared track");

		for (j = 0; j < i; j++)
			if (cars[i].slot == cars[j].slot)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate network grid slot");

		next.cars[i] = cars[i];
		next.traffic[i].threat = -1;
	}

	status = mh_field_rank(&next, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_start_playback(const struct mh_drive *cars, unsigned int count,
	int ghost, struct mh_field *output, struct mh_error *error_out)
{
	struct mh_field next;
	unsigned int i;
	enum mh_error_code status;

	if (cars == NULL || output == NULL || count < 1 ||
		count > MH_RACE_MAX_CARS || (ghost != 0 && ghost != 1) ||
		(ghost && count < 2))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid recorded field");

	memset(&next, 0, sizeof(next));
	next.playback = ghost ? MH_GAME_GHOST : MH_GAME_DEMO;
	next.count = count;
	next.active_mask = (1U << count) - 1;
	next.listener_mask = 1;

	for (i = 0; i < count; i++) {
		if (cars[i].car == NULL || cars[i].col == NULL ||
			cars[i].track == NULL || cars[i].ticks != 0 ||
			cars[i].start_ticks != 0 || cars[i].ai_path != NULL ||
			cars[i].remote != (!ghost || i != 0) ||
			cars[i].col != cars[0].col ||
			cars[i].route != cars[0].route ||
			cars[i].materials != cars[0].materials ||
			cars[i].track != cars[0].track ||
			cars[i].countdown != cars[0].countdown)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"recorded field requires fresh cars and shared track");

		next.cars[i] = cars[i];
		next.traffic[i].threat = -1;
	}

	status = mh_field_rank(&next, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_set_hulls(struct mh_field *field, const struct mh_collision *hulls,
	unsigned int count, struct mh_error *error_out)
{
	unsigned int i;

	if (field == NULL || hulls == NULL || count < 1 ||
		count > MH_RACE_MAX_CARS || count != field->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid field hull roster");

	for (i = 0; i < count; i++) {
		if (hulls[i].polygons == NULL || hulls[i].polygon_count == 0 ||
			hulls[i].polygon_count > (unsigned long)MH_U16_MAX) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid field car hull");
		}
	}

	for (i = 0; i < count; i++)
		field->hulls[i] = &hulls[i];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_reset(struct mh_field *field, int grid, struct mh_error *error_out)
{
	struct mh_field next;
	unsigned int i;
	enum mh_error_code status;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || (grid != 0 && grid != 1)) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid field reset");
	}

	next = *field;

	if (field->network && grid)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network grid restart requires a new session race");

	if (field->playback && grid)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recorded grid restart requires the playback controller");

	for (i = 0; i < (grid ? field->count : 1); i++) {
		status = mh_drive_reset(&next.cars[i], grid, error_out);

		if (status == MH_ERROR_NONE && grid)
			status = mh_field_locate(&next, i, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (grid) {
			memset(&next.traffic[i], 0, sizeof(next.traffic[i]));
			next.traffic[i].threat = -1;
			next.lateral_rate[i] = 0;
		}

		next.contact_count[i] = next.object_contact_count[i] = 0;
		next.contact_impulse[i] = 0;

		/*
		 * Leave body constraints intact. A later peer still supplies
		 * these constraints until its next contact turn publishes.
		 */
	}

	if (grid) {
		/*
		 * Restart selects the local view and enables its detailed sound
		 * even if the preceding view used positional playback.
		 */
		status = mh_field_restart_listener(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		next.scene_seconds = 0;
		next.scenery_contact_mask = 0;
		next.scenery_selection_valid = 0;
		next.physics_mask = 0;
		next.physics_selection_valid = 0;

		/* Saved bases/previous rank table survive Home. */
		next.tuning.ticks = 0;

		if (next.records_bound)
			for (i = 0; i < next.count; i++)
				next.cars[i].race.record_eligible =
					(next.record_mask >> i) & 1U;
	}

	if (grid)
		for (i = 0; i < next.object_count; i++) {
			status = mh_field_object_reset(
				&next.objects[i], 0, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	status = grid ? mh_field_ai_reset(&next, error_out)
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (status != MH_ERROR_NONE)
		return status;

	next.accumulator = 0;
	status = mh_field_rank(
		&next, grid || (!mh_drive_racing(&next.cars[0])), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
