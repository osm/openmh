#include <stdio.h>
#include <string.h>

#include "engine/game/mode.h"
#include "engine/settings/av.h"
#include "engine/ui/pause.h"

#define VALUE_TEXT_BYTES 16
#define SELECTED_COLOR 0xffffffUL
#define NORMAL_COLOR 0xe06820UL
#define DISABLED_SELECTED_COLOR 0UL
#define DISABLED_COLOR 0x402200UL

enum mh_error_code
mh_pause_layout(const struct mh_pause_state *state,
	const struct mh_pause_settings *display, unsigned int mode,
	int network_host, struct mh_pause_layout *output,
	struct mh_error *error_out)
{
	static const char *const labels[MH_PAUSE_PAGES][MH_PAUSE_MAIN_ROWS] = {
		{"Continue race", "Restart race", "GFX Options", "SFX Options",
			"Quit to menu"},
		{"Car shading:", "Perspective quality:", "View distance:",
			"Back", ""},
		{"SFX Volume:", "CD Volume:", "Back", "", ""}};

	static const char *const shading[MH_AV_SHADING_GLENZ + 1] = {
		"Flat", "Gouraud", "Reflection", "Glenz"};

	static const unsigned int rows[MH_PAUSE_PAGES] = {MH_PAUSE_MAIN_ROWS,
		MH_PAUSE_GRAPHICS_ROWS, MH_PAUSE_AUDIO_ROWS};

	struct mh_pause_layout next;
	const char *label;
	char text[MH_PAUSE_TEXT_BYTES], value[VALUE_TEXT_BYTES];
	size_t label_length, value_length;
	unsigned int i, count;
	unsigned long rgb;

	if (state == NULL || display == NULL || output == NULL ||
		state->page >= MH_PAUSE_PAGES ||
		state->selected[MH_PAUSE_MAIN] >= MH_PAUSE_MAIN_ROWS ||
		state->selected[MH_PAUSE_GRAPHICS] >= MH_PAUSE_GRAPHICS_ROWS ||
		state->selected[MH_PAUSE_AUDIO] >= MH_PAUSE_AUDIO_ROWS ||
		(state->visible != 0 && state->visible != 1) ||
		mode > MH_GAME_DEMO ||
		(network_host != 0 && network_host != 1) ||
		display->shading > MH_AV_SHADING_GLENZ ||
		(display->perspective != MH_AV_PERSPECTIVE_GOOD &&
			display->perspective != MH_AV_PERSPECTIVE_NORMAL) ||
		!(display->distance >= MH_PAUSE_DISTANCE_MIN &&
			display->distance <= MH_PAUSE_DISTANCE_MAX) ||
		display->effects_volume > MH_AV_VOLUME_MAX ||
		display->cd_volume > MH_AV_VOLUME_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid pause text context");

	memset(&next, 0, sizeof(next));
	count = state->visible ? rows[state->page] : 0;
	next.count = count;

	for (i = 0; i < count; i++) {
		label = labels[state->page][i];
		value[0] = 0;

		if (state->page == MH_PAUSE_MAIN && i == MH_PAUSE_QUIT) {
			if (mode == MH_GAME_LEAGUE ||
				(mode == MH_GAME_NETWORK && !network_host))
				label = "Retire race";
			else if (mode == MH_GAME_NETWORK)
				label = "Abort race";
		} else if (state->page == MH_PAUSE_GRAPHICS) {
			if (i == 0)
				strcpy(value, shading[display->shading]);
			else if (i == 1) {
				if (display->perspective ==
					MH_AV_PERSPECTIVE_NORMAL)
					strcpy(value, "Normal");
				else
					strcpy(value, "Better");
			} else if (i == 2)
				sprintf(value, "%u%%",
					(unsigned int)display->distance);
		} else if (state->page == MH_PAUSE_AUDIO && i < 2)
			sprintf(value, "%u%%",
				(i == 0 ? display->effects_volume
					: display->cd_volume) *
					100 / MH_PAUSE_VOLUME_MAX);

		/*
		 * The original "%s %s" always includes a space, even for empty
		 * values.
		 */
		label_length = strlen(label);
		value_length = strlen(value);

		if (label_length + value_length + 2 > sizeof(text))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"pause text exceeds line capacity");

		memcpy(text, label, label_length);
		text[label_length] = ' ';
		memcpy(text + label_length + 1, value, value_length + 1);
		rgb = i == state->selected[state->page] ? SELECTED_COLOR
							: NORMAL_COLOR;

		if (state->page == MH_PAUSE_MAIN &&
			i == MH_PAUSE_RESTART_RACE &&
			(mode == MH_GAME_LEAGUE || mode == MH_GAME_NETWORK))
			rgb = state->selected[MH_PAUSE_MAIN] ==
					MH_PAUSE_RESTART_RACE
				? DISABLED_SELECTED_COLOR
				: DISABLED_COLOR;

		strcpy(next.lines[i].text, text);
		next.lines[i].color = rgb;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
