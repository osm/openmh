#include <stdio.h>
#include <string.h>

#include "engine/ui/menu_graphics_fps.h"

static unsigned long
number(const char *text)
{
	unsigned long value;

	value = 0;

	while (*text != 0)
		value = value * 10 + (unsigned int)(*text++ - '0');

	return value;
}

int
mh_menu_graphics_fps_valid(const struct mh_menu_graphics_state *state)
{
	unsigned int i;

	if (state->fps_editing == 0)
		return 1;

	if (state->fps_editing != 1 ||
		state->selected != MH_MENU_GRAPHICS_MAX_FPS)
		return 0;

	for (i = 0; i < sizeof(state->fps_text); i++) {
		if (state->fps_text[i] == 0)
			return number(state->fps_text) <= MH_AV_MAX_FPS;

		if (state->fps_text[i] < '0' || state->fps_text[i] > '9')
			return 0;
	}

	return 0;
}

static void
finish_edit(
	struct mh_menu_graphics_state *state, struct mh_av_settings *settings)
{
	if (state->fps_text[0] != 0)
		settings->max_fps = number(state->fps_text);

	state->fps_editing = 0;
}

static void
begin_edit(struct mh_menu_graphics_state *state,
	const struct mh_av_settings *settings, int replace)
{
	if (state->fps_editing)
		return;

	if (replace)
		state->fps_text[0] = 0;
	else
		sprintf(state->fps_text, "%hu",
			(unsigned short)settings->max_fps);
	state->fps_editing = 1;
}

static unsigned int
edit_key(struct mh_menu_graphics_state *state, unsigned int key,
	unsigned char character)
{
	size_t length;
	unsigned long value;

	length = strlen(state->fps_text);

	if (key == MH_KEY_DELETE) {
		state->fps_text[0] = 0;
		return MH_MENU_SOUND_TEXT_ACCEPT;
	}

	if (key == MH_KEY_BACKSPACE) {
		if (length == 0)
			return MH_MENU_SOUND_TEXT_REJECT;

		state->fps_text[length - 1] = 0;

		return MH_MENU_SOUND_TEXT_ACCEPT;
	}

	if (character < '0' || character > '9')
		return MH_MENU_SOUND_TEXT_REJECT;

	value = number(state->fps_text) * 10 + character - '0';

	if (length + 1 >= sizeof(state->fps_text) || value > MH_AV_MAX_FPS)
		return MH_MENU_SOUND_TEXT_REJECT;

	state->fps_text[length] = (char)character;
	state->fps_text[length + 1] = 0;

	return MH_MENU_SOUND_TEXT_ACCEPT;
}

static void
sound(struct mh_menu_output *output, unsigned int value)
{
	output->events[output->count].kind = MH_MENU_SOUND;
	output->events[output->count++].value = value;
}

int
mh_menu_graphics_fps_update(struct mh_menu_graphics_state *state,
	struct mh_av_settings *settings,
	const struct mh_menu_graphics_input *input,
	struct mh_menu_output *output)
{
	unsigned int key, feedback;
	unsigned char character;

	if (state->selected != MH_MENU_GRAPHICS_MAX_FPS)
		return 0;

	if (state->fps_editing && input->escape == 1) {
		state->fps_editing = 0;
		sound(output, MH_MENU_SOUND_CANCEL);
		return 1;
	}

	if (input->escape == 1)
		return 0;

	if (input->up == 1 || input->down == 1) {
		if (state->fps_editing)
			finish_edit(state, settings);

		return 0;
	}

	if (input->keys[MH_KEY_ENTER] == 1) {
		if (state->fps_editing)
			finish_edit(state, settings);
		else
			begin_edit(state, settings, 0);
		sound(output, MH_MENU_SOUND_TEXT_ACCEPT);
		return 1;
	}

	/*
	 * At most one sound per poll, even when several keys arrive together.
	 */
	feedback = 0;

	for (key = 0; key < MH_KEY_COUNT; key++) {
		character = input->characters[key];

		if (input->keys[key] != 1 ||
			(character < ' ' && key != MH_KEY_BACKSPACE &&
				key != MH_KEY_DELETE))
			continue;

		if (character >= '0' && character <= '9')
			begin_edit(state, settings, 1);
		else if (key == MH_KEY_BACKSPACE || key == MH_KEY_DELETE)
			begin_edit(state, settings, 0);
		else {
			feedback = MH_MENU_SOUND_TEXT_REJECT;
			continue;
		}

		feedback = edit_key(state, key, character);
	}

	if (feedback != 0)
		sound(output, feedback);

	return 1;
}
