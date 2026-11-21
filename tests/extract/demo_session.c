/* Load prepared content through the ordinary single-player race owner. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/collision/provider.h"
#include "backends/font/provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/image/remap_provider.h"
#include "backends/image/tga_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/session/single_setup.h"
#include "client/settings/render_choice.h"
#include "engine/audio/ambient_assets.h"
#include "engine/audio/surface_assets.h"
#include "engine/game/mode.h"
#include "interfaces/audio/horn_source.h"

static struct mh_race_context context;
static struct mh_field field;
static struct mh_profile_catalog profiles;

enum {
	STARTUP_TICKS = 300,
	FINISH_TICKS = 60000
};

static enum mh_error_code
check_track_audio(const char *root, struct mh_error *error_out)
{
	struct mh_surface_assets surface;
	struct mh_ambient_assets ambient;
	const struct mh_track *track;
	enum mh_error_code status;

	memset(&surface, 0, sizeof(surface));
	memset(&ambient, 0, sizeof(ambient));
	track = &context.scene.assets.track;
	status = mh_surface_assets_load_from(mh_material_mat_srf_source(),
		mh_env_sounds_esd_source(), mh_audio_fallback_decoder(),
		mh_filesystem_resources(), root, track->materials_file,
		track->env_sound_file, &surface, error_out);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Surface bank (%s): %s\n",
			track->materials_file, error_out->message);

	if (status == MH_ERROR_NONE && track->env_sound_file[0] != 0)
		status = mh_ambient_assets_load_from(mh_env_sounds_esd_source(),
			mh_animation_lightwave_source(),
			mh_audio_fallback_decoder(), mh_filesystem_resources(),
			root, track->env_sound_file, track->scene_file,
			&ambient, error_out);

	mh_surface_assets_free(&surface);
	mh_ambient_assets_free(&ambient);

	return status;
}

static enum mh_error_code
advance_race(int finish, struct mh_error *error_out)
{
	struct mh_engine_input input;
	struct mh_results_screen screen;
	struct mh_ui_list list;
	const struct mh_standings *standings;
	unsigned long tick, limit;
	unsigned int i, positions;
	enum mh_error_code status;

	memset(&input, 0, sizeof(input));
	memset(&screen, 0, sizeof(screen));
	status = mh_drive_enable_ai(
		&field.cars[0], &context.scene.assets.ai_path, 2, error_out);
	limit = finish ? FINISH_TICKS : STARTUP_TICKS;

	for (tick = 0;
		status == MH_ERROR_NONE && tick < limit && !screen.visible;
		tick++) {
		status = mh_field_frame_tick_duration(&field, &input,
			tick % 2 == 0, tick % 2 == 0 ? 2 * MH_DRIVE_DT : 0,
			error_out);

		if (status == MH_ERROR_NONE && tick % 2 != 0)
			status = mh_field_frame_update(
				&field, 1.0f / 60, error_out);

		if (status == MH_ERROR_NONE && finish)
			status = mh_results_screen_update_local(&screen,
				&field.standings, 1,
				mh_drive_clock_ms(&field.cars[0]), error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	if (finish && (!screen.visible || !field.cars[0].race.finished))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"prepared demo local finisher did not reach results");

	if (finish) {
		status = mh_results_screen_prepare(
			&screen, &field, &field.ai_random, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!mh_results_draw(&context.results,
			mh_race_context_atlas(&context), &screen.final, 960,
			640, NULL, &list))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"prepared demo results drawing failed");
	}

	standings = finish ? &screen.final : &field.standings;
	printf("%lu ticks, %u cars, results=%d, physical finishers=%u\n", tick,
		field.count, screen.prepared, field.standings.finished_count);

	positions = 0;

	for (i = 0; i < field.count; i++) {
		printf("car %u: position %u, finished=%u\n", i + 1,
			standings->cars[i].position,
			standings->cars[i].finished);

		if (standings->cars[i].position < 1 ||
			standings->cars[i].position > field.count)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"invalid prepared demo standing");

		positions |= 1U << (standings->cars[i].position - 1);
	}

	if (positions != (1U << field.count) - 1)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"duplicate prepared demo standing");

	status = mh_field_reset(&field, 1, error_out);

	if (status == MH_ERROR_NONE && field.standings.complete)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"prepared demo restart retained completed standings");

	return status;
}

static int
run(const char *root, unsigned int renderer, unsigned int car,
	unsigned int track, int finish)
{
	struct mh_horn_catalog horns;
	struct mh_single_selection selection;
	struct mh_settings settings;
	struct mh_error error;
	unsigned long random;
	enum mh_error_code status;

	random = 1;
	status = mh_horn_catalog_load_from(mh_horn_fallback_source(),
		mh_filesystem_resources(), root, &horns, &error);

	if (status == MH_ERROR_NONE)
		status = mh_profile_catalog_load_from(
			mh_profile_fallback_source(), mh_filesystem_resources(),
			root, &horns, &random, &profiles, &error);

	if (status == MH_ERROR_NONE)
		status = mh_settings_init(
			&settings, "Driver", 255, 255, renderer, &error);

	if (status == MH_ERROR_NONE) {
		settings.car = car - 1;
		settings.track = track - 1;
		settings.game_type = MH_GAME_QUICK_RACE;
		settings.laps = 1;
		settings.automatic = 1;
		settings.catch_up = 1;
		status = mh_single_setup_load_from(mh_horn_fallback_source(),
			mh_material_mat_srf_source(),
			mh_env_sounds_esd_source(),
			mh_catalog_fallback_source(),
			mh_navigation_mot_dat_source(),
			mh_animation_lightwave_source(),
			mh_atlas_remap_source(), mh_model_myo_decoder(),
			mh_world_myw_decoder(), mh_collision_fallback_source(),
			mh_sprite_spr_source(), mh_font_fallback_source(),
			mh_image_pdi_source(), mh_image_fallback_decoder(),
			mh_image_tga_portrait_decoder(),
			mh_filesystem_resources(), root, &profiles, &horns,
			&settings, 1, &random, &context, &field, &selection,
			&error);
	}

	if (status == MH_ERROR_NONE)
		status = check_track_audio(root, &error);

	if (status == MH_ERROR_NONE)
		status = advance_race(finish, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_context_free(&context);

	return status == MH_ERROR_NONE ? 0 : 1;
}

int
main(int argc, char *argv[])
{
	unsigned int renderer;
	unsigned long car, track;
	char *end;

	if ((argc != 5 && argc != 6) ||
		(argc == 6 && strcmp(argv[5], "--finish") != 0) ||
		!mh_render_choice_parse(argv[2], &renderer)) {
		fprintf(stderr,
			"Usage: mh-demo-session-check ROOT RENDERER CAR TRACK [--finish]\n");
		return 1;
	}

	car = strtoul(argv[3], &end, 10);

	if (end == argv[3] || *end != 0 || car < 1 || car > 3)
		return 1;

	track = strtoul(argv[4], &end, 10);

	if (end == argv[4] || *end != 0 || track < 1 || track > 2)
		return 1;

	return run(argv[1], renderer, (unsigned int)car, (unsigned int)track,
		argc == 6);
}
