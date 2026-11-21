#ifndef MH_MENU_SPRITE_H
#define MH_MENU_SPRITE_H

#include "engine/core/error.h"
#include "engine/ui/menu_canvas.h"
#include "interfaces/sprite/pixels.h"
#include "interfaces/ui/layout.h"

enum mh_menu_sprite_kind {
	MH_MENU_SPRITE_PLAIN,
	MH_MENU_SPRITE_JITTER,
	MH_MENU_SPRITE_PHASE,
	MH_MENU_SPRITE_EXPAND,
	MH_MENU_SPRITE_SPREAD,
	MH_MENU_SPRITE_CENTER
};

struct mh_menu_sprite_state {
	long		timer[MH_MENU_LAYOUT_SLOTS];
	unsigned char	wobble[MH_MENU_LAYOUT_SLOTS];
};

struct mh_menu_sprite_command {
	unsigned int	kind, slot, frame;
	long		x, y, parameter, offset;
	float		factor;
};

struct mh_menu_sprite_output {
	unsigned int			count;
	struct mh_menu_sprite_command	commands[2];
};

struct mh_menu_sprite_row {
	long		offset;
	unsigned int	source;
};

/*
 * Return the selected label's animation frame (0..7) for the main and setup
 * pages. The menu clock is in unsigned 32-bit milliseconds.
 */
unsigned int	mh_menu_sprite_label_frame(unsigned long clock_ms);

/*
 * Call in layout order, including slots with absent sprites, because effects
 * share the caller's random state. Returns up to two draw requests.
 * Timers are signed 32-bit. Invalid inputs leave state and output unchanged.
 */
enum mh_error_code	mh_menu_sprite_select(
	const struct mh_menu_layout *layout, unsigned int page,
	unsigned int slot, unsigned int mix, long dx, long dy,
	unsigned int frame, struct mh_menu_sprite_state *state,
	unsigned long *random, struct mh_menu_sprite_output *output,
	struct mh_error *error_out);

/*
 * Build scanlines before clipping, with offsets relative to the sprite
 * origin. Call only for present sprites so absent ones do not advance the
 * jitter phase. Height must be 2..4096. The output buffer must fit the
 * transformed rows. Outputs are unchanged on error.
 */
enum mh_error_code	mh_menu_sprite_rows(
	const struct mh_menu_sprite_command *command, unsigned int height,
	unsigned int pitch, unsigned long *noise,
	struct mh_menu_sprite_row *rows, unsigned int capacity,
	unsigned int *count, struct mh_error *error_out);

/*
 * Apply decoded index/coverage rows to an indexed canvas. Plain sprites use
 * original vertical clipping; transformed sprites skip row origins below
 * begin and stop at the first origin >=end. Individual writes also stay inside
 * the allocation, preserving the original padding without unsafe writes.
 * Rows must come from rows() for this command/image. Never executes resources.
 * Invalid inputs are rejected before changing pixels.
 */
enum mh_error_code	mh_menu_sprite_blit(
	const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_command *command,
	const struct mh_menu_sprite_row *rows, unsigned int count,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

/* Same clipping, with muted menu-text color and stippled sprite coverage. */
enum mh_error_code	mh_menu_sprite_blit_disabled(
	const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_command *command,
	const struct mh_menu_sprite_row *rows, unsigned int count,
	struct mh_menu_canvas *canvas, struct mh_error *error_out);

/*
 * Execute commands in order using caller-owned row scratch. Disabled draws
 * retain the same row generation and noise history. Stop at the first error;
 * previously completed commands remain drawn, as with individual blits.
 */
enum mh_error_code	mh_menu_sprite_draw_commands(
	const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_output *commands,
	struct mh_menu_sprite_row *rows, unsigned int capacity,
	unsigned long *noise, struct mh_menu_canvas *canvas, int disabled,
	struct mh_error *error_out);

#endif
