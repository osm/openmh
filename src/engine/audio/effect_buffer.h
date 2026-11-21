#ifndef MH_AUDIO_EFFECT_BUFFER_H
#define MH_AUDIO_EFFECT_BUFFER_H

#include "engine/audio/effect_pcm.h"
#include "engine/core/error.h"

#define MH_EFFECT_BUFFER_FILL_BYTES 4096U
#define MH_EFFECT_BUFFER_ERROR_LOST 0x88780096UL

enum mh_effect_buffer_status {
	MH_EFFECT_BUFFER_STATUS_PLAYING = 1,
	MH_EFFECT_BUFFER_STATUS_LOST = 2,
	MH_EFFECT_BUFFER_STATUS_LOOPING = 4
};

/*
 * Byte offsets, not native sample pointers. The requested first offset is
 * retained even if the driver wraps its lock.
 */
struct mh_effect_buffer {
	unsigned long	first_start, first_end, second_start, second_end;
	unsigned long	play, write;
};

struct mh_effect_buffer_region {
	unsigned char	*data;
	size_t		bytes;
};

/*
 * Zero means device success. Status must write its value even on error;
 * the original ignores its return and both callers ignore Restore errors.
 * Lock returns disjoint writable regions totalling 4096 bytes, each aligned
 * to four-byte stereo frames. They must not alias channel/source storage.
 * The caller owns the driver, context and their synchronization.
 */
struct mh_effect_buffer_driver {
	unsigned long		(*status)(void *context, unsigned long *value);
	unsigned long (*position)(
		void	*context, unsigned long *play, unsigned long *write);
	unsigned long		(*restore)(void *context);
	unsigned long (*lock)(void *context, unsigned long offset,
		unsigned long	bytes, struct mh_effect_buffer_region regions[2]);
	unsigned long (*unlock)(
		void	*context, const struct mh_effect_buffer_region regions[2]);
};

/*
 * Mix each locked region as a separate 22050 Hz block; publish ranges before
 * Unlock. filled is the original Boolean result: failed Lock leaves PCM/state
 * intact; failed Unlock keeps their changes. A non-success status denotes
 * invalid native state/regions. Device failure returns MH_ERROR_NONE with
 * filled set to zero. Invalid regions are unlocked without touching PCM,
 * channels or ranges.
 */
enum mh_error_code	mh_effect_buffer_fill(struct mh_effect_buffer *buffer,
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	const struct mh_effect_buffer_driver *driver, void *context,
	unsigned long offset, int *filled, struct mh_error *error_out);

/*
 * Registered scheduler's periodic poll. NULL driver models an absent buffer.
 * Only a play cursor strictly inside the last fill permits one new fill. A
 * missed interval does not trigger catch-up or a jump to the write cursor.
 * Device failures are consumed; validation errors propagate.
 */
enum mh_error_code	mh_effect_buffer_poll(struct mh_effect_buffer *buffer,
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	const struct mh_effect_buffer_driver *driver, void *context,
	struct mh_error *error_out);

#endif
