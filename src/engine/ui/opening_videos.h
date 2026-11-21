#ifndef MH_UI_OPENING_VIDEOS_H
#define MH_UI_OPENING_VIDEOS_H

/*
 * Return 1=end, 2=skip, 3=unavailable. The startup caller continues after
 * unavailable entries. Zero is fatal; CLOSED is the native window-close
 * outcome, kept separate from a keyboard skip.
 */
#define MH_OPENING_VIDEOS 3

#define MH_OPENING_FAILED 0
#define MH_OPENING_ENDED 1
#define MH_OPENING_SKIPPED 2
#define MH_OPENING_UNAVAILABLE 3
#define MH_OPENING_CLOSED 4
#define MH_OPENING_CONTINUE 5

/* Sequence policy for the selected clip count; CONTINUE requests the next. */
int	mh_opening_video_result(unsigned int index, unsigned int count,
	int outcome);

typedef int (*mh_opening_video_play)(void *context, unsigned int index);

/*
 * Entries 0,1,2; skip stops the complete sequence. Returns ENDED, SKIPPED,
 * CLOSED or FAILED. No allocation or OS ownership.
 */
int	mh_opening_videos_run(mh_opening_video_play play, void *context);

#endif
