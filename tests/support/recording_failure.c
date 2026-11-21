/* Force real short writes without replacing the platform's stdio calls. */
#define _POSIX_C_SOURCE 200809L
#include <sys/resource.h>

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "client/session/playback_hud.h"
#include "engine/core/error.h"
#include "recording_failure.h"

#define RECORDING_LIMIT_BYTES 8192

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "recording failure line %d: %s\n",     \
				__LINE__, #x);                                 \
			goto done;                                             \
		}                                                              \
	} while (0)
#define TRY(x) CHECK((x) == MH_ERROR_NONE)

static enum mh_error_code
advance_race(struct mh_race_context *context, struct mh_field *field,
	struct mh_demo_race *race, struct mh_demo_recording *recording,
	struct mh_error *error_out)
{
	struct mh_engine_input controls;
	unsigned int steps;

	memset(&controls, 0, sizeof(controls));

	if (mh_demo_recording_active(recording))
		return mh_demo_recording_advance(recording, field, &controls,
			2 * MH_DRIVE_DT, 0, &steps,
			&context->scene.visuals.particle_system.random,
			error_out);

	if (race == NULL)
		return mh_field_advance_random(field, &controls,
			2 * MH_DRIVE_DT, 0, &steps,
			&context->scene.visuals.particle_system.random,
			error_out);

	return mh_demo_race_frame(
		race, &controls, 2 * MH_DRIVE_DT, 0, &steps, error_out);
}

int
mh_test_recording_failure(struct mh_race_context *context,
	struct mh_field *field, struct mh_demo_race *race,
	struct mh_demo_recording **recording, int menu)
{
	struct rlimit original, limit;
	struct sigaction action, previous;
	struct mh_playback_hud hud, deferred;
	struct mh_message_view view;
	struct mh_messages expected;
	struct mh_engine_input controls;
	enum mh_error_code status;
	enum mh_error_code write_status;
	unsigned long failed_at, revision, ticks;
	unsigned int i, steps;
	int saved, success, limit_applied, signal_ignored;

	success = limit_applied = signal_ignored = 0;
	write_status = MH_ERROR_NONE;
	memset(&controls, 0, sizeof(controls));

	if (menu)
		CHECK(field->cars[0].ticks == 0);

	CHECK(getrlimit(RLIMIT_FSIZE, &original) == 0);
	memset(&action, 0, sizeof(action));
	action.sa_handler = SIG_IGN;
	CHECK(sigemptyset(&action.sa_mask) == 0 &&
		sigaction(SIGXFSZ, &action, &previous) == 0);
	signal_ignored = 1;
	limit = original;
	limit.rlim_cur = RECORDING_LIMIT_BYTES;
	CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
	limit_applied = 1;
	mh_playback_hud_init(&hud);
	TRY(mh_playback_hud_frame(&hud, &context->hud, MH_VIEW_WIDTH, field,
		race, *recording, NULL));
	CHECK((hud.status_flags & 2) != 0);

	for (i = 0; i < 2000 && write_status == MH_ERROR_NONE; i++) {
		if (menu)
			status = mh_demo_recording_advance(*recording, field,
				&controls, i == 0 ? 6.0005L : .1L,
				MH_FRAME_MENU, &steps,
				&context->scene.visuals.particle_system.random,
				NULL);
		else
			status = advance_race(
				context, field, race, *recording, NULL);

		write_status = mh_demo_recording_write_error(*recording, NULL);
		CHECK(status == write_status);
		TRY(mh_playback_hud_frame(&hud, &context->hud, MH_VIEW_WIDTH,
			field, race, *recording, NULL));

		if (menu) {
			CHECK(steps == 0 && field->cars[0].ticks == 0);
			CHECK(mh_demo_recording_start_notice(*recording) &&
				mh_demo_recording_start_ticks(*recording) ==
					0 &&
				mh_demo_recording_start_ms(*recording) == 6001);
			CHECK((hud.delivered_notices & 4) != 0);

			if (i == 0) {
				CHECK(status == MH_ERROR_NONE &&
					hud.messages.revision ==
						(race == NULL ? 1UL : 2UL));
				CHECK(strcmp(hud.messages.rows[0].text,
					"Race recording started!") == 0 &&
					hud.messages.rows[0].milliseconds ==
						6001);
			}
		}
	}

	CHECK(write_status != MH_ERROR_NONE &&
		!mh_demo_recording_active(*recording) &&
		mh_demo_recording_failure_notice(*recording) &&
		(hud.delivered_notices & 8) != 0);
	failed_at = mh_demo_recording_failure_ticks(*recording);
	CHECK(failed_at == field->cars[0].ticks && (hud.status_flags & 2) == 0);

	/*
	 * Compare independently appended original text, including FONT2 wrap.
	 */
	mh_messages_init(&expected);
	TRY(mh_messages_width(&expected, 540, NULL));
	TRY(mh_messages_add(&expected, "Error!", " Could not write demo file!",
		0xe06820UL, (unsigned long)hud.clock_ms,
		context->hud.message_advance, NULL));
	CHECK(memcmp(&expected.rows[0], &hud.messages.rows[0],
		sizeof(expected.rows[0])) == 0);
	CHECK(hud.messages.rows[0].milliseconds ==
		mh_demo_recording_failure_ms(*recording));
	TRY(mh_messages_view(
		&hud.messages, hud.clock_ms, MH_VIEW_HEIGHT, 12, &view, NULL));
	CHECK(view.sound);
	revision = hud.messages.revision;
	mh_playback_hud_init(&deferred);
	TRY(mh_playback_hud_frame(&deferred, &context->hud, MH_VIEW_WIDTH,
		field, race, *recording, NULL));
	CHECK(memcmp(&hud, &deferred, sizeof(hud)) == 0);

	/* Pause/held frames neither retry IO nor duplicate the error. */
	for (i = 0; i < 5; i++) {
		CHECK(mh_demo_recording_frame(*recording, field, NULL) ==
			write_status);
		TRY(mh_playback_hud_frame(&hud, &context->hud, MH_VIEW_WIDTH,
			field, race, *recording, NULL));
		CHECK(memcmp(&hud, &deferred, sizeof(hud)) == 0);
	}

	for (i = 0; i < 60; i++) {
		TRY(advance_race(context, field, race, *recording, NULL));
		TRY(mh_playback_hud_frame(&hud, &context->hud, MH_VIEW_WIDTH,
			field, race, *recording, NULL));
	}

	CHECK(field->cars[0].ticks == failed_at + 60UL &&
		hud.messages.revision == revision);
	TRY(mh_messages_view(
		&hud.messages, hud.clock_ms, MH_VIEW_HEIGHT, 12, &view, NULL));
	CHECK(!view.sound);

	/*
	 * Even a zero-byte file limit cannot prevent Home: no take is reopened.
	 */
	limit.rlim_cur = 0;
	CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
	TRY(mh_demo_recording_restart(*recording, field, NULL));
	mh_playback_hud_restart(&hud, field);
	ticks = field->cars[0].ticks;
	CHECK(!mh_demo_recording_active(*recording) &&
		mh_demo_recording_start_ticks(*recording) == 0 &&
		mh_demo_recording_failure_ticks(*recording) == 0 &&
		!mh_demo_recording_start_notice(*recording) &&
		!mh_demo_recording_failure_notice(*recording));

	for (i = 0; i < 420; i++) {
		TRY(advance_race(context, field, race, *recording, NULL));
		TRY(mh_playback_hud_frame(&hud, &context->hud, MH_VIEW_WIDTH,
			field, race, *recording, NULL));
	}

	CHECK(field->cars[0].ticks == ticks + 420UL &&
		(hud.status_flags & 2) == 0 &&
		hud.messages.revision == revision + (race != NULL ? 1UL : 0UL));
	CHECK(mh_demo_recording_write_error(*recording, NULL) == write_status);
	status = mh_demo_recording_finish(*recording, field, &saved, NULL);
	*recording = NULL;
	CHECK(status == write_status && !saved);
	success = 1;

done:
	if (limit_applied && setrlimit(RLIMIT_FSIZE, &original) != 0)
		success = 0;

	if (signal_ignored && sigaction(SIGXFSZ, &previous, NULL) != 0)
		success = 0;

	if (success)
		printf("short recording write (%s): racing continues, original notice/icon, disabled restart and cleanup passed\n",
			menu ? "zero physics ticks" : "running");

	return success;
}
