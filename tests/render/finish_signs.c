#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/input/sdl_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/diagnostics/asset_scene.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/render_renderer.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/internal.h"

#define TRY(call)                                                              \
	do {                                                                   \
		if ((call) != MH_ERROR_NONE)                                   \
			goto done;                                             \
	} while (0)

static struct mh_race_context context;
static struct mh_field field;
static unsigned int received_position, position_calls;

static unsigned int
position(void *opaque, unsigned int fallback)
{
	(void)opaque;
	(void)fallback;

	position_calls++;

	return received_position;
}

static int
capture_frame(struct mh_race_loop *loop, const char *path)
{
	struct mh_file picture;
	FILE *file;

	if (!mh_renderer_tga_capture(loop->renderer, &picture))
		return 0;

	file = fopen(path, "wb");

	if (file == NULL) {
		free(picture.data);
		return 0;
	}

	if (fwrite(picture.data, 1, picture.size, file) != picture.size) {
		free(picture.data);
		fclose(file);
		return 0;
	}

	free(picture.data);

	if (fclose(file) != 0)
		return 0;

	return 1;
}

static int
capture_samples(struct mh_race_loop *loop, const char *prefix)
{
	char path[1024];
	unsigned int sample;
	int gate;

	/* Same camera/HUD for podium and hidden-rank pixel comparisons. */
	field.standings.cars[0].position = 1;
	loop->finish_position = 1;
	loop->finish_seconds = .99f;
	loop->frame_updated = 0;

	for (sample = 0; sample < 10; sample++) {
		gate = sample >= 5;
		received_position = gate ? 1 : sample;
		loop->finish_seen = sample != 5;
		field.time_attack = sample == 6;
		field.viewed_entry = sample == 8 ? 1 : 0;
		loop->viewed = &field.cars[field.viewed_entry];
		loop->viewed->camera_mode =
			sample == 7 ? MH_CAMERA_TRACK : MH_CAMERA_CHASE;
		loop->viewed->camera_focal = MH_CAMERA_RACE_FOCAL;
		loop->success = loop->running = 1;

		if (sample == 9)
			loop->finish_seconds = .25f;

		if (!mh_race_draw_frame(loop))
			return 0;

		if (position_calls !=
			(sample < 5		     ? sample + 1
					: sample < 9 ? 5
						: 6))
			return 0;

		sprintf(path, "%s-%u.tga", prefix, sample);

		if (!capture_frame(loop, path))
			return 0;
	}

	return 1;
}

int
main(int argc, char *argv[])
{
	struct mh_race_loop_options options;
	struct mh_race_session_hooks session;
	struct mh_audio_options audio;
	struct mh_av_settings settings;
	struct mh_drive drive;
	const struct mh_car *opponents[1];
	struct mh_race_loop *loop;
	struct mh_error error;
	unsigned int profile;
	int success;

	if (argc != 4 || strlen(argv[3]) > 900)
		return 2;

	profile = (unsigned int)atoi(argv[2]);

	if (profile >= MH_RENDER_PROFILE_COUNT)
		return 2;

	success = 0;
	loop = NULL;
	TRY(mh_av_settings_init(&settings, 0, 0, &error));
	settings.width = 640;
	settings.height = 400;
	TRY(mh_race_context_load_profile(argv[1], 1, 1, 1, &settings,
		(enum mh_render_profile)profile, NULL, &context, &error));
	TRY(mh_race_scene_load_ai(argv[1], &context.scene, &error));
	TRY(mh_race_scene_add_opponents(
		argv[1], 1, 1, 1, &context.scene, &error));
	TRY(mh_race_context_load_hud(argv[1], &context, &error));
	TRY(mh_results_assets_load(argv[1], 2, "Finish",
		mh_race_context_atlas(&context), &context.results, &error));
	TRY(mh_drive_start(&context.scene.assets.car,
		&context.scene.assets.world_collision,
		&context.scene.assets.materials, &context.scene.assets.route,
		&context.scene.assets.track, 0, 0, 1, &drive, &error));
	opponents[0] = &context.scene.assets.opponents[0];
	TRY(mh_field_start(&drive, opponents, 1, &context.scene.assets.ai_path,
		2, &field, &error));
	memset(&options, 0, sizeof(options));
	memset(&session, 0, sizeof(session));
	memset(&audio, 0, sizeof(audio));
	options.renderers = mh_diagnostic_renderers();
	options.windows = mh_sdl_window_system();
	options.inputs = mh_sdl_input_provider();
	options.timer = mh_game_clock_timer();
	options.headless = 1;
	options.root = argv[1];
	options.capture = argv[3];
	options.audio_options = &audio;
	session.finish_position = position;
	options.session = &session;
	TRY(mh_race_loop_open(&context, &field, &options, &loop, &error));
	field.cars[0].race.finished = 1;
	TRY(mh_field_camera_finish(&field, 0, &error));
	TRY(mh_field_camera_update(&field, 0, &error));
	success = capture_samples(loop, argv[3]);

done:
	if (!success && error.message != NULL)
		fprintf(stderr, "%s\n", error.message);

	if (loop != NULL && !mh_race_loop_close(loop))
		success = 0;

	mh_race_context_free(&context);

	return success ? 0 : 1;
}
