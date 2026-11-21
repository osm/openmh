#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/audio/sdl_provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_provider.h"
#include "backends/retro/provider.h"
#include "client/diagnostics/media_audio.h"
#include "client/diagnostics/options.h"
#include "client/diagnostics/render_renderer.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_race.h"
#include "client/hosts/capabilities.h"
#include "client/hosts/screenshots.h"
#include "client/hosts/storage.h"
#include "client/session/loading.h"
#include "engine/clock/game_timer.h"
#include "engine/settings/av.h"
#include "interfaces/audio/backend.h"
#include "interfaces/clock/clock.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/service.h"
#include "interfaces/storage/user_data_backend.h"

static int
run_session(struct mh_race_context *context, struct mh_field *field,
	const char *capture, int headless, int results, const char *root,
	const struct mh_audio_options *audio_options, FILE *trace,
	const struct mh_race_session_hooks *session,
	const struct mh_race_results_handler *result_handler, int *completed,
	int *quit, struct mh_audio **music, int *attract_exit,
	const struct mh_debug_options *debug)
{
	struct mh_race_loop_options options;

	memset(&options, 0, sizeof(options));

	if (debug != NULL)
		options.debug = *debug;

	options.capture = capture;
	options.headless = headless;
	options.results = results;
	options.root = root;
	options.audio_options = audio_options;
	options.trace = trace;
	options.session = session;
	options.result_handler = result_handler;
	options.completed = completed;
	options.quit = quit;
	options.music = music;
	options.attract_exit = attract_exit;

	return mh_diagnostic_race_run(context, field, &options, NULL);
}

int
mh_diagnostic_race_run(struct mh_race_context *context, struct mh_field *field,
	struct mh_race_loop_options *options,
	struct mh_session_loading *loading)
{
	struct mh_race_loop *loop;
	enum mh_app_status app_status;
	enum mh_error_code status;
	struct mh_error error;

	options->renderers = mh_sdl_render_service()->renderers;
	options->windows = mh_sdl_render_service()->windows;
	options->inputs = mh_sdl_input_provider();
	options->audio_provider = mh_sdl_audio_provider();
	options->retro_source = mh_retro_fallback_source();
	options->audio_decoder = mh_audio_fallback_decoder();
	options->audio_trace = mh_debug_audio_trace(&options->debug);
	options->timer = mh_game_clock_timer();
	options->user_data = mh_capability_available(MH_CAP_SCREENSHOTS)
		? mh_host_user_data()
		: NULL;

	options->screenshot_encoder = mh_host_screenshot_encoder(
		context->settings != NULL ? context->settings->screenshot_format
					: mh_screenshot_format_default());

	options->screenshot_directory = options->debug.screenshot_directory;
	options->loading = loading == NULL ? NULL : loading->view;

	status = mh_race_loop_open(context, field, options, &loop, &error);

	if (loading != NULL)
		mh_session_loading_close(loading);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Race: %s\n", error.message);
		return 0;
	}

	do {
		app_status = mh_race_loop_step(loop);

		if (mh_race_loop_delay(loop) != 0)
			(void)mh_clock_source_delay(options->timer->source,
				mh_race_loop_delay(loop), NULL);
	} while (app_status == MH_APP_RUNNING);

	return mh_race_loop_close(loop);
}

int
mh_race_run(struct mh_race_context *context, struct mh_field *field,
	const char *capture, int headless, int results, const char *root,
	const struct mh_audio_options *audio_options, FILE *trace,
	const struct mh_debug_options *debug)
{
	return run_session(context, field, capture, headless, results, root,
		audio_options, trace, NULL, NULL, NULL, NULL, NULL, NULL,
		debug);
}

int
mh_race_run_session(struct mh_race_context *context, struct mh_field *field,
	const char *root, const struct mh_audio_options *audio_options,
	FILE *trace, const struct mh_race_session_hooks *session,
	const struct mh_debug_options *debug)
{
	if (session == NULL)
		return 0;

	return run_session(context, field, NULL, 0, session->initial_results,
		root, audio_options, trace, session, NULL, NULL, NULL, NULL,
		NULL, debug);
}

int
mh_race_run_local(struct mh_race_context *context, struct mh_field *field,
	const char *root, const struct mh_audio_options *audio_options,
	const struct mh_race_session_hooks *session,
	const struct mh_race_results_handler *results, int *completed,
	int *quit, struct mh_audio **music,
	const struct mh_debug_options *debug)
{
	if (music != NULL)
		*music = NULL;

	if (completed == NULL || quit == NULL)
		return 0;

	*completed = *quit = 0;

	if (results == NULL || results->commit == NULL || field == NULL ||
		field->network || (session != NULL && !session->pause_with_ui))
		return 0;

	return run_session(context, field, NULL, 0, 0, root, audio_options,
		NULL, session, results, completed, quit, music, NULL, debug);
}

int
mh_race_run_attract(struct mh_race_context *context, struct mh_field *field,
	const char *root, const struct mh_audio_options *audio_options,
	const struct mh_race_session_hooks *session, int *manual_exit,
	int *quit, const struct mh_debug_options *debug)
{
	if (manual_exit == NULL || quit == NULL)
		return 0;

	*manual_exit = *quit = 0;

	if (field == NULL || !field->attract || field->network ||
		(session != NULL &&
			(!session->pause_with_ui || session->initial_results)))
		return 0;

	return run_session(context, field, NULL, 0, 0, root, audio_options,
		NULL, session, NULL, NULL, quit, NULL, manual_exit, debug);
}

int
mh_race_capture_session(struct mh_race_context *context, struct mh_field *field,
	const char *capture, int headless, const char *root,
	const struct mh_audio_options *audio_options,
	const struct mh_race_session_hooks *session,
	const struct mh_debug_options *debug)
{
	if (capture == NULL || session == NULL)
		return 0;

	return run_session(context, field, capture, headless,
		session->initial_results, root, audio_options, NULL, session,
		NULL, NULL, NULL, NULL, NULL, debug);
}
