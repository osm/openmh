#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "client/diagnostics/media_video.h"
#include "client/hosts/sdl_video.h"
#include "interfaces/video/backend.h"

static Uint32 SDLCALL
inject_event(void *context, SDL_TimerID id, Uint32 interval)
{
	SDL_Event event;
	int mode;

	(void)id;
	(void)interval;
	mode = *(int *)context;
	memset(&event, 0, sizeof(event));
	event.type = mode == 3 ? SDL_EVENT_WINDOW_CLOSE_REQUESTED
			: SDL_EVENT_KEY_DOWN;
	event.key.key = mode == 1 ? SDLK_RETURN : SDLK_ESCAPE;

	if (mode == 4)
		event.type = SDL_EVENT_QUIT;

	SDL_PushEvent(&event);

	if (mode == 4) {
		memset(&event, 0, sizeof(event));
		event.type = SDL_EVENT_KEY_DOWN;
		event.key.key = SDLK_ESCAPE;
		SDL_PushEvent(&event);
	}

	return 0;
}

static int
check_memory_video(const char *path)
{
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_file data;
	struct mh_video_options options;
	struct mh_video_player *player;
	int outcome, ok;

	if (mh_resource_source_read(mh_filesystem_resources(), path, &data,
		NULL) != MH_ERROR_NONE)
		return 0;

	entry.path = "memory-only.smk";
	entry.data = data.data;
	entry.size = data.size;

	if (mh_memory_resources_open(&entry, 1, &resources, NULL) !=
		MH_ERROR_NONE) {
		free(data.data);
		return 0;
	}

	memset(&options, 0, sizeof(options));
	options.resources = resources;
	options.reference = entry.path;
	options.mute = 1;
	player = NULL;
	ok = mh_video_open_from(mh_host_sdl_videos(), &options, &outcome,
		&player, NULL) == MH_ERROR_NONE;

	if (ok)
		ok = mh_video_step(player) == MH_VIDEO_RUNNING;

	if (player != NULL)
		ok = !mh_video_close(player) &&
			outcome == MH_VIDEO_NO_OUTCOME && ok;

	mh_memory_resources_close(resources);
	free(data.data);

	return ok;
}

int
main(int argc, char *argv[])
{
	SDL_TimerID timer;
	SDL_Window *retained;
	SDL_WindowID identity;
	struct mh_video_player *player;
	enum mh_video_status status;
	unsigned long delay;
	int mode, outcome, success;

	if (argc != 2)
		return 2;

	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
	SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		return 1;

	retained = SDL_CreateWindow(
		"Retained frontend", 64, 64, SDL_WINDOW_HIDDEN);

	if (retained == NULL)
		return 1;

	identity = SDL_GetWindowID(retained);

	if (!check_memory_video(argv[1]) ||
		SDL_GetWindowFromID(identity) != retained)
		return 1;

	/*
	 * Cancellation releases only player-owned resources and publishes no
	 * end.
	 */
	for (mode = 0; mode < 2; mode++) {
		player = NULL;

		if (!mh_video_open(argv[1], 1, NULL, 0, &outcome, &player))
			return 1;

		if (mode != 0 && mh_video_step(player) != MH_VIDEO_RUNNING)
			return 1;

		if (mh_video_close(player) || outcome != 0 ||
			SDL_GetWindowFromID(identity) != retained)
			return 1;
	}

	for (mode = 0; mode < 5; mode++) {
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
		SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
		timer = SDL_AddTimer(1000, inject_event, &mode);

		if (timer == 0)
			return 1;

		if (mode % 2 != 0)
			success = mh_video_play(
				argv[1], mode == 2, NULL, 0, &outcome);
		else {
			if (!mh_video_open(argv[1], mode == 2, NULL, 0,
				&outcome, &player))
				return 1;

			do {
				status = mh_video_step(player);
				delay = mh_video_delay(player);

				if (delay != 0)
					SDL_Delay(delay);
			} while (status == MH_VIDEO_RUNNING);

			if (mh_video_step(player) != status ||
				mh_video_delay(player) != 0 || outcome != 0)
				return 1;

			success = mh_video_close(player);
		}

		SDL_RemoveTimer(timer);

		if (SDL_GetWindowFromID(identity) != retained ||
			!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
			fprintf(stderr,
				"movie destroyed the retained frontend\n");
			return 1;
		}

		if (!success || outcome != (mode >= 3 ? 3 : 2)) {
			fprintf(stderr, "movie input case %d returned %d/%d\n",
				mode, success, outcome);
			return 1;
		}
	}

	SDL_DestroyWindow(retained);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	puts("Movie memory resources, lifecycle cancellation, terminal steps, Enter/Escape, muted skip, close precedence and retained frontend passed");

	return 0;
}
