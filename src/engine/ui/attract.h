#ifndef MH_ATTRACT_H
#define MH_ATTRACT_H

#include "engine/core/error.h"
#include "engine/settings/config.h"

#define MH_ATTRACT_NAME_BYTES 16

struct mh_attract_course {
	unsigned long	division;
	char		name[MH_ATTRACT_NAME_BYTES];
};

struct mh_attract_view {
	unsigned int	viewed;
	unsigned int	in_car, requested_in_car, chase, track, intro;
	unsigned long	phase; /* Nonzero remembers the in-car half. */
};

struct mh_attract_frame {
	struct mh_attract_view	view;

	/* Explicit listener release on the old viewed car. */
	unsigned int		release_mask;

	/* DEMO blink and strict >40 seconds after GO. */
	int			label, finished;
};

struct mh_field;
struct mh_drive_frame;
struct mh_hud_assets;
struct mh_ui_list;

/*
 * Catalog order is the original sorted TRK order, including locked courses.
 * Reject special remainder >=10, division quotient >3 and the previous name
 * (ASCII case folding, first 16 bytes). Retain rejected RNG draws; a bounded
 * no-candidate/exhaustion error changes no output.
 */
enum mh_error_code	mh_attract_course_select(
	const struct mh_attract_course *courses, unsigned int count,
	const char previous[MH_ATTRACT_NAME_BYTES], unsigned long *random,
	unsigned int *selected, struct mh_error *error_out);

/*
 * Prepare temporary attract settings for the selected track (a source-file
 * index). Keep the saved settings when returning to the menu.
 */
enum mh_error_code	mh_attract_settings(const struct mh_settings *saved,
	unsigned int track, struct mh_settings *runtime,
	struct mh_error *error_out);

/*
 * Choose the idle delay for the current menu page. An automatic return
 * from attract mode uses the shorter restart delay.
 */
unsigned long	mh_attract_idle_delay(int main_page, unsigned int previous_mode,
	unsigned long exit_flag);

/*
 * Strict full-clock difference against stored u32 start. stamp_ms is the second
 * getter sample, only consumed when due. Polling publishes a transition
 * request; the caller then runs the normal page request/leave sequence and sets
 * its new timeout. Errors are atomic.
 */
enum mh_error_code	mh_attract_idle_poll(unsigned long *started,
	unsigned long delay, long double now_ms, long double stamp_ms,
	int *requested, struct mh_error *error_out);

/*
 * Update the attract camera before the viewed-car and physics pass. go_ms==0
 * retains all camera fields. A 20-second cycle uses in-car first, then track
 * and advances to the next active entrant once at the boundary. Whole elapsed
 * milliseconds wrap before modulo; the exit comparison keeps full game-clock
 * precision.
 */
enum mh_error_code	mh_attract_frame(const struct mh_attract_view *state,
	unsigned int count, unsigned int active_mask, unsigned long go_ms,
	long double now_ms, struct mh_attract_frame *output,
	struct mh_error *error_out);

/*
 * Apply the decision before bound camera input. Preserve the shared camera
 * pose/lens across a view change; only explicit releases change sound owners.
 * Phase is cleared on fresh race entry, retained across restart.
 */
enum mh_error_code	mh_attract_field_frame(struct mh_field *field,
	const struct mh_drive_frame *sample, unsigned long *phase,
	struct mh_attract_frame *output, struct mh_error *error_out);

/*
 * Centered hardware font at y=4, black pass at +1,+1. The caller applies the
 * original menu/held-Info suppression.
 */
int	mh_attract_label_draw(const struct mh_hud_assets *assets, int visible,
	struct mh_ui_list *list);

#endif
