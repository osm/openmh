#include <stdio.h>
#include <string.h>

#include "client/race/race.h"
#include "client/session/options.h"
#include "client/session/race_options.h"
#include "interfaces/storage/user_data_backend.h"
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "session race options:%d: %s\n",       \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_session_options options;
	struct mh_race_loop_options race;
	struct mh_race_scene_assets assets;
	struct mh_user_data saves, screenshots;
	unsigned int music[] = {7, 3};

	memset(&options, 0, sizeof(options));
	memset(&race, 0, sizeof(race));
	memset(&assets, 0, sizeof(assets));
	memset(&saves, 0, sizeof(saves));
	memset(&screenshots, 0, sizeof(screenshots));
	assets.track.cd_track = 9;
	CHECK(mh_session_course_music(&options, &assets, NULL) ==
		MH_ERROR_NONE);
	CHECK(assets.track.cd_track == 9);
	options.course_music = music;
	options.course_music_count = 2;
	CHECK(mh_session_course_music(&options, &assets, NULL) ==
		MH_ERROR_INVALID);
	CHECK(assets.track.cd_track == 9);
	assets.track_number = 3;
	CHECK(mh_session_course_music(&options, &assets, NULL) ==
		MH_ERROR_INVALID);
	CHECK(assets.track.cd_track == 9);
	assets.track_number = 2;
	CHECK(mh_session_course_music(&options, &assets, NULL) ==
		MH_ERROR_NONE);
	CHECK(assets.track.cd_track == 3);
	options.course_music_count = 0;
	CHECK(mh_session_course_music(&options, &assets, NULL) ==
		MH_ERROR_INVALID);
	CHECK(assets.track.cd_track == 3);

	/*
	 * A reused options object must not carry mode-specific state or
	 * save-data ownership into screenshots. Mode owners attach their hooks
	 * afterward.
	 */
	race.headless = race.results = 1;
	race.root = "previous";
	options.wait_for_loading = 1;
	options.user_data = &saves;
	options.screenshot_data = &screenshots;
	options.screenshot_directory = "captures";
	mh_session_race_options(&options, NULL, &race);
	CHECK(race.wait_for_loading && race.user_data == &screenshots);
	CHECK(race.screenshot_directory == options.screenshot_directory);
	CHECK(!race.headless && !race.results && race.root == NULL);
	CHECK(race.session == NULL && race.music == NULL &&
		race.completed == NULL);
	options.screenshot_data = NULL;
	mh_session_race_options(&options, NULL, &race);
	CHECK(race.user_data == NULL);

	return 0;
}
