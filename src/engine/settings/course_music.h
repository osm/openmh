#ifndef MH_SETTINGS_COURSE_MUSIC_H
#define MH_SETTINGS_COURSE_MUSIC_H

#include "engine/settings/config.h"
#include "interfaces/catalog/track.h"

/*
 * Bind installed Division identities and TRK defaults, retaining existing
 * choices. The installed order replaces the previous catalog; errors preserve
 * settings. Inputs are borrowed only for the call.
 */
enum mh_error_code	mh_settings_music_bind(struct mh_settings *settings,
	const struct mh_track *tracks, unsigned int count,
	struct mh_error *error_out);

/*
 * Use the first matching Division choice, otherwise the authored CDTrack.
 * Track is required; settings may be NULL to select the authored default.
 */
unsigned int	mh_settings_track_music(const struct mh_settings *settings,
	const struct mh_track *track);

#endif
