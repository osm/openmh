#include <string.h>

#include "engine/audio/race_profile.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_slider.h"
#include "engine/ui/menu_sound.h"

#define VOLUME_STEP 3.0L
#define FIRST_AUDIO_TRACK 2

static int
valid_values(const struct mh_menu_sound_values *values)
{
	return values != NULL && values->effects <= MH_MENU_SOUND_VOLUME_MAX &&
		values->music <= MH_MENU_SOUND_VOLUME_MAX &&
		values->loop <= 1 && values->menu_track <= MH_U32_MAX;
}

static void
append_event(struct mh_menu_sound_output *result, unsigned int kind,
	unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

static void
play_track(struct mh_menu_sound_output *result, unsigned long track)
{
	append_event(result, MH_MENU_CD_STOP, 0);
	append_event(result, MH_MENU_CD_PLAY, (unsigned int)(track & 255));
}

static void
decrease_volume(unsigned int selected, struct mh_menu_sound_values *values,
	long double step, struct mh_menu_sound_output *output)
{
	unsigned int volume, kind, sound;
	unsigned long amount;

	volume = selected == MH_MENU_SOUND_EFFECTS ? values->effects
						: values->music;
	kind = selected == MH_MENU_SOUND_EFFECTS ? MH_MENU_EFFECTS_VOLUME
						: MH_MENU_MUSIC_VOLUME;
	sound = MH_MENU_SOUND_SLIDER_LIMIT;

	if (volume != 0) {
		amount = (unsigned long)step;

		if (amount > volume)
			amount = volume;

		volume -= (unsigned int)amount;

		if (selected == MH_MENU_SOUND_EFFECTS)
			values->effects = volume;
		else
			values->music = volume;
		append_event(output, kind, volume);
		sound = MH_MENU_SOUND_SLIDER_DECREASE;
	}

	append_event(output, MH_MENU_SOUND, sound);
}

static void
increase_volume(unsigned int selected, struct mh_menu_sound_values *values,
	unsigned long current_track, long double step,
	struct mh_menu_sound_output *output)
{
	unsigned int volume, kind, sound;
	unsigned long amount;

	volume = selected == MH_MENU_SOUND_EFFECTS ? values->effects
						: values->music;
	kind = selected == MH_MENU_SOUND_EFFECTS ? MH_MENU_EFFECTS_VOLUME
						: MH_MENU_MUSIC_VOLUME;
	sound = MH_MENU_SOUND_SLIDER_LIMIT;

	if (volume < MH_MENU_SOUND_VOLUME_MAX) {
		if (selected == MH_MENU_SOUND_MUSIC && volume == 0 &&
			current_track != values->menu_track)
			play_track(output, values->menu_track);

		amount = (unsigned long)(step + (long double)volume);
		volume = amount > MH_MENU_SOUND_VOLUME_MAX
			? MH_MENU_SOUND_VOLUME_MAX
			: (unsigned int)amount;

		if (selected == MH_MENU_SOUND_EFFECTS)
			values->effects = volume;
		else
			values->music = volume;
		append_event(output, kind, volume);
		sound = MH_MENU_SOUND_SLIDER_INCREASE;
	}

	append_event(output, MH_MENU_SOUND, sound);
}

static enum mh_error_code
update_sound(struct mh_menu_sound_state *state,
	struct mh_menu_sound_values *values,
	const struct mh_menu_sound_catalog *catalog,
	const struct mh_menu_sound_input *input, unsigned int *race_audio,
	struct mh_menu_sound_output *output, struct mh_error *error_out)
{
	struct mh_menu_sound_state next;
	struct mh_menu_sound_values value;
	struct mh_menu_sound_output result;
	unsigned int i, sound;
	unsigned long track;
	long double step;

	if (!valid_values(values) || state == NULL || catalog == NULL ||
		input == NULL || output == NULL || catalog->tracks == NULL ||
		catalog->count == 0 ||
		catalog->count > MH_MENU_SOUND_COURSE_LIMIT ||
		catalog->last_track > MH_U32_MAX ||
		state->selected > (race_audio != NULL ? MH_MENU_SOUND_MODE
						: MH_MENU_SOUND_COURSE) ||
		(race_audio != NULL && *race_audio > MH_RACE_AUDIO_A3D20) ||
		state->course >= catalog->count || state->list_changed > 1 ||
		input->elapsed_ms > MH_U32_MAX ||
		input->current_track > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound options input");

	for (i = 0; i < catalog->count; i++)
		if (catalog->tracks[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid sound options CD track");

	next = *state;
	value = *values;
	memset(&result, 0, sizeof(result));
	step = (long double)mh_menu_slider_step(input->elapsed_ms, input->shift,
		input->control, input->alt) *
		VOLUME_STEP;

	if (input->up == 1) {
		sound = MH_MENU_SOUND_ROW_PREVIOUS;

		if (next.selected == MH_MENU_SOUND_COURSE && next.course != 0) {
			next.course--;
			next.list_changed = 1;
		} else if (next.selected != MH_MENU_SOUND_EFFECTS)
			next.selected--;
		else if (race_audio != NULL) {
			next.selected = MH_MENU_SOUND_MODE;
			next.course = catalog->count - 1;
			next.list_changed = 1;
		} else
			sound = MH_MENU_SOUND_ROW_LIMIT;

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->down == 1) {
		sound = MH_MENU_SOUND_ROW_NEXT;

		if (next.selected == MH_MENU_SOUND_MODE)
			next.selected = MH_MENU_SOUND_EFFECTS;
		else if (next.selected < MH_MENU_SOUND_COURSE)
			next.selected++;
		else if (next.selected == MH_MENU_SOUND_COURSE &&
			next.course < catalog->count - 1) {
			next.course++;
			next.list_changed = 1;
		} else if (next.selected == MH_MENU_SOUND_COURSE &&
			race_audio != NULL)
			next.selected = MH_MENU_SOUND_MODE;
		else
			sound = MH_MENU_SOUND_ROW_LIMIT;

		append_event(&result, MH_MENU_SOUND, sound);
	}

	track = catalog->tracks[next.course];

	if (input->held_left == 1 && next.selected < MH_MENU_SOUND_LOOP)
		decrease_volume(next.selected, &value, step, &result);

	if (input->left == 1) {
		if (next.selected == MH_MENU_SOUND_LOOP) {
			sound = MH_MENU_SOUND_TOGGLE_LIMIT;

			if (value.loop == 1) {
				value.loop = 0;
				sound = MH_MENU_SOUND_TOGGLE_OFF;
			}

			append_event(&result, MH_MENU_SOUND, sound);
		} else if (next.selected == MH_MENU_SOUND_COURSE) {
			sound = MH_MENU_SOUND_LAPS_PREVIOUS_LIMIT;

			if (track > FIRST_AUDIO_TRACK) {
				track--;
				sound = MH_MENU_SOUND_LAPS_PREVIOUS;
			}

			append_event(&result, MH_MENU_SOUND, sound);
		}
	}

	if (input->held_right == 1 && next.selected < MH_MENU_SOUND_LOOP)
		increase_volume(next.selected, &value, input->current_track,
			step, &result);

	if (input->right == 1) {
		if (next.selected == MH_MENU_SOUND_LOOP) {
			sound = MH_MENU_SOUND_TOGGLE_LIMIT;

			if (value.loop == 0) {
				value.loop = 1;
				sound = MH_MENU_SOUND_TOGGLE_ON;
			}

			append_event(&result, MH_MENU_SOUND, sound);
		} else if (next.selected == MH_MENU_SOUND_COURSE) {
			sound = MH_MENU_SOUND_LAPS_NEXT_LIMIT;

			if (track < catalog->last_track) {
				track++;
				sound = MH_MENU_SOUND_LAPS_NEXT;
			}

			append_event(&result, MH_MENU_SOUND, sound);
		}
	}

	if (next.selected == MH_MENU_SOUND_MODE) {
		if (input->left == 1 || input->right == 1 ||
			input->enter == 1) {
			*race_audio = mh_race_audio_next(
				*race_audio, input->left == 1 ? -1 : 1);

			append_event(&result, MH_MENU_SOUND,
				*race_audio ? MH_MENU_SOUND_TOGGLE_ON
					: MH_MENU_SOUND_TOGGLE_OFF);
		}
	}

	if (input->enter == 1 && next.selected != MH_MENU_SOUND_MODE) {
		if (next.selected == MH_MENU_SOUND_LOOP) {
			append_event(&result, MH_MENU_SOUND,
				value.loop == 0 ? MH_MENU_SOUND_TOGGLE_ON
						: MH_MENU_SOUND_TOGGLE_OFF);
			value.loop = 1 - value.loop;
		} else if (next.selected == MH_MENU_SOUND_COURSE) {
			if (value.music != 0 && input->current_track != track) {
				append_event(&result, MH_MENU_CD_SCAN, 0);
				play_track(&result, track);
			}
		} else
			append_event(&result, MH_MENU_SOUND,
				MH_MENU_SOUND_VALUE_LIMIT);
	}

	if (input->escape == 1)
		append_event(&result, MH_MENU_PAGE, MH_MENU_OPTIONS);

	catalog->tracks[next.course] = track;
	*state = next;
	*values = value;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_sound_update(struct mh_menu_sound_state *state,
	struct mh_menu_sound_values *values,
	const struct mh_menu_sound_catalog *catalog,
	const struct mh_menu_sound_input *input,
	struct mh_menu_sound_output *output, struct mh_error *error_out)
{
	return update_sound(
		state, values, catalog, input, NULL, output, error_out);
}

enum mh_error_code
mh_menu_sound_update_race_audio(struct mh_menu_sound_state *state,
	struct mh_menu_sound_values *values,
	const struct mh_menu_sound_catalog *catalog,
	const struct mh_menu_sound_input *input, unsigned int *race_audio,
	struct mh_menu_sound_output *output, struct mh_error *error_out)
{
	if (race_audio == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing race audio choice");

	return update_sound(
		state, values, catalog, input, race_audio, output, error_out);
}

enum mh_error_code
mh_menu_sound_leave(const struct mh_menu_sound_values *values,
	unsigned long current_track, struct mh_menu_sound_output *output,
	struct mh_error *error_out)
{
	struct mh_menu_sound_output result;

	if (!valid_values(values) || current_track > MH_U32_MAX ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound options departure");

	memset(&result, 0, sizeof(result));

	if (values->music != 0 && current_track != values->menu_track)
		play_track(&result, values->menu_track);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
