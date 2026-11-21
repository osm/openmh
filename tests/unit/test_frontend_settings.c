#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/settings/menu_return.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "frontend settings:%d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_settings loaded, snapshot, result, expected, saved_result;
	unsigned int mode;

	CHECK(mh_settings_init(&loaded, "Saved", 255, 127, 0, NULL) ==
		MH_ERROR_NONE);
	loaded.record_race = 1;
	loaded.av.detail_mode = MH_DETAIL_CUSTOM;
	loaded.av.smoke = 0;
	snapshot = loaded;
	snapshot.record_race = 0;
	snapshot.tcp_rate = 37;
	snapshot.prediction.minimum = 8;
	snapshot.prediction.maximum = 2;
	snapshot.av.effects_volume = 31;
	snapshot.av.music_volume = 79;
	snapshot.av.car_shading = MH_AV_SHADING_GLENZ;
	snapshot.av.perspective = 16;
	snapshot.av.view_distance = 99.125f;
	snapshot.av.smoke = 1;
	snapshot.av.car_view = MH_CAR_VIEW_OUTSIDE;
	expected = loaded;
	expected.av.car_view = MH_CAR_VIEW_OUTSIDE;
	expected.av.effects_volume = 31;
	expected.av.music_volume = 79;
	expected.av.car_shading = MH_AV_SHADING_GLENZ;
	expected.av.perspective = 16;
	expected.av.view_distance = 99.125f;

	for (mode = 0; mode < 7; mode++) {
		result = loaded;
		expected.tcp_rate = mode == 4 ? 37 : loaded.tcp_rate;
		expected.prediction =
			mode == 4 ? snapshot.prediction : loaded.prediction;
		CHECK(mh_settings_restore_menu(&result, &snapshot, mode,
			&result, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&expected, &result, sizeof(result)) == 0);
	}

	result = snapshot;
	CHECK(mh_settings_restore_menu(&loaded, &result, 5, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&expected, &result, sizeof(result)) == 0);
	CHECK(mh_settings_restore_menu(&loaded, NULL, 0, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &result, sizeof(result)) == 0);
	saved_result = result;
	snapshot.av.view_distance = 151;
	CHECK(mh_settings_restore_menu(&loaded, &snapshot, 0, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved_result, &result, sizeof(result)) == 0);
	snapshot.av.view_distance = 80;
	snapshot.team_name[4] = 'x';
	memset(snapshot.team_name, 'x', sizeof(snapshot.team_name));
	CHECK(mh_settings_restore_menu(&loaded, &snapshot, 4, &result, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved_result, &result, sizeof(result)) == 0);
	CHECK(mh_settings_restore_menu(&loaded, &snapshot, 2, &result, NULL) ==
		MH_ERROR_NONE);
	puts("frontend settings: selective return, recording choice, aliases and atomic failures passed");

	return 0;
}
