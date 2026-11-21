#ifndef MH_AUDIO_CD_H
#define MH_AUDIO_CD_H

#include "engine/core/error.h"
#define MH_CD_VOLUME_MAX 255

enum mh_cd_boundary {
	MH_CD_MENU,
	MH_CD_RACE,
	MH_CD_RESULTS,
	MH_CD_RESTORE_MENU
};

/*
 * Ordered original device requests: scan disc, stop, set volume, play.
 * A play request itself stops/reopens original MCI playback. The port
 * consumes extracted WAV PCM.
 */
struct mh_cd_request {
	int		scan, stop, volume, play;
	unsigned int	track;
};

enum mh_error_code	mh_cd_select(enum mh_cd_boundary boundary,
	unsigned int volume, unsigned int mode, unsigned long selected_track,
	unsigned long current_track, struct mh_cd_request *output,
	struct mh_error *error_out);
int			mh_cd_repeat(unsigned int volume, unsigned int loop);

/*
 * Race-frame polling, independent of PCM completion. The unsigned
 * start+duration sum wraps before comparing against the full real-time
 * millisecond value. Equality does not request a restart.
 */
enum mh_error_code	mh_cd_repeat_select(unsigned int volume,
	unsigned int loop, unsigned long current_track,
	unsigned long started_ms, unsigned long duration_ms,
	long double milliseconds, struct mh_cd_request *output,
	struct mh_error *error_out);

#endif
