#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"
#include "engine/demo/capture.h"
#include "engine/game/race_limits.h"
#include "engine/input/control_type.h"
#include "engine/network/drive.h"
#include "engine/simulation/field_internal.h"

#define CAPTURE_ELAPSED_LIMIT 1e9
#define RECORDED_COLORS 4

static int
copy_profile_text(
	char *output, size_t width, const char *source, int allow_empty)
{
	size_t index;
	unsigned char byte;

	for (index = 0; index < width; index++) {
		byte = (unsigned char)source[index];

		if (byte == 0) {
			output[index] = 0;
			return index != 0 || allow_empty;
		}

		if (byte < ' ')
			return 0;

		output[index] = (char)byte;
	}

	return 0;
}

static int
valid_local_field(const struct mh_field *field)
{
	unsigned int entry;

	if (field == NULL || field->count < 1 ||
		field->count > MH_DEMO_PLAYERS || field->network ||
		field->playback || !(field->active_mask & 1) ||
		(field->tuning.catchup != 0 && field->tuning.catchup != 1) ||
		field->active_mask >= (1U << field->count) ||
		field->unavailable_mask >= (1U << field->count))
		return 0;

	for (entry = 0; entry < field->count; entry++)
		if (field->cars[entry].remote ||
			field->cars[entry].car == NULL ||
			field->cars[entry].track == NULL ||
			field->cars[entry].track != field->cars[0].track ||
			!field->cars[entry].countdown ||
			field->cars[entry].race.lap_limit !=
				field->cars[0].track->laps)
			return 0;

	return field->cars[0].track->laps >= MH_RACE_MIN_LAPS &&
		field->cars[0].track->laps <= MH_RACE_MAX_LAPS;
}

enum mh_error_code
mh_demo_capture_header(const struct mh_field *field,
	const struct mh_profile *profiles, struct mh_demo *header,
	struct mh_error *error_out)
{
	struct mh_demo next;
	struct mh_demo_player *player;
	const struct mh_profile *profile;
	unsigned int entry, color;

	if (!valid_local_field(field) || profiles == NULL || header == NULL ||
		field->active_mask != (1U << field->count) - 1)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recording requires a local race with countdown");

	memset(&next, 0, sizeof(next));

	/* European 3.1 recorder initialization. */
	next.version = MH_DEMO_VERSION_30;

	if (field->cars[0].frame.fixed_rate) {
		next.version = MH_DEMO_VERSION_40;
		next.timing_version = MH_SIM_TIMING_VERSION;
		next.simulation_rate = field->cars[0].frame.fixed_rate;
	}

	next.player_count = field->count;
	next.laps = field->cars[0].track->laps;
	next.flags = field->tuning.catchup ? 1 : 0;

	if (!copy_profile_text(next.track, MH_DEMO_NAME_BYTES,
		field->cars[0].track->name, 0))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"recorded track name exceeds original width");

	for (entry = 0; entry < field->count; entry++) {
		/*
		 * Lifetime slice counts survive Restart; the countdown origin
		 * is the native guard for constructing a fresh recording
		 * header.
		 */
		if (field->cars[entry].start_ticks != 0)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"recording must begin before the race countdown");

		player = &next.players[entry];
		profile = &profiles[entry];
		player->active = 1;

		/* Copy nick/team, including immediate NUL. */
		if (!copy_profile_text(player->name, MH_DEMO_NAME_BYTES,
			profile->nick, 1) ||
			!copy_profile_text(player->team, MH_DEMO_TEAM_BYTES,
				profile->team, 1) ||
			!copy_profile_text(player->car, MH_DEMO_NAME_BYTES,
				field->cars[entry].car->name, 0))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"recorded nick/team/car exceeds original width (15/4/15)");

		/* Copy the selected runtime horn name. */
		if (!copy_profile_text(
			player->horn, MH_DEMO_NAME_BYTES, profile->horn, 1))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"recorded horn exceeds original width");

		for (color = 0; color < RECORDED_COLORS; color++) {
			if (profile->colors[color] > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"recorded color exceeds disk word");

			player->colors[color] = profile->colors[color];
		}

		player->gear_mode =
			field->cars[entry].config.options.manual ? 0 : 1;

		/*
		 * Default inspection identity; applied profiles override the
		 * human word before opening the recorder. AI remains
		 * unclassified.
		 */
		player->control_type = entry == 0 ? MH_CONTROL_KEYBOARD
						: MH_CONTROL_UNSPECIFIED;
	}

	*header = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_capture_sample(const struct mh_field *field,
	const struct mh_demo *header, struct mh_demo_record_sample *sample,
	struct mh_error *error_out)
{
	struct mh_demo_record_sample next;
	struct mh_net_vehicle vehicle;
	unsigned int entry;
	enum mh_error_code status;

	if (!valid_local_field(field) || header == NULL || sample == NULL ||
		header->simulation_rate != field->cars[0].frame.fixed_rate ||
		header->player_count != field->count ||
		header->local_player != 0 ||
		header->laps != field->cars[0].track->laps ||
		header->flags != (field->tuning.catchup ? 1UL : 0UL))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recorded race roster/settings changed");

	memset(&next, 0, sizeof(next));
	next.active_mask = field->active_mask;
	next.unavailable_mask = field->unavailable_mask;

	for (entry = 0; entry < field->count; entry++) {
		if (!header->players[entry].active)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"recorded race lost an initial entrant");

		status = mh_net_drive_capture(
			&field->cars[entry], NULL, &vehicle, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_net_vehicle_encode(&vehicle,
				next.bodies[entry], MH_DEMO_VEHICLE_BYTES,
				error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*sample = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_capture_crossings(const struct mh_field *field,
	struct mh_demo_player players[MH_DEMO_PLAYERS],
	struct mh_error *error_out)
{
	struct mh_demo_player next[MH_DEMO_PLAYERS];
	unsigned int entry;

	if (!valid_local_field(field) || players == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording checkpoint source");

	memset(next, 0, sizeof(next));

	for (entry = 0; entry < field->count; entry++)
		memcpy(next[entry].crossings, field->cars[entry].race.splits,
			sizeof(next[entry].crossings));

	memcpy(players, next, sizeof(next));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_capture_advance(struct mh_field *field,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, const struct mh_demo *header,
	struct mh_demo_capture_frame *capture, unsigned int *steps,
	unsigned long *random, struct mh_error *error_out)
{
	struct mh_field next;
	struct mh_demo_capture_frame frame;
	enum mh_error_code status;
	unsigned int step_count;

	if (!valid_local_field(field) || controls == NULL || header == NULL ||
		capture == NULL || steps == NULL || random == NULL ||
		*random > MH_U32_MAX ||
		!(elapsed >= 0 && elapsed <= CAPTURE_ELAPSED_LIMIT) ||
		(action != MH_FRAME_RUN && action != MH_FRAME_SUSPEND &&
			action != MH_FRAME_MENU))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded local frame");

	next = *field;

	if (next.cars[0].frame.fixed_rate == 0)
		next.ai_random = *random;

	step_count = 0;
	memset(&frame, 0, sizeof(frame));

	if (action == MH_FRAME_SUSPEND)
		next.accumulator = 0;
	else {
		status = mh_field_frame_physics(&next, controls, elapsed,
			action, &step_count, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_frame_media_time(next.cars[0].frame.seconds,
				next.cars[0].frame.intro_ms, &frame.ready,
				&frame.clock_ms, error_out);

		if (status == MH_ERROR_NONE && frame.ready)
			status = mh_demo_capture_sample(
				&next, header, &frame.sample, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_field_frame_finish(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*field = next;
	*capture = frame;
	*steps = step_count;

	if (next.cars[0].frame.fixed_rate == 0)
		*random = next.ai_random;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
