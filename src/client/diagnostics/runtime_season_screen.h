#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_SEASON_SCREEN_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_SEASON_SCREEN_H

#include "client/session/season_screen.h"

/*
 * Modal season presentation. Returns success; accepted distinguishes Enter
 * from Escape/window close. Capture renders one offscreen frame when non-NULL.
 * Optional settings select ScreenDimension; NULL uses the inspection default.
 */
int	mh_season_present(const struct mh_results_assets *results,
	const struct mh_ui_atlas *atlas, const unsigned long *points,
	const char *footer, const char *capture_path,
	const struct mh_av_settings *settings, int *accepted,
	const struct mh_debug_options *debug);

#endif
