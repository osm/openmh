#ifndef MH_MENU_BACKGROUND_H
#define MH_MENU_BACKGROUND_H

#include "engine/core/error.h"
#include "engine/ui/menu_lines.h"

#define MH_MENU_BACKGROUND_LINES 2048
#define MH_MENU_BACKGROUND_OBJECTS 10
#define MH_MENU_BACKGROUND_MODELS 9

struct mh_menu_background_object {
	float		speed, position[3];
	unsigned long	model, angle[3], timer;
};

struct mh_menu_background {
	unsigned int				count;
	unsigned long				lightning_frames;
	long					lightning[4];
	struct mh_menu_background_object	objects[MH_MENU_BACKGROUND_OBJECTS];
};

struct mh_menu_background_model {
	unsigned int	model, brightness;
	float		position[3];
	unsigned long	angle[3];
};

struct mh_menu_background_draw {
	struct mh_menu_line		lines[MH_MENU_BACKGROUND_LINES];
	unsigned int			line_count, model_count;
	struct mh_menu_background_model	models[MH_MENU_BACKGROUND_OBJECTS];
};

/*
 * Re-entering the menus resets only Z and timer, preserving other object
 * fields. Zero-initialize the state before its first use. Count <=10; the
 * original configured default is three, selected from nine LOB assets.
 */
enum mh_error_code	mh_menu_background_reset(
	struct mh_menu_background *state, unsigned int count,
	struct mh_error *error_out);

/*
 * Consume four values of the caller's shared RNG and request a 3..10-frame
 * horizontal/vertical bolt. Separate from its menu-sound trigger.
 */
enum mh_error_code	mh_menu_background_flash(
	struct mh_menu_background *state, unsigned long *random,
	struct mh_error *error_out);

/*
 * Gears, three lightning traversals, then floating model requests. Layouts 2/3
 * return without advancing anything. Frame time and clock are unsigned 32-bit
 * milliseconds. State, RNG and output are unchanged on errors. The caller draws
 * lines before models, then page artwork. This does not load the wireframe
 * models or schedule the sound-driven flash.
 */
enum mh_error_code	mh_menu_background_step(
	struct mh_menu_background *state, unsigned int layout,
	unsigned long clock_ms, unsigned long elapsed_ms, unsigned long *random,
	struct mh_menu_background_draw *output, struct mh_error *error_out);

#endif
