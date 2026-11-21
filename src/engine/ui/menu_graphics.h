#ifndef MH_MENU_GRAPHICS_H
#define MH_MENU_GRAPHICS_H

#include "engine/core/error.h"
#include "engine/input/keyboard.h"
#include "engine/settings/av.h"
#include "engine/ui/menu.h"

#define MH_MENU_GRAPHICS_DEVICES 64
#define MH_MENU_GRAPHICS_MODES 4096
#define MH_MENU_GRAPHICS_FPS_BYTES 6

enum mh_menu_graphics_choice {
	MH_MENU_GRAPHICS_DEVICE = 0,
	MH_MENU_GRAPHICS_MODE,
	MH_MENU_GRAPHICS_BRIGHTNESS,
	MH_MENU_GRAPHICS_INFO_DETAIL,
	MH_MENU_GRAPHICS_ROAD_MAP,
	MH_MENU_GRAPHICS_CHECKPOINT_INFO,
	MH_MENU_GRAPHICS_CHECKPOINT_DELAY,
	MH_MENU_GRAPHICS_DETAIL,
	MH_MENU_GRAPHICS_LENS_FLARES,
	MH_MENU_GRAPHICS_SPARKS,
	MH_MENU_GRAPHICS_SMOKE,
	MH_MENU_GRAPHICS_HALOS,
	MH_MENU_GRAPHICS_SHADOW,
	MH_MENU_GRAPHICS_SKID_MARKS,
	MH_MENU_GRAPHICS_NAME_PLATES,
	MH_MENU_GRAPHICS_SKY,
	MH_MENU_GRAPHICS_TRACK_DETAIL,
	MH_MENU_GRAPHICS_CAR_DETAIL,
	MH_MENU_GRAPHICS_CAR_SHADING,
	MH_MENU_GRAPHICS_PERSPECTIVE,
	MH_MENU_GRAPHICS_PERSPECTIVE_QUALITY,
	MH_MENU_GRAPHICS_VIEW_DISTANCE,
	MH_MENU_GRAPHICS_MOTION_BLUR,
	MH_MENU_GRAPHICS_TEXTURE_PRECISION,
	MH_MENU_GRAPHICS_TEXTURE_FILTER,
	MH_MENU_GRAPHICS_FRAME_QUEUE,
	MH_MENU_GRAPHICS_TRIPLE_BUFFER,
	MH_MENU_GRAPHICS_FULLSCREEN,
	MH_MENU_GRAPHICS_VSYNC,
	MH_MENU_GRAPHICS_MAX_FPS,
	MH_MENU_GRAPHICS_SCREENSHOT_FORMAT,
	MH_MENU_GRAPHICS_SHOW_FPS,
	MH_MENU_GRAPHICS_ROWS
};

struct mh_menu_graphics_mode {
	unsigned long	width, height;
};

struct mh_menu_graphics_device {
	const char				*name;
	const struct mh_menu_graphics_mode	*modes;

	/* Zero modes retains an unavailable stable ID; navigation skips it. */
	unsigned int				count;

	/* Can select Glenz before loading a race's maps. */
	int					glenz;
	int					texture_filter;
	int					texture_precision;
	int					frame_queue;
	int					blur_history;
	int					motion_blur;
};

struct mh_menu_graphics_catalog {
	/* Window capability, independent of the selected scene renderer. */
	int					fullscreen;

	/* Native race presentation settings, independent of detail presets. */
	int					presentation;
	int					screenshots;
	const struct mh_menu_graphics_device	*devices;
	unsigned int				count;
};

struct mh_menu_graphics_state {
	unsigned int	selected, device, mode;

	/* Draft text stays separate from the saved FPS limit until accepted. */
	unsigned int	fps_editing;
	char		fps_text[MH_MENU_GRAPHICS_FPS_BYTES];
};

struct mh_menu_graphics_input {
	unsigned int	escape, up, down, held_right, held_left, right, left;
	unsigned int	shift, control, alt;
	unsigned long	elapsed_ms;
	unsigned char	keys[MH_KEY_COUNT], characters[MH_KEY_COUNT];
};

/* Shared bounded state/catalog validation for input and page composition. */
enum mh_error_code	mh_menu_graphics_validate(
	const struct mh_menu_graphics_state *state,
	const struct mh_av_settings *settings,
	const struct mh_menu_graphics_catalog *catalog,
	struct mh_error *error_out);

/*
 * Graphics settings with mode remapping. Events fire only for 1, in the order
 * declared above. The native FPS field accepts typed digits, Backspace/Delete,
 * Enter to apply and Escape to cancel. Moving Up/Down applies a nonempty draft;
 * an empty draft keeps the previous value. Typing on an inactive field replaces
 * the previous value.
 *
 * Device switches preserve dimensions using the original signed 32-bit product
 * score, even at a device boundary. Counts: 1..64 devices, 1..4096 modes each.
 * Names are unused by input. Dimensions/times fit u32; finite brightness may be
 * negative, as allowed by CFG. Invalid input changes nothing.
 *
 * Does not publish dimensions into settings or apply a detail preset. At race
 * launch from the menu, dimensions publish before CFG saving; the preset
 * applies afterward. Those operations are separate from page input.
 */
enum mh_error_code	mh_menu_graphics_update(
	struct mh_menu_graphics_state *state, struct mh_av_settings *settings,
	const struct mh_menu_graphics_catalog *catalog,
	const struct mh_menu_graphics_input *input,
	struct mh_menu_output *output, struct mh_error *error_out);

/*
 * Keep row IDs stable; insert native presentation options after Mode. Call with
 * a validated catalog and row/position within its visible range.
 */
unsigned int	mh_menu_graphics_row_at(
	const struct mh_menu_graphics_catalog *catalog, unsigned int position);
unsigned int	mh_menu_graphics_row_position(
	const struct mh_menu_graphics_catalog *catalog, unsigned int row);

/*
 * Availability within a validated device catalog; ordinary rows stay visible.
 */
int	mh_menu_graphics_row_available(
	const struct mh_menu_graphics_device *device, unsigned int row);

#endif
