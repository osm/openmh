#include <stdio.h>
#include <string.h>

#include "client/application/race_frontend_internal.h"
#include "client/settings/render_choice.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "race settings:%d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static struct mh_application app;
	static struct mh_race_frontend frontend;
	struct mh_car car;
	struct mh_track track;
	struct mh_control_profile control;
	struct mh_race_request request;
	struct mh_settings output, saved;
	unsigned int style, detail;

	memset(&car, 0, sizeof(car));
	memset(&track, 0, sizeof(track));
	memset(&control, 0, sizeof(control));
	strcpy(car.name, "Fixture car");
	strcpy(track.name, "Fixture track");
	strcpy(control.layout.name, "Keyboard");
	app.preferences = &app.defaults;
	app.race_frontend = &frontend;
	frontend.cars.entries = &car;
	frontend.cars.count = 1;
	frontend.tracks.entries = &track;
	frontend.tracks.count = 1;
	frontend.controls.profiles = &control;
	frontend.controls.count = 1;
	frontend.car_available[0] = frontend.track_available[0] = 1;
	frontend.horns.count = 1;
	strcpy(frontend.horns.horns[0].name, "Horn");
	CHECK(mh_settings_init(app.preferences, "Driver", 100, 100,
		mh_render_choice_default(), NULL) == MH_ERROR_NONE);
	app.preferences->av.race_audio = mh_race_audio_default();

	for (style = MH_AV_NAMES_NONE; style <= MH_AV_NAMES_TRANSPARENT;
		style++) {
		app.preferences->av.name_plates = style;
		mh_race_frontend_defaults(&app, &request);
		CHECK(mh_race_request_get(&request, "show_player_names") ==
			(style != MH_AV_NAMES_NONE));
		CHECK(mh_race_frontend_configure(
			&app, &request, &output, NULL) == MH_ERROR_NONE);
		CHECK(output.av.name_plates == style);
	}

	for (detail = MH_DETAIL_LOW; detail <= MH_DETAIL_CUSTOM; detail++) {
		app.preferences->av.detail_mode = MH_DETAIL_HIGH;
		app.preferences->av.name_plates = MH_AV_NAMES_TRANSPARENT;
		mh_race_frontend_defaults(&app, &request);
		request.detail = detail;
		CHECK(mh_race_request_set(&request, "show_player_names", 0,
			NULL) == MH_ERROR_NONE);
		saved = *app.preferences;
		CHECK(mh_race_frontend_configure(
			&app, &request, &output, NULL) == MH_ERROR_NONE);
		CHECK(output.av.name_plates == MH_AV_NAMES_NONE);
		CHECK(memcmp(app.preferences, &saved, sizeof(saved)) == 0);

		/*
		 * A later launch reads visibility from the persisted settings.
		 */
		*app.preferences = output;
		mh_race_frontend_defaults(&app, &request);
		CHECK(!request.show_player_names);
		CHECK(mh_race_request_set(&request, "show_player_names", 1,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_race_frontend_configure(
			&app, &request, &output, NULL) == MH_ERROR_NONE);
		CHECK(output.av.name_plates != MH_AV_NAMES_NONE);
	}

	CHECK(mh_race_request_set(&request, "show_player_names", 2, NULL) ==
		MH_ERROR_RANGE);
	CHECK(request.show_player_names == 1);
	saved = output;
	request.show_player_names = 2;
	CHECK(mh_race_frontend_configure(&app, &request, &output, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
	puts("race frontend settings passed");

	return 0;
}
