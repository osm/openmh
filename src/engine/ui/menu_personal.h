#ifndef MH_MENU_PERSONAL_H
#define MH_MENU_PERSONAL_H

#include "engine/core/error.h"
#include "engine/input/text_edit.h"
#include "engine/settings/config.h"
#include "engine/ui/menu.h"
#include "interfaces/font/glyphs.h"

#define MH_MENU_PERSONAL_COLORS 32
#define MH_MENU_PERSONAL_SWATCHES 4
#define MH_MENU_PERSONAL_SHORTCUTS 10
#define MH_MENU_PERSONAL_EVENTS 784

enum mh_menu_personal_row {
	MH_MENU_PERSONAL_NAME = 0,
	MH_MENU_PERSONAL_TEAM = 1,
	MH_MENU_PERSONAL_PLAYER_COLOR = 2,
	MH_MENU_PERSONAL_SYSTEM = 3, /* Patched layout inserts this row. */
	MH_MENU_PERSONAL_SHORTCUT_FIRST = 3,
	MH_MENU_PERSONAL_SHORTCUT_LAST = 12,
	MH_MENU_PERSONAL_CUSTOM_COLORS = 13,
	MH_MENU_PERSONAL_BODY_FIRST = 14,
	MH_MENU_PERSONAL_BODY_LAST = 16,
	MH_MENU_PERSONAL_ROWS = 17
};

struct mh_menu_personal_state {
	unsigned int	selected;
	int		patched;
};

struct mh_menu_personal_input {
	unsigned long	clock_ms;
	unsigned char	keys[MH_KEY_COUNT], characters[MH_KEY_COUNT];
};

struct mh_menu_personal_swatch {
	unsigned long	color; /* Displayed BEFORE this frame's color edit. */
	unsigned int	active, nearest;
};

struct mh_menu_personal_output {
	struct mh_menu_event	events[MH_MENU_PERSONAL_EVENTS];
	unsigned int		count, swatch_count;
	int			patched;

	struct mh_menu_personal_swatch
		swatches[MH_MENU_PERSONAL_SWATCHES]; /* Player, body 1/2/3. */
	unsigned int		cursor, cursor_row;

	unsigned long
		cursor_width; /* Post-edit width, even when blink is off. */
};

/* Map a baseline row to its position with the patched System row inserted. */
unsigned int	mh_menu_personal_row(unsigned int row, int patched);

/*
 * Personal settings with color picker, nearest-color selection and byte
 * editor. Rows: name/team/player color, ten shortcuts, custom colors, three
 * body colors. Default.ACT supplies choices 255 down to 224. Up/Down,
 * Left/Right, color widgets, text editing, Enter, Escape occur in that order.
 * Only key event 1 acts. Both exit keys request Options and keep edits. The
 * draw callback precedes input; swatches/cursor overlay it after update.
 * Names/team/shortcuts use limits 16/5/128 including NUL, pixel widths
 * 144/144/440. No SDL, palette or rendering side effects here. Validate all
 * text, RGB choices and metrics; errors preserve every output.
 */
enum mh_error_code	mh_menu_personal_update(
	struct mh_menu_personal_state *state, struct mh_settings *settings,
	const unsigned long colors[MH_MENU_PERSONAL_COLORS],
	const struct mh_menu_personal_input *input,
	const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_menu_personal_output *output, struct mh_error *error_out);

#endif
