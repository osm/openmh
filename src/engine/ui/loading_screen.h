#ifndef MH_LOADING_SCREEN_H
#define MH_LOADING_SCREEN_H

#include "engine/core/error.h"
#include "engine/render/profile.h"
#include "interfaces/catalog/track.h"

#define MH_LOADING_ROWS 5

struct mh_loading_row {
	const char	*label;
	char		value[MH_TRACK_TEXT_SIZE];
	int		label_x, value_x, y;
};

struct mh_loading_layout {
	struct mh_loading_row	rows[MH_LOADING_ROWS];
};

struct mh_loading_progress {
	unsigned int	remaining, marked;
};

/*
 * Six milestones, descending rows, then stay at row zero. A zero timer sample
 * does not paint the row. Marked bits retain pixels already painted on the
 * original surface; elapsed time selects no animation.
 */
void			mh_loading_progress_init(
	struct mh_loading_progress *progress);
enum mh_error_code	mh_loading_progress_step(
	struct mh_loading_progress *progress, unsigned long milliseconds,
	struct mh_error *error_out);

/*
 * Original PC 3.1 loading captions, at the requested logical resolution.
 * System is 0 for Metric, 1 for Imperial. Name is the selected catalog name;
 * location and lengths come from the TRK. Record times are milliseconds.
 * The output owns its values and borrows only static labels. Failure leaves
 * it unchanged. Artwork, fonts, presentation and loading progress are separate.
 */
enum mh_error_code	mh_loading_layout(const struct mh_track *track,
	const char *name, unsigned int system, unsigned long lap_ms,
	unsigned long race_ms, unsigned int width, unsigned int height,
	enum mh_render_profile profile, struct mh_loading_layout *output,
	struct mh_error *error_out);

#endif
