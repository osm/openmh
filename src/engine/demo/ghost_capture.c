#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/ghost_capture.h"
#include "engine/demo/race_internal.h"
#include "engine/game/mode.h"
#include "engine/network/drive.h"

#define CAPTURE_ELAPSED_LIMIT 1e9

static int
valid_ghost_race(const struct mh_demo_race *race)
{
	struct mh_demo_layout layout;
	const struct mh_field *field;
	unsigned int entry;

	if (race == NULL || race->playback.demo == NULL ||
		!race->playback.ghost ||
		mh_demo_race_layout(race->playback.demo, 1, &layout, NULL) !=
			MH_ERROR_NONE)
		return 0;

	field = &race->field;

	if (field->count > MH_DEMO_MAX_PLAYERS ||
		field->count != layout.count ||
		race->layout.count != layout.count ||
		field->playback != MH_GAME_GHOST || field->network ||
		!(field->active_mask & 1) ||
		field->active_mask >= (1U << field->count) ||
		field->unavailable_mask >= (1U << field->count) ||
		(field->tuning.catchup != 0 && field->tuning.catchup != 1))
		return 0;

	for (entry = 0; entry < field->count; entry++)
		if (race->layout.recording[entry] != layout.recording[entry] ||
			field->cars[entry].remote != (entry != 0) ||
			field->cars[entry].car == NULL ||
			field->cars[entry].track == NULL ||
			field->cars[entry].track != field->cars[0].track ||
			!field->cars[entry].countdown ||
			field->cars[entry].race.lap_limit !=
				race->playback.demo->laps ||
			field->cars[entry].track->laps !=
				race->playback.demo->laps)
			return 0;

	return 1;
}

enum mh_error_code
mh_ghost_capture_header(const struct mh_demo_race *race,
	const struct mh_profile *human, struct mh_demo *header,
	struct mh_error *error_out)
{
	struct mh_field local_field;
	struct mh_demo next;
	unsigned char header_bytes[MH_DEMO_MAX_HEADER_BYTES];
	unsigned int entry, slot;
	enum mh_error_code status;

	if (!valid_ghost_race(race) || human == NULL || header == NULL ||
		race->field.active_mask != (1U << race->field.count) - 1)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recording requires a fresh dense Ghost race");

	for (entry = 0; entry < race->field.count; entry++)
		if (race->field.cars[entry].start_ticks != 0)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"Ghost recording must begin before countdown");

	/*
	 * Reuse the local identity/width rules for the human alone. Remote
	 * metadata is copied directly from the original recording below.
	 */
	local_field = race->field;
	local_field.count = local_field.active_mask = 1;
	local_field.unavailable_mask &= 1;
	local_field.playback = 0;
	status = mh_demo_capture_header(&local_field, human, &next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (race->field.cars[0].frame.fixed_rate == 0)
		next.version = race->playback.demo->version;

	next.player_count = race->field.count;

	if (next.version == MH_DEMO_VERSION_25)
		memset(next.players[0].horn, 0, sizeof(next.players[0].horn));

	for (entry = 1; entry < next.player_count; entry++) {
		slot = race->layout.recording[entry];
		next.players[entry] = race->playback.demo->players[slot];
		next.players[entry].active = 1;
		memset(next.players[entry].crossings, 0,
			sizeof(next.players[entry].crossings));
	}

	status = mh_demo_header_write(
		&next, header_bytes, sizeof(header_bytes), error_out);

	if (status == MH_ERROR_NONE)
		*header = next;

	return status;
}

enum mh_error_code
mh_ghost_capture_sample(const struct mh_demo_race *race,
	const struct mh_demo *header, struct mh_demo_record_sample *sample,
	struct mh_error *error_out)
{
	struct mh_demo_record_sample next;
	struct mh_net_vehicle local_vehicle, recorded_vehicle;
	unsigned int entry, slot;
	enum mh_error_code status;

	if (!valid_ghost_race(race) || header == NULL || sample == NULL ||
		header->version !=
			(race->field.cars[0].frame.fixed_rate
					? MH_DEMO_VERSION_40
					: race->playback.demo->version) ||
		header->simulation_rate !=
			race->field.cars[0].frame.fixed_rate ||
		header->player_count != race->field.count ||
		header->local_player != 0 ||
		header->laps != race->field.cars[0].track->laps ||
		header->flags != (race->field.tuning.catchup ? 1UL : 0UL))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"Ghost recording roster/settings changed");

	memset(&next, 0, sizeof(next));
	next.active_mask = race->field.active_mask;
	next.unavailable_mask = race->field.unavailable_mask;
	status = mh_net_drive_capture(
		&race->field.cars[0], NULL, &local_vehicle, error_out);

	for (entry = 0; status == MH_ERROR_NONE && entry < header->player_count;
		entry++) {
		if (!header->players[entry].active)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"Ghost recording lost an initial entrant");

		if (entry != 0) {
			/*
			 * Extract angles from the applied matrix. Interpolated
			 * cached angles can exceed one revolution.
			 */
			slot = race->layout.recording[entry];
			status = mh_net_drive_capture_remote(
				&race->field.cars[entry],
				&race->playback.samples[slot].vehicle,
				&recorded_vehicle, error_out);
		}

		if (status == MH_ERROR_NONE)
			status = mh_net_vehicle_encode(
				entry == 0 ? &local_vehicle : &recorded_vehicle,
				next.bodies[entry], MH_DEMO_VEHICLE_BYTES,
				error_out);
	}

	if (status == MH_ERROR_NONE)
		*sample = next;

	return status;
}

enum mh_error_code
mh_ghost_capture_crossings(const struct mh_demo_race *race,
	struct mh_demo_player players[MH_DEMO_MAX_PLAYERS],
	struct mh_error *error_out)
{
	struct mh_demo_player next[MH_DEMO_MAX_PLAYERS];
	unsigned int entry;

	if (!valid_ghost_race(race) || players == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Ghost checkpoint source");

	memset(next, 0, sizeof(next));

	for (entry = 0; entry < race->field.count; entry++)
		memcpy(next[entry].crossings,
			race->field.cars[entry].race.splits,
			sizeof(next[entry].crossings));

	memcpy(players, next, sizeof(next));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ghost_capture_advance(struct mh_demo_race *race,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, const struct mh_demo *header,
	struct mh_demo_capture_frame *capture, unsigned int *steps,
	unsigned long *random, struct mh_error *error_out)
{
	struct mh_demo_race next;
	struct mh_demo_capture_frame frame;
	enum mh_error_code status;
	unsigned int step_count;

	if (!valid_ghost_race(race) || controls == NULL || header == NULL ||
		capture == NULL || steps == NULL || random == NULL ||
		*random > MH_U32_MAX ||
		!(elapsed >= 0 && elapsed <= CAPTURE_ELAPSED_LIMIT) ||
		(action != MH_FRAME_RUN && action != MH_FRAME_SUSPEND &&
			action != MH_FRAME_MENU))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded Ghost frame");

	next = *race;

	if (next.field.cars[0].frame.fixed_rate == 0)
		next.field.ai_random = *random;

	step_count = 0;
	memset(&frame, 0, sizeof(frame));

	if (action == MH_FRAME_SUSPEND)
		next.field.accumulator = 0;
	else {
		status = mh_demo_race_frame_physics(&next, controls, elapsed,
			action, &step_count, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_frame_media_time(
				next.field.cars[0].frame.seconds,
				next.field.cars[0].frame.intro_ms, &frame.ready,
				&frame.clock_ms, error_out);

		if (status == MH_ERROR_NONE && frame.ready)
			status = mh_ghost_capture_sample(
				&next, header, &frame.sample, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_demo_race_frame_finish(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*race = next;
	*capture = frame;
	*steps = step_count;

	if (next.field.cars[0].frame.fixed_rate == 0)
		*random = next.field.ai_random;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
