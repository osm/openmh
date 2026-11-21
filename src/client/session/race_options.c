#include <string.h>

#include "client/race/race.h"
#include "client/session/options.h"
#include "client/session/race_options.h"
#include "engine/render/scene_assets.h"

void
mh_session_race_options(const struct mh_session_options *options,
	struct mh_loading_view *loading, struct mh_race_loop_options *race)
{
	memset(race, 0, sizeof(*race));

	race->loading = loading;
	race->wait_for_loading = options->wait_for_loading;
	race->renderers = options->renderers;
	race->windows = options->windows;
	race->window = options->window;
	race->inputs = options->inputs;
	race->retro_source = options->retro_source;
	race->audio_decoder = options->audio_decoder;
	race->audio_provider = options->audio_provider;
	race->debug = options->debug;
	race->audio_trace = options->audio_trace;
	race->timer = options->timer;
	race->user_data = options->screenshot_data;
	race->screenshot_encoder = options->screenshot_encoder;
	race->screenshot_directory = options->screenshot_directory;
}

enum mh_error_code
mh_session_course_music(const struct mh_session_options *options,
	struct mh_race_scene_assets *assets, struct mh_error *error_out)
{
	if (options->course_music != NULL) {
		if (assets->track_number < 1 ||
			assets->track_number > options->course_music_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid course music index");

		assets->track.cd_track =
			options->course_music[assets->track_number - 1];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
