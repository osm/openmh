#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/session_race_steps.h"
#include "client/race/race.h"
#include "client/race/trace.h"
#include "client/session/demo_recording.h"
#include "engine/core/error.h"
#include "engine/render/scene_matrices.h"
#include "engine/ui/tron_intro.h"

enum {
	DIAGNOSTIC_TICKS_PER_FRAME = 2,
	REPLAY_LINE_SIZE = 1024,
	REPLAY_THROTTLE = 0,
	REPLAY_BRAKE,
	REPLAY_STEERING,
	REPLAY_SHIFT_UP,
	REPLAY_SHIFT_DOWN,
	REPLAY_HANDBRAKE,
	REPLAY_RESET,
	REPLAY_VALUES
};

enum replay_reset {
	REPLAY_CONTINUE,
	REPLAY_RECOVER,
	REPLAY_RESTART
};

static enum mh_error_code
present_diagnostic_frame(struct mh_race_context *race_context,
	struct mh_field *field, struct mh_error *error_out)
{
	float model_matrices[MH_FIELD_MESHES * 16], view_projection[16],
		view_matrix[16];
	struct mh_tron_intro intro;
	struct mh_retro_frame retro;
	enum mh_error_code status;

	/*
	 * Deterministic 60 Hz presentation for command-line simulation/replays.
	 */
	if (field->cars[0].ticks % DIAGNOSTIC_TICKS_PER_FRAME != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_field_frame_update(field, 1.0f / 60, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_skids(
			&race_context->scene, field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_update_particles(
			&race_context->scene, field, error_out);

	if (status == MH_ERROR_NONE &&
		(field->cars[0].cheats & MH_CHEAT_TRON)) {
		status = mh_tron_intro_frame(
			race_context->scene.assets.view_width,
			race_context->scene.assets.view_height,
			race_context->scene.visuals.tron_time, 1.0f / 60, 1,
			&intro, error_out);

		if (status == MH_ERROR_NONE)
			race_context->scene.visuals.tron_time = intro.next_time;
	}

	if (status == MH_ERROR_NONE &&
		(field->cars[0].cheats & MH_CHEAT_RETRO)) {
		status = mh_retro_frame(1.0f / 60,
			&race_context->scene.visuals.retro_state, &retro,
			error_out);

		if (status == MH_ERROR_NONE)
			race_context->scene.visuals.retro_state = retro.next;
	}

	if (status != MH_ERROR_NONE)
		return status;

	/* Software skid pixels are prepared by the software scene path. */
	if (race_context->scene.assets.profile == MH_RENDER_SOFTWARE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	mh_field_matrices(field,
		(double)race_context->scene.assets.view_width /
			race_context->scene.assets.view_height,
		model_matrices, view_projection, view_matrix);

	return mh_skid_mesh_update(&race_context->scene.visuals.skid_mesh,
		race_context->scene.visuals.skids, MH_RACE_MAX_CARS,
		view_projection, field->cars[0].eye, error_out);
}

static enum mh_error_code
record_diagnostic_frame(struct mh_demo_recording *recording,
	const struct mh_field *field, struct mh_error *error_out)
{
	return recording != NULL &&
			field->cars[0].ticks % DIAGNOSTIC_TICKS_PER_FRAME == 0
		? mh_demo_recording_frame(recording, field, error_out)
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
parse_controls(const char *line, struct mh_engine_input *input,
	enum replay_reset *reset)
{
	const char *cursor;
	char *end;
	double values[REPLAY_VALUES];
	unsigned int i;

	cursor = line;

	for (i = 0; i < REPLAY_VALUES; i++) {
		errno = 0;
		values[i] = strtod(cursor, &end);

		if (cursor == end || errno != 0 ||
			!(values[i] >= -1 && values[i] <= 2) ||
			(*end != 0 && !isspace((unsigned char)*end)))
			break;

		cursor = end;
	}

	while (isspace((unsigned char)*cursor))
		cursor++;

	if (i != REPLAY_VALUES || *cursor != 0 || values[REPLAY_THROTTLE] < 0 ||
		values[REPLAY_THROTTLE] > 1 || values[REPLAY_BRAKE] < 0 ||
		values[REPLAY_BRAKE] > 1 || values[REPLAY_STEERING] > 1 ||
		(values[REPLAY_SHIFT_UP] != 0 &&
			values[REPLAY_SHIFT_UP] != 1) ||
		(values[REPLAY_SHIFT_DOWN] != 0 &&
			values[REPLAY_SHIFT_DOWN] != 1) ||
		(values[REPLAY_HANDBRAKE] != 0 &&
			values[REPLAY_HANDBRAKE] != 1) ||
		(values[REPLAY_RESET] != 0 && values[REPLAY_RESET] != 1 &&
			values[REPLAY_RESET] != 2)) {
		return 0;
	}

	input->throttle = values[REPLAY_THROTTLE];
	input->brake = values[REPLAY_BRAKE];
	input->steering = values[REPLAY_STEERING];
	input->shift_up = (int)values[REPLAY_SHIFT_UP];
	input->shift_down = (int)values[REPLAY_SHIFT_DOWN];
	input->handbrake = (int)values[REPLAY_HANDBRAKE];
	*reset = (enum replay_reset)values[REPLAY_RESET];

	return 1;
}

static enum mh_error_code
prepare_replay_tick(struct mh_race_context *race_context,
	struct mh_field *field, struct mh_demo_recording *recording,
	enum replay_reset reset, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_race_scores_frame(race_context, field, error_out);

	if (status == MH_ERROR_NONE && reset == REPLAY_RESTART)
		status = mh_race_records_save(race_context, field, error_out);

	if (status == MH_ERROR_NONE && reset != REPLAY_CONTINUE)
		status = reset == REPLAY_RESTART && recording != NULL
			? mh_demo_recording_restart(recording, field, error_out)
			: mh_field_reset(
				field, reset == REPLAY_RESTART, error_out);

	if (status == MH_ERROR_NONE && reset == REPLAY_RESTART) {
		mh_race_records_restarted(race_context);
		mh_race_scene_skids_reset(&race_context->scene);
		mh_race_scene_reset_particles(&race_context->scene);
	}

	return status;
}

static enum mh_error_code
advance_diagnostic_frame(struct mh_race_context *race_context,
	struct mh_field *field, const struct mh_engine_input *input,
	struct mh_demo_recording *recording, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_field_frame_tick_duration(field, input,
		field->cars[0].ticks % DIAGNOSTIC_TICKS_PER_FRAME == 0,
		field->cars[0].ticks % DIAGNOSTIC_TICKS_PER_FRAME == 0
			? DIAGNOSTIC_TICKS_PER_FRAME * MH_DRIVE_DT
			: 0,
		error_out);

	if (status == MH_ERROR_NONE)
		status = present_diagnostic_frame(
			race_context, field, error_out);

	if (status == MH_ERROR_NONE)
		status = record_diagnostic_frame(recording, field, error_out);

	return status;
}

enum mh_error_code
mh_debug_race_replay(struct mh_race_context *race_context,
	struct mh_field *field, const char *path, FILE *trace,
	struct mh_demo_recording *recording, struct mh_error *error_out)
{
	FILE *file;
	char line[REPLAY_LINE_SIZE];
	enum replay_reset reset;
	unsigned long line_number;
	struct mh_engine_input input;
	struct mh_error error;
	enum mh_error_code status;

	file = fopen(path, "r");

	if (file == NULL)
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot open input replay");

	line_number = 0;
	error = mh_success;
	status = MH_ERROR_NONE;

	while (fgets(line, sizeof(line), file) != NULL) {
		line_number++;

		if (strchr(line, '\n') == NULL && !feof(file)) {
			status = mh_error_set(
				&error, MH_ERROR_RANGE, "replay line too long");
			break;
		}

		if (!parse_controls(line, &input, &reset)) {
			status = mh_error_set(&error, MH_ERROR_INVALID,
				"invalid replay controls");
			break;
		}

		status = prepare_replay_tick(
			race_context, field, recording, reset, &error);

		if (status == MH_ERROR_NONE)
			status = advance_diagnostic_frame(
				race_context, field, &input, recording, &error);

		if (status != MH_ERROR_NONE)
			break;

		mh_race_trace_field(trace, field);

		if (trace != NULL && ferror(trace)) {
			status = mh_error_set(
				&error, MH_ERROR_IO, "trace write failed");
			break;
		}
	}

	if (status == MH_ERROR_NONE && ferror(file))
		status = mh_error_set(
			&error, MH_ERROR_IO, "input replay read failed");

	fclose(file);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Replay line %lu: %s\n", line_number,
			error.message);

	return mh_error_forward(error_out, &error);
}

enum mh_error_code
mh_debug_race_ticks(struct mh_race_context *race_context,
	struct mh_field *field, unsigned long ticks, FILE *trace,
	struct mh_demo_recording *recording, struct mh_error *error_out)
{
	struct mh_engine_input input;
	unsigned long tick;
	enum mh_error_code status;

	memset(&input, 0, sizeof(input));

	for (tick = 0; tick < ticks; tick++) {
		status = mh_race_scores_frame(race_context, field, error_out);

		if (status == MH_ERROR_NONE)
			status = advance_diagnostic_frame(race_context, field,
				&input, recording, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_race_trace_field(trace, field);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
