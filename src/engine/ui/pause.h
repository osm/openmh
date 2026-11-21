#ifndef MH_PAUSE_H
#define MH_PAUSE_H

#include "engine/core/error.h"
#define MH_PAUSE_EVENTS 24
#define MH_PAUSE_PAGES 3
#define MH_PAUSE_MAIN_ROWS 5
#define MH_PAUSE_TEXT_BYTES 64
#define MH_PAUSE_GRAPHICS_ROWS 4
#define MH_PAUSE_AUDIO_ROWS 3
#define MH_PAUSE_VOLUME_MAX 254
#define MH_PAUSE_DISTANCE_MIN 30
#define MH_PAUSE_DISTANCE_MAX 150

enum mh_pause_page {
	MH_PAUSE_MAIN = 0,
	MH_PAUSE_GRAPHICS = 1,
	MH_PAUSE_AUDIO = 2
};

enum mh_pause_action {
	MH_PAUSE_CONTINUE = 0,
	MH_PAUSE_RESTART_RACE = 1,
	MH_PAUSE_GFX_OPTIONS = 2,
	MH_PAUSE_SFX_OPTIONS = 3,
	MH_PAUSE_QUIT = 4
};

enum mh_pause_key {
	MH_PAUSE_TOGGLE = 1,
	MH_PAUSE_UP = 2,
	MH_PAUSE_DOWN = 4,
	MH_PAUSE_LEFT = 8,
	MH_PAUSE_RIGHT = 16,
	MH_PAUSE_ENTER = 32,
	MH_PAUSE_ESCAPE = 64 /* Raw Escape, used separately by demo mode 5. */
};

enum mh_pause_event_kind {
	MH_PAUSE_PLAY,
	MH_PAUSE_STOP,
	MH_PAUSE_DEVICE_STOP,
	MH_PAUSE_INPUT_MODE,
	MH_PAUSE_REFLECTION,
	MH_PAUSE_FLAT,
	MH_PAUSE_PERSPECTIVE,
	MH_PAUSE_DISTANCE,
	MH_PAUSE_EFFECTS_VOLUME,
	MH_PAUSE_CD_VOLUME,
	MH_PAUSE_RESTART
};

enum mh_pause_sample {
	MH_PAUSE_LOOP,
	MH_PAUSE_CONFIRM,
	MH_PAUSE_CURSOR,
	MH_PAUSE_ERROR
};

struct mh_pause_state {
	unsigned int	page, selected[MH_PAUSE_PAGES];
	int		visible;
};

struct mh_pause_settings {
	/* 0 flat/1 Gouraud/2 reflection/3 Glenz; perspective 16/32. */
	unsigned int	shading, perspective;

	/* Percent, 30..150. */
	float		distance;

	/* Device 0..255; menu right clamp 254. */
	unsigned int	effects_volume, cd_volume;
};

struct mh_pause_input {
	unsigned int	keys, mode;

	/* Supplied external query/callback results. */
	int		cd_playing, restart_ok;
};

struct mh_pause_event {
	enum mh_pause_event_kind	kind;
	double				value;
};

struct mh_pause_output {
	struct mh_pause_event		events[MH_PAUSE_EVENTS];

	/*
	 * Original labels are formatted before navigation; only distance is
	 * refreshed again after horizontal input during the same call.
	 */
	struct mh_pause_settings	display;
	unsigned int			count;

	/* Dimming is selected after toggle, before navigation/Enter closes. */
	int				keep_running, demo_exit, dimmed;
};

struct mh_pause_line {
	char		text[MH_PAUSE_TEXT_BYTES];
	unsigned long	color;
};

struct mh_pause_layout {
	struct mh_pause_line	lines[MH_PAUSE_MAIN_ROWS];
	unsigned int		count;
};

/* Shared pause labels/colors; font, positions and drawing are separate. */
enum mh_error_code	mh_pause_layout(const struct mh_pause_state *state,
	const struct mh_pause_settings *display, unsigned int mode,
	int network_host, struct mh_pause_layout *output,
	struct mh_error *error_out);

/*
 * In-race three-page controller. Inputs are already the event-table/binding
 * query results, not held keys or a repeat scheduler. Events retain callback
 * order. Errors preserve every output. A frontend executes RESTART and handles
 * its result; restart_ok allows the callback's return to be modeled
 * independently of that backend. Renderer dim/restore callbacks, drawing and
 * persistence are separate.
 */
enum mh_error_code	mh_pause_init(struct mh_pause_state *state,
	struct mh_error *error_out);
enum mh_error_code	mh_pause_update(struct mh_pause_state *state,
	struct mh_pause_settings *settings, const struct mh_pause_input *input,
	struct mh_pause_output *output, struct mh_error *error_out);

/* Glenz can be reselected only when the race loaded its maps at startup. */
enum mh_error_code	mh_pause_update_glenz(struct mh_pause_state *state,
	struct mh_pause_settings *settings, const struct mh_pause_input *input,
	int glenz_available, struct mh_pause_output *output,
	struct mh_error *error_out);

struct mh_hud_assets;
struct mh_ui_list;

/*
 * Append the original hardware-font overlay, using output.display from the
 * controller. mode 4 distinguishes host Abort race from client Retire race.
 * Failure preserves the existing list prefix and count.
 */
int	mh_pause_draw(const struct mh_hud_assets *assets,
	const struct mh_pause_state *state,
	const struct mh_pause_settings *display, unsigned int mode,
	int network_host, struct mh_ui_list *list);

#endif
