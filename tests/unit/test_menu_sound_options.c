#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/audio/race_profile.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_sound.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Sound options line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
invalid_inputs(void)
{
	struct mh_menu_sound_state state, saved_state;
	struct mh_menu_sound_values values, saved_values;
	struct mh_menu_sound_catalog catalog, saved_catalog;
	struct mh_menu_sound_input input;
	struct mh_menu_sound_output output, saved_output;
	unsigned long tracks[2], saved_tracks[2];
	unsigned int failure;
	enum mh_error_code status;

	for (failure = 0; failure < 17; failure++) {
		memset(&state, 0, sizeof(state));
		memset(&values, 0, sizeof(values));
		memset(&catalog, 0, sizeof(catalog));
		memset(&input, 0, sizeof(input));
		memset(&output, 0x5a, sizeof(output));
		tracks[0] = 4;
		tracks[1] = 11;
		catalog.count = 2;
		catalog.tracks = tracks;
		catalog.last_track = 11;
		input.held_right = input.down = input.right = input.enter =
			input.escape = 1;

		if (failure == 0)
			state.selected = 4;

		if (failure == 1)
			state.course = 2;

		if (failure == 2)
			state.list_changed = 2;

		if (failure == 3)
			values.effects = 256;

		if (failure == 4)
			values.music = 256;

		if (failure == 5)
			values.loop = 2;

		if (failure == 6)
			catalog.count = 0;

		if (failure == 7)
			catalog.count = 4097;

		if (failure == 8)
			catalog.tracks = NULL;

		if (failure == 9) {
			if (ULONG_MAX == MH_U32_MAX)
				continue;

			tracks[1] = ULONG_MAX;
		}

		if (failure == 10) {
			if (ULONG_MAX == MH_U32_MAX)
				continue;

			values.menu_track = ULONG_MAX;
		}

		if (failure == 11) {
			if (ULONG_MAX == MH_U32_MAX)
				continue;

			catalog.last_track = ULONG_MAX;
		}

		if (failure == 12) {
			if (ULONG_MAX == MH_U32_MAX)
				continue;

			input.elapsed_ms = ULONG_MAX;
		}

		if (failure == 13) {
			if (ULONG_MAX == MH_U32_MAX)
				continue;

			input.current_track = ULONG_MAX;
		}

		saved_state = state;
		saved_values = values;
		saved_catalog = catalog;
		saved_output = output;
		memcpy(saved_tracks, tracks, sizeof(tracks));
		status = mh_menu_sound_update(failure == 14 ? NULL : &state,
			&values, &catalog, failure == 15 ? NULL : &input,
			failure == 16 ? NULL : &output, NULL);
		CHECK(status != MH_ERROR_NONE &&
			memcmp(&state, &saved_state, sizeof(state)) == 0 &&
			memcmp(&values, &saved_values, sizeof(values)) == 0 &&
			memcmp(&catalog, &saved_catalog, sizeof(catalog)) ==
				0 &&
			memcmp(tracks, saved_tracks, sizeof(tracks)) == 0 &&
			memcmp(&output, &saved_output, sizeof(output)) == 0);
	}

	CHECK(mh_menu_sound_update(&state, NULL, &catalog, &input, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_menu_sound_update(&state, &values, NULL, &input, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_menu_sound_leave(NULL, 0, &output, NULL) != MH_ERROR_NONE);
	CHECK(mh_menu_sound_leave(&values, 0, NULL, NULL) != MH_ERROR_NONE);
	values.music = 256;
	saved_output = output;
	CHECK(mh_menu_sound_leave(&values, 0, &output, NULL) != MH_ERROR_NONE &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);

	return 0;
}

static int
race_audio_choice(void)
{
	struct mh_menu_sound_state state;
	struct mh_menu_sound_values values;
	struct mh_menu_sound_catalog catalog;
	struct mh_menu_sound_input input;
	struct mh_menu_sound_output output;
	unsigned long tracks[2];
	unsigned int race_audio;

	memset(&state, 0, sizeof(state));
	memset(&values, 0, sizeof(values));
	memset(&input, 0, sizeof(input));
	tracks[0] = 3;
	tracks[1] = 4;
	catalog.tracks = tracks;
	catalog.count = 2;
	catalog.last_track = 11;
	state.selected = MH_MENU_SOUND_COURSE;
	race_audio = 0;
	input.down = 1;
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_SOUND_COURSE && state.course == 1);
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_SOUND_MODE);
	input.down = 0;
	input.right = 1;
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(race_audio == MH_RACE_AUDIO_DS3D && output.count == 1);
	input.right = 0;
	input.enter = 1;
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(race_audio == MH_RACE_AUDIO_EAX);
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(race_audio == MH_RACE_AUDIO_A3D12);
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(race_audio == MH_RACE_AUDIO_A3D20);
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(race_audio == MH_RACE_AUDIO_SOFTWARE &&
		output.events[0].value == MH_MENU_SOUND_TOGGLE_OFF);
	input.enter = 0;
	input.left = input.escape = 1;
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(race_audio == MH_RACE_AUDIO_A3D20 && output.count == 2 &&
		output.events[1].kind == MH_MENU_PAGE);
	input.left = input.escape = 0;
	input.up = 1;
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_SOUND_COURSE && state.course == 1);
	CHECK(tracks[0] == 3 && tracks[1] == 4 && values.effects == 0);
	state.selected = MH_MENU_SOUND_EFFECTS;
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_SOUND_MODE && state.course == 1);
	input.up = 0;
	input.down = 1;
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_NONE);
	CHECK(state.selected == MH_MENU_SOUND_EFFECTS);
	state.selected = MH_MENU_SOUND_COURSE;
	race_audio = MH_RACE_AUDIO_A3D20 + 1;
	CHECK(mh_menu_sound_update_race_audio(&state, &values, &catalog, &input,
		&race_audio, &output, NULL) == MH_ERROR_INVALID);
	CHECK(state.selected == MH_MENU_SOUND_COURSE &&
		race_audio == MH_RACE_AUDIO_A3D20 + 1);

	return 0;
}

int
main(void)
{
	struct mh_menu_sound_state state;
	struct mh_menu_sound_values values;
	struct mh_menu_sound_catalog catalog;
	struct mh_menu_sound_input input;
	struct mh_menu_sound_output output;
	unsigned long tracks[2];

	CHECK(invalid_inputs() == 0 && race_audio_choice() == 0);
	memset(&state, 0, sizeof(state));
	memset(&values, 0, sizeof(values));
	memset(&input, 0, sizeof(input));
	catalog.count = 2;
	catalog.tracks = tracks;
	catalog.last_track = 11;
	tracks[0] = 4;
	tracks[1] = 11;
	values.menu_track = 259;
	values.music = 1;
	state.selected = 1;
	input.elapsed_ms = 5;
	input.up = input.down = input.held_left = input.held_right =
		input.enter = input.escape = 1;
	CHECK(mh_menu_sound_update(&state, &values, &catalog, &input, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.count == MH_MENU_SOUND_OPTIONS_EVENTS &&
		values.music == 1 && state.selected == 1);
	CHECK(output.events[2].kind == MH_MENU_MUSIC_VOLUME &&
		output.events[2].value == 0);
	CHECK(output.events[4].kind == MH_MENU_CD_STOP &&
		output.events[5].kind == MH_MENU_CD_PLAY &&
		output.events[5].value == 3 &&
		output.events[6].kind == MH_MENU_MUSIC_VOLUME &&
		output.events[6].value == 1);
	CHECK(output.events[9].kind == MH_MENU_PAGE &&
		output.events[9].value == MH_MENU_OPTIONS);
	memset(&input, 0, sizeof(input));
	input.held_right = 1;
	values.music = 0;
	CHECK(mh_menu_sound_update(&state, &values, &catalog, &input, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.count == 4 && output.events[0].kind == MH_MENU_CD_STOP &&
		values.music == 0);
	state.selected = 3;
	values.music = 128;
	input.enter = input.right = 1;
	input.current_track = 5;
	CHECK(mh_menu_sound_update(&state, &values, &catalog, &input, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(tracks[0] == 5 && output.count == 1 &&
		output.events[0].value == 22);
	input.current_track = 4;
	input.right = 0;
	CHECK(mh_menu_sound_update(&state, &values, &catalog, &input, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.count == 3 && output.events[0].kind == MH_MENU_CD_SCAN &&
		output.events[2].value == 5);
	CHECK(mh_menu_sound_leave(&values, 3, &output, NULL) == MH_ERROR_NONE &&
		output.count == 2 && output.events[1].value == 3);
	CHECK(mh_menu_sound_leave(&values, 259, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 0);
	puts("Sound options ordered volume/unmute, course preview, departure and atomic bounds passed");

	return 0;
}
