#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/session/demo_recording.h"
#include "engine/core/error.h"
#include "engine/input/control_type.h"
#include "engine/storage/demo_file.h"

/* Largest 120 Hz tick count that converts to an unsigned 32-bit ms value. */
#define RECORDING_MAX_TICKS 515396075UL

enum {
	RECORDING_TICKS_PER_SECOND = 120,
	RECORDING_DELAY_MS = 6000,
	RECORDING_MIN_FRAMES = 64
};

struct mh_demo_recording {
	struct mh_demo_recorder	recorder;
	struct mh_demo_file	*file;
	unsigned long	last_ticks, origin_ticks, start_ticks, failure_ticks;
	unsigned long		start_ms, failure_ms;
	int			start_notice, failure_notice;
	struct mh_error		failure;
	int			write_failed;
	struct mh_demo_race	*ghost_race;
	struct mh_net_race	*network_race;
};

void
mh_demo_recording_abort(struct mh_demo_recording *recording)
{
	if (recording == NULL)
		return;

	mh_demo_file_abort(recording->file);
	free(recording);
}

unsigned long
mh_demo_recording_start_ticks(const struct mh_demo_recording *recording)
{
	return recording == NULL ? 0 : recording->start_ticks;
}

int
mh_demo_recording_start_notice(const struct mh_demo_recording *recording)
{
	return recording != NULL && recording->start_notice;
}

int
mh_demo_recording_failure_notice(const struct mh_demo_recording *recording)
{
	return recording != NULL && recording->failure_notice;
}

unsigned long
mh_demo_recording_start_ms(const struct mh_demo_recording *recording)
{
	return recording == NULL ? 0 : recording->start_ms;
}

unsigned long
mh_demo_recording_failure_ms(const struct mh_demo_recording *recording)
{
	return recording == NULL ? 0 : recording->failure_ms;
}

static unsigned long
lifetime_ms(const struct mh_drive *drive)
{
	return drive->frame.active
		? (unsigned long)((long double)drive->frame.seconds * 1000.0L)
		: (drive->ticks / RECORDING_TICKS_PER_SECOND) * 1000 +
			(drive->ticks % RECORDING_TICKS_PER_SECOND) * 1000 /
				RECORDING_TICKS_PER_SECOND;
}

enum mh_error_code
mh_demo_recording_write_error(
	const struct mh_demo_recording *recording, struct mh_error *error_out)
{
	return recording != NULL && recording->write_failed
		? mh_error_forward(error_out, &recording->failure)
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_recording_recover_write_error(const struct mh_demo_recording *recording,
	unsigned int *record_race, enum mh_error_code status,
	struct mh_error *error_out)
{
	struct mh_error error;

	if (status == MH_ERROR_NONE ||
		mh_demo_recording_write_error(recording, &error) ==
			MH_ERROR_NONE)
		return status;

	fprintf(stderr, "Recording stopped: %s\n", error.message);
	*record_race = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_demo_recording_active(const struct mh_demo_recording *recording)
{
	return recording != NULL && recording->failure.code == MH_ERROR_NONE;
}

unsigned long
mh_demo_recording_failure_ticks(const struct mh_demo_recording *recording)
{
	return recording == NULL ? 0 : recording->failure_ticks;
}

static enum mh_error_code
open_with_header(struct mh_data_directory *directory, const char *filename,
	int ignore_case, const struct mh_demo *header,
	struct mh_demo_recording **output, struct mh_error *error_out)
{
	struct mh_demo_recording *recording;
	enum mh_error_code status;

	if (directory == NULL || filename == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recording directory/name/output");

	recording = calloc(1, sizeof(*recording));

	if (recording == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening recording");

	status = mh_demo_record_begin(header, &recording->recorder, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_file_open_in(directory, filename, ignore_case,
			header, &recording->file, error_out);

	if (status != MH_ERROR_NONE) {
		mh_demo_recording_abort(recording);
		return status;
	}

	*output = recording;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_recording_open_in(struct mh_data_directory *directory,
	const char *filename, const struct mh_field *field,
	const struct mh_profile *profiles,
	const struct mh_control_binding_set *controls,
	struct mh_demo_recording **output, struct mh_error *error_out)
{
	struct mh_demo header;
	enum mh_error_code status;

	status = mh_demo_capture_header(field, profiles, &header, error_out);

	if (status == MH_ERROR_NONE && controls != NULL)
		header.players[0].control_type = mh_control_type_update(
			controls->codes[MH_CONTROL_LEFT], 0);

	if (status == MH_ERROR_NONE)
		status = open_with_header(
			directory, filename, 0, &header, output, error_out);

	if (status == MH_ERROR_NONE)
		(*output)->last_ticks = (*output)->origin_ticks =
			field->cars[0].ticks;

	return status;
}

enum mh_error_code
mh_demo_recording_open_ghost_in(struct mh_data_directory *directory,
	const char *filename, struct mh_demo_race *race,
	const struct mh_profile *human,
	const struct mh_control_binding_set *controls,
	struct mh_demo_recording **output, struct mh_error *error_out)
{
	struct mh_demo header;
	enum mh_error_code status;

	status = mh_ghost_capture_header(race, human, &header, error_out);

	if (status == MH_ERROR_NONE && controls != NULL)
		header.players[0].control_type = mh_control_type_update(
			controls->codes[MH_CONTROL_LEFT], 0);

	if (status == MH_ERROR_NONE)
		status = open_with_header(
			directory, filename, 0, &header, output, error_out);

	if (status == MH_ERROR_NONE) {
		(*output)->ghost_race = race;
		(*output)->last_ticks = (*output)->origin_ticks =
			race->field.cars[0].ticks;
	}

	return status;
}

static enum mh_error_code
capture_sample(struct mh_demo_recording *recording,
	const struct mh_field *field, struct mh_demo_record_sample *sample,
	struct mh_error *error_out)
{
	if (recording->network_race != NULL) {
		if (field != &recording->network_race->field)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"network recording field changed");

		return mh_network_capture_sample(recording->network_race,
			&recording->recorder.header, sample, error_out);
	}

	if (recording->ghost_race == NULL)
		return mh_demo_capture_sample(
			field, &recording->recorder.header, sample, error_out);

	if (field != &recording->ghost_race->field)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"Ghost recording field changed");

	return mh_ghost_capture_sample(recording->ghost_race,
		&recording->recorder.header, sample, error_out);
}

enum mh_error_code
mh_demo_recording_open_network_in(struct mh_data_directory *directory,
	const char *filename, struct mh_net_race *race,
	const struct mh_net_session *session, struct mh_demo_recording **output,
	struct mh_error *error_out)
{
	struct mh_demo header;
	enum mh_error_code status;

	status = mh_network_capture_header(race, session, &header, error_out);

	if (status == MH_ERROR_NONE)
		status = open_with_header(
			directory, filename, 1, &header, output, error_out);

	if (status == MH_ERROR_NONE)
		(*output)->network_race = race;

	return status;
}

static enum mh_error_code
append_sample(struct mh_demo_recording *recording, const struct mh_field *field,
	unsigned long clock_ms, const struct mh_demo_record_sample *sample,
	struct mh_error *error_out)
{
	struct mh_demo_record_event event;
	struct mh_error error;
	enum mh_error_code status;

	status = mh_demo_record_step(
		&recording->recorder, clock_ms, sample, &event, &error);

	if (status == MH_ERROR_NONE)
		status = mh_demo_file_append(recording->file, &event, &error);

	if (status == MH_ERROR_NONE && event.started) {
		recording->start_notice = 1;
		recording->start_ticks = field->cars[0].ticks;
		recording->start_ms = lifetime_ms(&field->cars[0]);
	}

	if (status != MH_ERROR_NONE &&
		mh_demo_file_write_failed(recording->file)) {
		recording->failure_notice = 1;
		recording->write_failed = 1;
		recording->failure_ticks = field->cars[0].ticks;
		recording->failure_ms = lifetime_ms(&field->cars[0]);
		mh_demo_file_abort(recording->file);
		recording->file = NULL;
	}

	recording->failure = error;

	return mh_error_forward(error_out, &error);
}

enum mh_error_code
mh_demo_recording_advance(struct mh_demo_recording *recording,
	struct mh_field *field, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	unsigned long *random, struct mh_error *error_out)
{
	struct mh_demo_capture_frame frame;
	enum mh_error_code status;

	if (recording == NULL || field == NULL ||
		recording->network_race != NULL ||
		(recording->ghost_race != NULL &&
			field != &recording->ghost_race->field))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording advance binding");

	if (recording->failure.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &recording->failure);

	if (field->cars[0].ticks < recording->last_ticks ||
		field->cars[0].ticks < recording->origin_ticks)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"recording clock moved backwards");

	status = recording->ghost_race == NULL
		? mh_demo_capture_advance(field, controls, elapsed, action,
			&recording->recorder.header, &frame, steps, random,
			error_out)
		: mh_ghost_capture_advance(recording->ghost_race, controls,
			elapsed, action, &recording->recorder.header, &frame,
			steps, random, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * All simulation stages succeeded. Only these owned bytes cross into
	 * storage; a later write error must not rewind the completed race.
	 */
	recording->last_ticks = field->cars[0].ticks;

	return frame.ready ? append_sample(recording, field, frame.clock_ms,
				&frame.sample, error_out)
			: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_recording_network_advance(struct mh_demo_recording *recording,
	struct mh_net_session *session, const struct mh_engine_input *controls,
	long double elapsed, double now_ms, unsigned int viewed_entry,
	unsigned int *steps, struct mh_error *error_out)
{
	struct mh_demo_capture_frame frame;
	enum mh_error_code status;

	if (recording == NULL || recording->network_race == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network recording binding");

	if (recording->failure.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &recording->failure);

	if (recording->network_race->field.cars[0].ticks <
		recording->last_ticks)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"recording clock moved backwards");

	status = mh_network_capture_advance(recording->network_race, session,
		controls, elapsed, now_ms, viewed_entry,
		&recording->recorder.header, &frame, steps, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	recording->last_ticks = recording->network_race->field.cars[0].ticks;

	return frame.ready
		? append_sample(recording, &recording->network_race->field,
			frame.clock_ms, &frame.sample, error_out)
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_recording_frame(struct mh_demo_recording *recording,
	const struct mh_field *field, struct mh_error *error_out)
{
	struct mh_demo_record_sample sample;
	unsigned long ticks, clock_ms;
	struct mh_error error;
	enum mh_error_code status;
	int ready;

	if (recording == NULL || field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing recording/field");

	if (recording->failure.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &recording->failure);

	if (recording->network_race != NULL) {
		if (field != &recording->network_race->field)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"network recording field changed");

		if (!recording->network_race->started)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		recording->origin_ticks = recording->network_race->start_tick;
	}

	ticks = field->cars[0].ticks;

	/* Bound before conversion on both 32- and 64-bit hosts. */
	if (ticks < recording->last_ticks || ticks < recording->origin_ticks ||
		(!field->cars[0].frame.active &&
			ticks - recording->origin_ticks >
				RECORDING_MAX_TICKS)) {
		mh_error_set(&recording->failure, MH_ERROR_RANGE,
			"recording clock moved backwards or exceeded disk range");
		return mh_error_forward(error_out, &recording->failure);
	}

	recording->last_ticks = ticks;
	ticks -= recording->origin_ticks;

	if (field->cars[0].frame.active) {
		status = mh_frame_media_time(field->cars[0].frame.seconds,
			field->cars[0].frame.intro_ms, &ready, &clock_ms,
			&error);

		if (status != MH_ERROR_NONE) {
			recording->failure = error;
			return mh_error_forward(error_out, &recording->failure);
		}

		if (!ready)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	} else {
		clock_ms = (ticks / RECORDING_TICKS_PER_SECOND) * 1000 +
			(ticks % RECORDING_TICKS_PER_SECOND) * 1000 /
				RECORDING_TICKS_PER_SECOND;

		if (clock_ms <= RECORDING_DELAY_MS)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = capture_sample(recording, field, &sample, &error);

	if (status != MH_ERROR_NONE) {
		recording->failure = error;
		return mh_error_forward(error_out, &recording->failure);
	}

	return append_sample(recording, field, clock_ms, &sample, error_out);
}

static enum mh_error_code
restart_without_recording(struct mh_demo_recording *recording,
	struct mh_field *field, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (recording->ghost_race != NULL &&
		field != &recording->ghost_race->field)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"Ghost recording field changed");

	status = recording->ghost_race == NULL
		? mh_field_reset(field, 1, error_out)
		: mh_demo_race_restart(recording->ghost_race, error_out);

	if (status == MH_ERROR_NONE) {
		recording->start_notice = recording->failure_notice = 0;
		recording->start_ticks = recording->failure_ticks =
			recording->start_ms = recording->failure_ms = 0;
	}

	return status;
}

enum mh_error_code
mh_demo_recording_restart(struct mh_demo_recording *recording,
	struct mh_field *field, struct mh_error *error_out)
{
	struct mh_field next_field;
	struct mh_demo_race *next_ghost;
	struct mh_demo_recorder next_recorder;
	struct mh_demo_record_sample sample;
	enum mh_error_code status;

	if (recording == NULL || field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing recording/field");

	if (recording->network_race != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network recording ends on return to lobby");

	if (recording->write_failed)
		return restart_without_recording(recording, field, error_out);

	if (recording->failure.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &recording->failure);

	next_ghost = NULL;

	if (recording->ghost_race != NULL) {
		if (field != &recording->ghost_race->field)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"Ghost recording field changed");

		next_ghost = (struct mh_demo_race *)malloc(sizeof(*next_ghost));

		if (next_ghost == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory restarting Ghost recording");

		*next_ghost = *recording->ghost_race;
		status = mh_demo_race_restart(next_ghost, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_ghost_capture_sample(next_ghost,
				&recording->recorder.header, &sample,
				error_out);

		next_field = next_ghost->field;
	} else {
		next_field = *field;
		status = mh_field_reset(&next_field, 1, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_demo_capture_sample(&next_field,
				&recording->recorder.header, &sample,
				error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_demo_record_begin(
			&recording->recorder.header, &next_recorder, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_file_restart(
			recording->file, &next_recorder.header, error_out);

	if (status != MH_ERROR_NONE) {
		free(next_ghost);
		return status;
	}

	recording->recorder = next_recorder;

	/*
	 * Local and Ghost restarts preserve lifetime time and slice counts,
	 * while beginning a fresh recording gate.
	 */
	recording->last_ticks = recording->origin_ticks =
		next_field.cars[0].ticks;
	recording->start_ticks = recording->start_ms = 0;
	recording->start_notice = recording->failure_notice = 0;

	if (next_ghost != NULL)
		*recording->ghost_race = *next_ghost;
	else
		*field = next_field;
	free(next_ghost);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
capture_crossings(const struct mh_demo_recording *recording,
	const struct mh_field *field, struct mh_demo_player *players,
	struct mh_error *error_out)
{
	if (recording->network_race != NULL)
		return mh_network_capture_crossings(
			recording->network_race, players, error_out);

	return recording->ghost_race == NULL
		? mh_demo_capture_crossings(field, players, error_out)
		: mh_ghost_capture_crossings(
			recording->ghost_race, players, error_out);
}

enum mh_error_code
mh_demo_recording_prepare(struct mh_demo_recording *recording,
	const struct mh_field *field, struct mh_demo_file **take,
	struct mh_error *error_out)
{
	struct mh_demo_record_sample sample;
	struct mh_demo_record_event event;
	struct mh_demo_player players[MH_DEMO_MAX_PLAYERS];
	struct mh_error error;
	enum mh_error_code status;

	if (take != NULL)
		*take = NULL;

	if (recording == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing recording");

	error = recording->failure;
	status = error.code;

	if (take == NULL)
		status = mh_error_set(
			&error, MH_ERROR_INVALID, "missing recording result");

	if (status == MH_ERROR_NONE)
		status = capture_sample(recording, field, &sample, &error);

	if (status == MH_ERROR_NONE)
		status = capture_crossings(recording, field, players, &error);

	if (status == MH_ERROR_NONE)
		status = mh_demo_record_finish(&recording->recorder, &sample,
			players, recording->recorder.initial_mask, &event,
			&error);

	if (status == MH_ERROR_NONE &&
		recording->recorder.frame_count >= RECORDING_MIN_FRAMES) {
		status = mh_demo_file_append(recording->file, &event, &error);

		if (status == MH_ERROR_NONE) {
			status = mh_demo_file_seal(recording->file,
				&recording->recorder.header,
				recording->recorder.frame_count, &error);

			if (status == MH_ERROR_NONE) {
				*take = recording->file;
				recording->file = NULL;
			}
		}
	}

	mh_demo_recording_abort(recording);

	return mh_error_forward(error_out, &error);
}

enum mh_error_code
mh_demo_recording_finish(struct mh_demo_recording *recording,
	const struct mh_field *field, int *saved, struct mh_error *error_out)
{
	struct mh_demo_file *take;
	enum mh_error_code status;

	if (saved == NULL) {
		mh_demo_recording_abort(recording);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recording result");
	}

	*saved = 0;
	status = mh_demo_recording_prepare(recording, field, &take, error_out);

	if (status == MH_ERROR_NONE && take != NULL) {
		status = mh_demo_file_publish(take, error_out);

		if (status == MH_ERROR_NONE)
			*saved = 1;
	}

	mh_demo_file_abort(take);

	return status;
}
