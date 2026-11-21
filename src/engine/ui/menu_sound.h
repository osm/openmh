#ifndef MH_UI_MENU_SOUND_H
#define MH_UI_MENU_SOUND_H

#include "engine/core/error.h"
#include "engine/ui/menu.h"

#define MH_MENU_SOUND_COURSE_LIMIT 4096
#define MH_MENU_SOUND_VOLUME_MAX 255

enum mh_menu_sound_row {
	MH_MENU_SOUND_EFFECTS = 0,
	MH_MENU_SOUND_MUSIC = 1,
	MH_MENU_SOUND_LOOP = 2,
	MH_MENU_SOUND_COURSE = 3,
	MH_MENU_SOUND_MODE = 4 /* Native extension after the course list. */
};

struct mh_menu_sound_state {
	unsigned int	selected, course, list_changed;
};

struct mh_menu_sound_values {
	unsigned int	effects, music, loop; /* Volumes 0..255, CDLoop 0/1. */
	unsigned long	menu_track;
};

struct mh_menu_sound_catalog {
	/* Mutable CDTrack words in menu course order. */
	unsigned long	*tracks;
	unsigned int	count;

	unsigned long
		last_track; /* Last physical CD track, from the disc scan. */
};

struct mh_menu_sound_input {
	unsigned int	up, down, left, right, enter, escape;
	unsigned int	held_left, held_right, shift, control, alt;
	unsigned long	elapsed_ms, current_track;
};

/*
 * SOUND and PAGE use the common menu event kinds; volume events carry raw
 * 0..255 values, CD_PLAY a physical track byte. SCAN/STOP carry zero.
 */
enum mh_menu_sound_event_kind {
	MH_MENU_EFFECTS_VOLUME = 5,
	MH_MENU_MUSIC_VOLUME,
	MH_MENU_CD_SCAN,
	MH_MENU_CD_STOP,
	MH_MENU_CD_PLAY
};

#define MH_MENU_SOUND_OPTIONS_EVENTS 10

struct mh_menu_sound_output {
	struct mh_menu_event	events[MH_MENU_SOUND_OPTIONS_EVENTS];
	unsigned int		count;
};

/*
 * Sound input, including frame scaling. Query values fire only when equal to 1.
 * Up/Down run before held Left, Left event, held Right, Right event, Enter,
 * Escape. Shift takes precedence over Control and Alt. The adapter applies all
 * output events in order, including volume changes before their feedback
 * sounds. A changed track is auditioned only on Enter. Counts 1..4096; all
 * unsigned long inputs and CDTrack words must fit u32. Invalid input preserves
 * state, values, catalog and output. No device APIs. Init retains selection;
 * drawing consumes list_changed separately.
 */
enum mh_error_code	mh_menu_sound_update(struct mh_menu_sound_state *state,
	struct mh_menu_sound_values *values,
	const struct mh_menu_sound_catalog *catalog,
	const struct mh_menu_sound_input *input,
	struct mh_menu_sound_output *output, struct mh_error *error_out);

/*
 * Native menu adds the saved race-audio choice after the last course.
 * Up from Effects wraps to it; Down wraps back to Effects.
 * Left cycles backward; Right/Enter cycle forward through native profiles.
 */
enum mh_error_code	mh_menu_sound_update_race_audio(
	struct mh_menu_sound_state *state, struct mh_menu_sound_values *values,
	const struct mh_menu_sound_catalog *catalog,
	const struct mh_menu_sound_input *input, unsigned int *race_audio,
	struct mh_menu_sound_output *output, struct mh_error *error_out);

/*
 * Restore the menu song on departure, if audible and different. Like
 * preview/unmute, compare full track words, then play the low byte.
 */
enum mh_error_code	mh_menu_sound_leave(
	const struct mh_menu_sound_values *values, unsigned long current_track,
	struct mh_menu_sound_output *output, struct mh_error *error_out);

#endif
