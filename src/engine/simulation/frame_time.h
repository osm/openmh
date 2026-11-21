#ifndef MH_FRAME_TIME_H
#define MH_FRAME_TIME_H

#include "engine/core/clock.h"
#include "engine/core/error.h"

#define MH_FRAME_MAX_STEPS 10

/*
 * A native focus/inspection suspension stops the whole frame. An original
 * visible-menu frame still samples clocks, playback, recording and final
 * presentation/progress; only physical integration is omitted.
 */
enum mh_frame_action {
	MH_FRAME_RUN,
	MH_FRAME_SUSPEND,
	MH_FRAME_MENU
};

struct mh_frame_request {
	long double		elapsed;
	enum mh_frame_action	action;
};

/*
 * Use the default whole-frame scenery interval. Input is the physical interval,
 * already halved for underwater racing. No remainder carries to the next frame.
 * Zero produces no world calls.
 */
struct mh_frame_schedule {
	unsigned int	count;
	float		car[MH_FRAME_MAX_STEPS], scenery[MH_FRAME_MAX_STEPS];
};

/*
 * Separate products of the absolute simulation clock in seconds. now_ms is the
 * binary32 value retained for the NEXT frame's subtraction; race_ms and the
 * strict countdown comparisons use the unrounded clock. cue is -1 outside an
 * open cue interval, otherwise 3, 2, 1 or 0 (GO). Menu/mode visibility, sound
 * latches and finished-race freezing belong to their callers. Zero intro/go
 * timestamps retain the sentinels.
 */
struct mh_frame_time {
	float		now_ms, elapsed;
	unsigned long	go_ms, race_ms;
	int		cue;
};

/*
 * Sample using the entry menu flag; gate physics using the post-controller
 * flag. Mode 4 bypasses both menu gates.
 */
enum mh_error_code	mh_frame_menu_policy(unsigned int mode, int menu_before,
	int menu_after, long double elapsed, struct mh_frame_request *output,
	struct mh_error *error_out);

enum mh_error_code	mh_frame_schedule(float elapsed,
	struct mh_frame_schedule *output, struct mh_error *error_out);

/*
 * Samples, but does not advance, the clock. The provider owns pause/time
 * accumulation; nonpositive deltas become 1e-5 and elapsed is capped at 1.
 * Extremely small positive deltas can underflow at the binary32 store.
 * Native validation
 * rejects timestamps outside the original unsigned 32-bit range and intro
 * epochs whose cue additions would wrap. Failure leaves output untouched.
 */
enum mh_error_code	mh_frame_time(double seconds, float previous_ms,
	unsigned long intro_ms, unsigned long go_ms,
	struct mh_frame_time *output, struct mh_error *error_out);

/*
 * Playback/recording gate after physics. Compare the unrounded absolute clock
 * with intro+6000 BEFORE truncating the supplied timestamp. Both outputs are
 * preserved on failure; a zero intro never opens the gate.
 */
enum mh_error_code	mh_frame_media_time(double seconds,
	unsigned long intro_ms, int *ready, unsigned long *clock_ms,
	struct mh_error *error_out);

#endif
