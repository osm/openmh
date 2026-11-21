#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "client/diagnostics/asset_audio.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/asset_ui.h"
#include "client/race/race.h"
#include "engine/audio/ambient_assets.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"

static int
club_music(const char *root)
{
	struct mh_track track;
	struct mh_ambient_assets assets;
	struct mh_mixer mixer;
	struct mh_sound_listener listener;
	struct mh_error error;
	enum mh_error_code status;
	static const double eye[3] = {476, -37, -188},
			target[3] = {478, -37, -191};

	float pcm[2048];
	unsigned int i, voice;
	int audible;

	memset(&assets, 0, sizeof(assets));
	status = mh_track_config_load(root, 7, &track, &error);

	if (status == MH_ERROR_NONE)
		status = mh_ambient_assets_load(root, track.env_sound_file,
			track.scene_file, &assets, &error);

	voice = assets.bank.count;

	for (i = 0; i < assets.bank.count; i++)
		if (strcmp(assets.bank.sounds[i].sample, "dance2.wav") == 0)
			voice = i;

	if (status == MH_ERROR_NONE && voice == assets.bank.count)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"Club TBL music emitter missing");

	mh_sound_listener_camera(eye, target, &listener);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_init(48000, 1, &mixer, &error);

	if (status == MH_ERROR_NONE)
		status = mh_ambient_update(
			&mixer, &assets.bank, &listener, 0, &error);

	if (status == MH_ERROR_NONE &&
		(!mixer.voices[voice].active || mixer.voices[voice].left <= 0 ||
			mixer.voices[voice].right <= 0))
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"Club TBL music inaudible at greeting location");

	/*
	 * Isolate this original emitter while retaining its prepared spatial
	 * gains, so unrelated industrial ambience cannot satisfy the check.
	 */
	if (status == MH_ERROR_NONE) {
		for (i = 0; i < assets.bank.count; i++)
			if (i != voice)
				mh_mixer_stop(&mixer, i);

		status = mh_mixer_render(&mixer, pcm, 1024, 0, &error);
	}

	audible = 0;

	if (status == MH_ERROR_NONE)
		for (i = 0; i < 2048; i++)
			if (pcm[i] > .001f || pcm[i] < -.001f)
				audible = 1;

	if (status == MH_ERROR_NONE && !audible)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"Club TBL sample rendered silence");

	mh_ambient_assets_free(&assets);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Club TBL audio: %s\n", error.message);

	return status == MH_ERROR_NONE;
}

static int
capture(struct mh_renderer *renderer, const struct mh_hud_assets *assets,
	const char *track, const float xyz[3], unsigned int frames,
	const char *path)
{
	struct mh_hud_secrets secrets;
	struct mh_hud_state hud;
	struct mh_ui_list list;
	unsigned int i;

	memset(&secrets, 0, sizeof(secrets));
	memset(&hud, 0, sizeof(hud));
	hud.marker_count = 1;
	hud.map_mode = 2;
	hud.markers[0].active = hud.markers[0].viewed = 1;
	memcpy(hud.markers[0].world, xyz, sizeof(hud.markers[0].world));

	for (i = 0; i < frames; i++)
		mh_hud_secrets_frame(&secrets, track, &hud, MH_VIEW_WIDTH,
			assets->secret_scroll_length);

	mh_ui_begin(&list, MH_VIEW_WIDTH, MH_VIEW_HEIGHT);

	if (!mh_hud_secrets_draw(assets, &hud.secrets, &list) ||
		list.count == 0)
		return 0;

	if (strcmp(track, "RuhrStadt") == 0 &&
		(!secrets.club_credits || hud.secrets.notice != 1))
		return 0;

	if (strcmp(track, "Atlantika") == 0 &&
		(hud.secrets.notice != 2 || !secrets.scroll_started))
		return 0;

	return mh_renderer_ui_frame(renderer, &list) &&
		mh_renderer_capture(renderer, path);
}

int
main(int argc, char *argv[])
{
	struct mh_hud_assets assets;
	struct mh_renderer *renderer;
	struct mh_error error;
	enum mh_error_code status;
	char path[1024];
	int success;
	static const float club[3] = {476, -37, -188},
			atlantika[3] = {159, 12, -436};

	if (argc != 3 || strlen(argv[2]) > 900)
		return 2;

	if (!club_music(argv[1]))
		return 1;

	status = mh_hud_assets_load(argv[1], &assets, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "HUD assets: %s\n", error.message);
		return 1;
	}

	success = assets.hardware_cell_width == 16 &&
		assets.hardware_height == 12 &&
		assets.secret_scroll_length == 2006 &&
		strncmp(assets.secret_scroll,
			"Welcome to HCR&D's hidden scroll,", 32) == 0;
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		free(assets.atlas.image.rgba);
		return 1;
	}

	renderer = mh_sdl_renderer_create_scene(NULL, NULL, 0);
	success = success && renderer != NULL &&
		mh_renderer_ui_upload(renderer, &assets.atlas);
	sprintf(path, "%s-club.bmp", argv[2]);

	if (success)
		success = capture(
			renderer, &assets, "RuhrStadt", club, 203, path);

	sprintf(path, "%s-atlantika.bmp", argv[2]);

	if (success)
		success = capture(
			renderer, &assets, "Atlantika", atlantika, 1200, path);

	if (!success)
		fprintf(stderr, "Hidden HUD asset/render check failed: %s\n",
			SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();
	free(assets.atlas.image.rgba);

	if (success)
		puts("Club TBL greeting/credits latch and audible music, original font/scroll and animated Atlantika overlay passed");

	return !success;
}
