#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/simulation/frame_time.h"

/* Preserve the original binary64, binary32 and integer literal widths. */
#define CLOCK_SECONDS_LIMIT 4294967.296
#define MEDIA_DELAY_MS 6000UL
#define GO_DELAY_MS 12000UL
#define CUE_STEP_MS 2000UL
#define CUE_COUNT 4
#define FRAME_REQUEST_LIMIT 1e9
#define FRAME_FALLBACK_SECONDS 1e-5f
#define FRAME_STEP_SECONDS .04
#define FRAME_STEP_SECONDS_F .04f

enum mh_error_code
mh_frame_menu_policy(unsigned int mode, int menu_before, int menu_after,
	long double elapsed, struct mh_frame_request *output,
	struct mh_error *error_out)
{
	if (output == NULL || mode > MH_GAME_DEMO ||
		(menu_before != 0 && menu_before != 1) ||
		(menu_after != 0 && menu_after != 1) ||
		!(elapsed >= 0 && elapsed <= FRAME_REQUEST_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu frame policy");

	output->elapsed = mode == MH_GAME_NETWORK || !menu_before ? elapsed : 0;
	output->action = mode == MH_GAME_NETWORK || !menu_after ? MH_FRAME_RUN
								: MH_FRAME_MENU;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_frame_media_time(double seconds, unsigned long intro_ms, int *ready,
	unsigned long *clock_ms, struct mh_error *error_out)
{
	long double now_ms;

	if (ready == NULL || clock_ms == NULL ||
		!(seconds >= 0 && seconds < CLOCK_SECONDS_LIMIT) ||
		intro_ms > MH_U32_MAX - MEDIA_DELAY_MS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording clock sample");

	now_ms = (long double)seconds * 1000.0L;

	if (now_ms >= MH_U32_MODULUS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"recording clock exceeds original range");

	*ready = intro_ms != 0 &&
		now_ms > (long double)(intro_ms + MEDIA_DELAY_MS);
	*clock_ms = (unsigned long)now_ms;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_frame_schedule(float elapsed, struct mh_frame_schedule *output,
	struct mh_error *error_out)
{
	struct mh_frame_schedule next;
	float remaining;
	unsigned int slice;

	if (output == NULL || !(elapsed >= 0 && elapsed <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid physical frame interval");

	memset(&next, 0, sizeof(next));
	remaining = elapsed;

	while (remaining > 0 && next.count < MH_FRAME_MAX_STEPS) {
		slice = next.count++;

		/*
		 * Compare with binary64 .04; store binary32 .04. Subtract
		 * binary32 .04 even on the final short slice.
		 */
		next.car[slice] = remaining > FRAME_STEP_SECONDS
			? FRAME_STEP_SECONDS_F
			: remaining;
		next.scenery[slice] = slice == 0 ? elapsed : 0;
		remaining = (float)(remaining - FRAME_STEP_SECONDS_F);
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_frame_time(double seconds, float previous_ms, unsigned long intro_ms,
	unsigned long go_ms, struct mh_frame_time *output,
	struct mh_error *error_out)
{
	struct mh_frame_time next;
	long double now_ms, elapsed;
	unsigned long cue_start_ms, cue_end_ms;
	unsigned int cue_index;

	if (output == NULL ||
		!(seconds >= 0 && seconds < CLOCK_SECONDS_LIMIT) ||
		!(previous_ms >= 0 && previous_ms <= (double)MH_U32_MODULUS) ||
		intro_ms > MH_U32_MAX -
				(MEDIA_DELAY_MS + CUE_COUNT * CUE_STEP_MS) ||
		go_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid simulation clock sample");

	/*
	 * Retain the product in extended precision. Store binary32 but leave
	 * that product unrounded for the subtraction. The .001 multiplier is
	 * binary64, not a long-double literal.
	 */
	now_ms = (long double)seconds * 1000.0L;

	if (now_ms >= MH_U32_MODULUS ||
		(go_ms != 0 && now_ms < (long double)go_ms))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"simulation clock precedes GO or exceeds original range");

	memset(&next, 0, sizeof(next));
	next.now_ms = (float)now_ms;
	elapsed = (now_ms - (long double)previous_ms) * (long double).001;
	next.elapsed = elapsed > 0 ? (float)elapsed : FRAME_FALLBACK_SECONDS;

	if (next.elapsed > 1)
		next.elapsed = 1;

	next.go_ms = go_ms;

	if (intro_ms != 0 && go_ms == 0 &&
		now_ms > (long double)(intro_ms + GO_DELAY_MS))
		next.go_ms = (unsigned long)now_ms;

	if (next.go_ms != 0)
		next.race_ms =
			(unsigned long)(now_ms - (long double)next.go_ms);

	next.cue = -1;

	if (intro_ms != 0)
		for (cue_index = 0; cue_index < CUE_COUNT; cue_index++) {
			cue_start_ms = intro_ms + MEDIA_DELAY_MS +
				CUE_STEP_MS * cue_index;

			cue_end_ms = cue_start_ms + CUE_STEP_MS;

			if (now_ms > (long double)cue_start_ms &&
				now_ms < (long double)cue_end_ms)
				next.cue = (CUE_COUNT - 1) - (int)cue_index;
		}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
