#include <stddef.h>
#include <string.h>

#include "engine/audio/cd.h"
#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"

#define CD_RESULTS_TRACK 2
#define CD_TRACK_MASK 0xff

enum mh_error_code
mh_cd_select(enum mh_cd_boundary boundary, unsigned int volume,
	unsigned int mode, unsigned long selected_track,
	unsigned long current_track, struct mh_cd_request *output,
	struct mh_error *error_out)
{
	struct mh_cd_request next;

	if (output == NULL || boundary < MH_CD_MENU ||
		boundary > MH_CD_RESTORE_MENU || volume > MH_CD_VOLUME_MAX ||
		mode > MH_GAME_DEMO || selected_track > MH_U32_MAX ||
		current_track > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid CD selection");

	memset(&next, 0, sizeof(next));

	if (volume != 0) {
		if (boundary == MH_CD_MENU) {
			/* Fresh menu entry, including race return. */
			next.scan = next.stop = next.volume = next.play = 1;
		} else if (boundary == MH_CD_RACE) {
			/* Network mode skips this local selection. */
			if (mode != MH_GAME_NETWORK)
				next.stop = next.play = 1;
		} else if (boundary == MH_CD_RESULTS) {
			if (mode == MH_GAME_QUICK_RACE ||
				mode == MH_GAME_LEAGUE) {
				next.play = 1;
				selected_track = CD_RESULTS_TRACK;
			}
		} else if (current_track != selected_track) {
			/* Compare the full word, then play its low byte. */
			next.stop = next.play = 1;
		}

		if (next.play)
			next.track =
				(unsigned int)(selected_track & CD_TRACK_MASK);
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_cd_repeat(unsigned int volume, unsigned int loop)
{
	/* Menu has no corresponding repeat call. */
	return volume != 0 && loop != 0;
}

enum mh_error_code
mh_cd_repeat_select(unsigned int volume, unsigned int loop,
	unsigned long current_track, unsigned long started_ms,
	unsigned long duration_ms, long double milliseconds,
	struct mh_cd_request *output, struct mh_error *error_out)
{
	struct mh_cd_request next;
	unsigned long deadline_ms;

	if (output == NULL || volume > MH_CD_VOLUME_MAX || loop > 1 ||
		current_track > MH_U32_MAX || started_ms > MH_U32_MAX ||
		duration_ms > MH_U32_MAX ||
		!(milliseconds >= 0 && milliseconds < MH_CLOCK_MS_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid CD repeat clock");

	memset(&next, 0, sizeof(next));
	deadline_ms = (started_ms + duration_ms) & MH_U32_MAX;

	if (mh_cd_repeat(volume, loop) && current_track != 0 &&
		milliseconds > (long double)deadline_ms) {
		next.play = 1;
		next.track = (unsigned int)(current_track & CD_TRACK_MASK);
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
