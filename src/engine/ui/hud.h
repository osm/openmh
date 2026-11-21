#ifndef MH_HUD_H
#define MH_HUD_H

#include "engine/core/error.h"
#include "engine/settings/av.h"
#include "engine/simulation/field.h"
#include "engine/ui/draw.h"
#include "engine/ui/messages.h"
#include "engine/ui/race_info.h"
#include "interfaces/catalog/profile.h"
#include "interfaces/font/glyphs.h"

#define MH_HUD_MIN_WIDTH 320
#define MH_HUD_MIN_HEIGHT 200
#define MH_HUD_VIEW_LIMIT 16384
#define MH_HUD_FONT_LIMIT 256
#define MH_HUD_MAP_RANKS 8
/* The track map uses 3.14159 for a half-turn. */
#define MH_HUD_MAP_RADIANS_PER_DEGREE 0.01745327777777778
#define MH_HUD_SECRET_SCROLL_BYTES 8192

enum mh_hud_sprite {
	MH_HUD_SMALL = 0,
	MH_HUD_BIG = 10,
	MH_HUD_KMH = 20,
	MH_HUD_GEAR,
	MH_HUD_REVERSE,
	MH_HUD_METER,
	MH_HUD_COLON,
	MH_HUD_SLASH,
	MH_HUD_TOTAL,
	MH_HUD_TIME,
	MH_HUD_LAP,
	MH_HUD_BEST,
	MH_HUD_POS,
	MH_HUD_THREE,
	MH_HUD_TWO,
	MH_HUD_ONE,
	MH_HUD_GO,
	MH_HUD_WRONG_WAY,
	MH_HUD_MARKER,
	MH_HUD_OTHER_MARKER,
	MH_HUD_AVENGER,
	MH_HUD_MAP_NUMBER,
	MH_HUD_RACE = MH_HUD_MAP_NUMBER + MH_HUD_MAP_RANKS,
	MH_HUD_LAST,
	MH_HUD_EVER,
	MH_HUD_CAMERA,
	MH_HUD_GHOST,
	MH_HUD_MPH,
	MH_HUD_SPRITES
};

enum mh_hud_status {
	MH_HUD_STATUS_GHOST = 1,
	MH_HUD_STATUS_RECORDING = 2,
	MH_HUD_STATUS_DEMO = 4
};

struct mh_hud_assets {
	struct mh_ui_atlas	atlas;
	struct mh_ui_region	sprites[MH_HUD_SPRITES], glyphs[MH_FONT_GLYPHS];
	unsigned int		advance[MH_FONT_GLYPHS];

	/* Original FONT2 byte-mode wrap metric. */
	unsigned int		message_advance[MH_FONT_GLYPHS];
	struct mh_ui_region	map;
	float			map_info[4], map_offset[2];
	struct mh_ui_region	hardware_glyphs[MH_FONT_GLYPHS];

	/* White texel for untextured overlays. */
	struct mh_ui_region	solid;
	unsigned int hardware_advance[MH_FONT_GLYPHS], hardware_cell_width,
		hardware_height;
	char			secret_scroll[MH_HUD_SECRET_SCROLL_BYTES];
	unsigned int		secret_scroll_length;
};

enum mh_hud_secret_notice {
	MH_HUD_SECRET_NONE = 0,
	MH_HUD_SECRET_CLUB = 1,
	MH_HUD_SECRET_SCROLLER = 2
};

struct mh_hud_secrets {
	unsigned int	visits, notice;

	/* Process latch selecting the alternate Credits page. */
	int		club_credits;
	int		scroll_started, scroll_x;
	unsigned int	scroll_offset, phase_y, phase_x, bounce_phase;
};

struct mh_hud_marker {
	float		world[3];
	unsigned int	rank;

	/* Player color: 0xRRGGBB. */
	unsigned long	color;
	int		active, viewed, special;
};

struct mh_hud_gap {
	char		name[64];
	unsigned long	color;
	unsigned int	position;
	long		milliseconds;
};

struct mh_hud_state {
	/* Held action, independent of InfoDetail and race clocks. */
	int				show_info;
	struct mh_race_info		info;

	/* 0 none, 1 speed/current lap, 2 all gauges. */
	unsigned int			info_detail;
	unsigned int			speed, gear, lap, laps, position, cars;
	unsigned int			system;
	unsigned long			total_ms, lap_ms, best_ms;
	unsigned long			last_ms, split_ms;

	/* 0 ordinary, 1 best/finish, 2 record. */
	unsigned int			lap_notice, race_notice;

	/* Time-mode/duration gates; detail/config checked at draw. */
	int				time_notices;

	/* Configured mode, before finish forces gap_mode=2. */
	unsigned int			checkpoint_info;
	unsigned int			gap_mode, gap_count;

	/* Time attack reserves three rows in either visible mode. */
	int				record_gaps;
	struct mh_hud_gap		gaps[MH_RACE_MAX_CARS];

	/* [0,1] bright meter fraction. */
	double				revs;
	enum mh_start_phase		start;
	int				paused, finished, complete, timed;
	int				wrong_way;

	/* Low word of truncated game time, for 200ms blink. */
	unsigned long			clock_ms;

	/* Elapsed clock, independent of viewed car. */
	unsigned long			status_flags;

	/* Map mode: 0 off, 1 adjacent ranks, 2 all. */
	unsigned int			map_mode, marker_count;
	struct mh_hud_marker		markers[MH_RACE_MAX_CARS];

	/* Previous map call's notice and animation. */
	struct mh_hud_secrets		secrets;

	/* Optional borrowed native session status. */
	const char			*message;

	/* Borrowed original notification history. */
	const struct mh_messages	*messages;

	/* Notification callback's sampled game clock. */
	double				message_ms;

	/* NULL inactive; borrowed editable history head. */
	const char			*chat_text;
	unsigned long			chat_ms;
};

/* Centered hardware-font text with a one-pixel black shadow. */
int	mh_hud_label(const struct mh_hud_assets *assets, const char *text,
	float y, struct mh_ui_list *list);

/* Multiply binary32 speed by the selected binary32 factor, then truncate. */
unsigned int	mh_hud_display_speed(float speed, unsigned int system);

/*
 * Read-only snapshot. The primary car owns the frame clock even while
 * viewing another entrant. Invalid clock/field/output preserves the output.
 */
enum mh_error_code	mh_hud_state(const struct mh_field *field, int paused,
	struct mh_hud_state *state, struct mh_error *error_out);

/*
 * Replace the three status bits, preserving other packet bits. Playback is
 * active from initialization until EOF; recording is active from opening the
 * take, before the first frame passes the >6s gate.
 */
unsigned long	mh_hud_status_flags(unsigned long flags, unsigned int mode,
	int playback_active, int recording_active);

/*
 * Fixed bottom-right pixels, visible on even 200ms intervals at InfoDetail 1/2.
 * Checkpoint, map, race-clock and pause gates do not apply.
 */
int	mh_hud_status_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list);
int	mh_hud_messages_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list);

/*
 * Uses the viewed car's held crossing, active roster and original profile
 * nicknames; a time-attack field instead selects the record rows and ignores
 * profiles. gap_mode defaults to 2; set 0/1/2 before this call for
 * off/compact/full.
 */
enum mh_error_code	mh_hud_gap_state(const struct mh_field *field,
	const struct mh_profile *profiles, struct mh_hud_state *state,
	struct mh_error *error_out);

/*
 * Time-attack record gaps: personal lap, global lap, global race. Uses the
 * viewed car and shared record book. suppressed is the caller's drawing gate;
 * checkpoint audio has its separate latch/gates. Read-only and atomic on error,
 * including invalid record payloads.
 */
enum mh_error_code	mh_hud_record_gap_state(const struct mh_field *field,
	int suppressed, struct mh_hud_state *state, struct mh_error *error_out);
int			mh_hud_gaps_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list);

/*
 * Snapshot rendered body translations and roster identities without modifying
 * simulation. NULL profiles use the default player color.
 */
void	mh_hud_map_state(const struct mh_field *field, const float *models,
	const struct mh_profile *profiles, struct mh_hud_state *state);

/*
 * Map projection. Omit nonfinite/singular/extreme offscreen inputs rather than
 * converting an undefined float to integer.
 */
int	mh_hud_map_project(const float info[4], const float offset[2],
	const float world[3], int origin_x, int origin_y, int pixel[2]);
int	mh_hud_map_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list);

/*
 * Original hardware-map location tests. Disabled map or unlisted viewed car
 * retains the previous notice/counter. XYZ is the rendered body translation.
 */
void	mh_hud_secret_update(struct mh_hud_secrets *secrets, const char *track,
	unsigned int map_mode, int active, const float world[3]);

/*
 * Snapshot this frame's notice, then advance animation and next map state.
 * Call once per presented HUD frame, including paused frames.
 */
void	mh_hud_secrets_frame(struct mh_hud_secrets *secrets, const char *track,
	struct mh_hud_state *hud, unsigned int width,
	unsigned int scroll_length);
int	mh_hud_secrets_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_secrets *secrets, struct mh_ui_list *list);

/*
 * With the normal-resolution 10px top margin. Appends checkpoint/lap/finish
 * notices to an initialized list.
 */
int	mh_hud_notices_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list);

/*
 * Hardware HUD layout and timers. Viewports >=320x200. Fixed hardware pixels
 * anchored to edges, with clipping.
 */
int	mh_hud_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, unsigned int width,
	unsigned int height, struct mh_ui_list *list);

/*
 * Compose after a scene overlay such as the TRON introduction. The existing
 * list must have matching dimensions; failure restores its published count.
 */
int	mh_hud_append(const struct mh_hud_assets *assets,
	const struct mh_hud_state *state, unsigned int width,
	unsigned int height, struct mh_ui_list *list);

#endif
