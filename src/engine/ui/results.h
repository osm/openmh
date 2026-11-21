#ifndef MH_RESULTS_H
#define MH_RESULTS_H

#include "engine/core/error.h"
#include "engine/game/standings.h"
#include "engine/simulation/field.h"
#include "engine/ui/draw.h"
#include "interfaces/catalog/profile.h"
#include "interfaces/font/glyphs.h"

#define MH_RESULTS_MAX_PLAYERS MH_RACE_MAX_CARS

struct mh_results_assets {
	/*
	 * Regions borrow the shared HUD/results atlas. Profiles use entry
	 * order.
	 */
	struct mh_ui_region background, portraits[MH_RESULTS_MAX_PLAYERS],
		glyphs[MH_FONT_GLYPHS];
	unsigned int		advance[MH_FONT_GLYPHS], count;
	struct mh_profile	profiles[MH_RESULTS_MAX_PLAYERS];

	/* Native network departure annotation by entry. */
	unsigned int		left_mask;
};

struct mh_results_row {
	char	score[32], race[32], best[32];
};

struct mh_results_screen {
	int			visible, finish_seen, prepared, aborted;

	/* Immutable prepared board, physical flags retained. */
	struct mh_standings	final;

	/* Last timer observation, not a committed world clock. */
	long double		finish_clock_ms;
	unsigned long		finish_anchor_ms;
};

/* European 3.1 ordinary board gate; other exits still prepare/save results. */
int	mh_results_board_required(unsigned int mode, int aborted);

/*
 * Prepare once after the local deadline; consumes the supplied shared RNG
 * only on success. Reopening preserves rows and random state.
 */
enum mh_error_code	mh_results_screen_prepare(
	struct mh_results_screen *screen, const struct mh_field *field,
	unsigned long *random, struct mh_error *error_out);

/*
 * Pause-menu Quit prepares immediately, with the human last and opponents
 * randomly ranked before generating patched synthetic times. Atomic and
 * idempotent. The application decides whether to display or save directly.
 */
enum mh_error_code	mh_results_screen_abort(
	struct mh_results_screen *screen, const struct mh_field *field,
	unsigned long *random, struct mh_error *error_out);
void			mh_results_row(const struct mh_race_result *result,
	struct mh_results_row *row);

/*
 * Immediate board policy retained for network sessions: open once at the
 * controlled finish; preview pauses
 * a still-racing controlled car, but actual finishers may wait for the AI.
 */
void	mh_results_screen_update(struct mh_results_screen *screen,
	const struct mh_standings *standings);

/*
 * Non-network finish scheduler: original now-25000 anchor and strict
 * now > u32(anchor+35000), using the extended simulation-clock product.
 * Only an active controlled finisher starts the timer. Failure is atomic.
 */
enum mh_error_code	mh_results_screen_update_local(
	struct mh_results_screen *screen, const struct mh_standings *standings,
	int local_active, long double now_ms, struct mh_error *error_out);

/*
 * Observe prior-frame finishes using THIS frame's sampled clock, before
 * physics. A newly expired deadline commits only clocks to the field: the
 * original exits here. Otherwise leave the field for its ordinary advance.
 * Both outputs are unchanged on failure. Caller supplies menu-scaled time.
 */
enum mh_error_code	mh_results_screen_frame(
	struct mh_results_screen *screen, struct mh_field *field,
	long double elapsed, struct mh_error *error_out);
int			mh_results_screen_pauses(
	const struct mh_results_screen *screen,
	const struct mh_standings *standings);

/*
 * Original 640x400 composition, uniformly scaled and centered. Optional native
 * footer (NULL hides it). Reads standings; never changes ranking or clocks.
 */
int	mh_results_draw(const struct mh_results_assets *assets,
	const struct mh_ui_atlas *atlas, const struct mh_standings *standings,
	unsigned int width, unsigned int height, const char *footer,
	struct mh_ui_list *list);

/*
 * Season board. Assets/profiles/points are already in sorted division order (2
 * or 8 rows). Interpret cumulative u32 points as signed for display. No
 * ranking, awards or rollover side effects.
 */
int	mh_results_season_draw(const struct mh_results_assets *assets,
	const struct mh_ui_atlas *atlas, const unsigned long *points,
	unsigned int width, unsigned int height, const char *footer,
	struct mh_ui_list *list);

#endif
