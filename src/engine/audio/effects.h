#ifndef MH_AUDIO_EFFECTS_H
#define MH_AUDIO_EFFECTS_H

#include "engine/audio/effect_buffer.h"
#include "engine/audio/mixer.h"
#include "engine/core/error.h"

struct mh_effect_rate {
	unsigned long	whole, fraction;
};

struct mh_effects_mixer {
	struct mh_mixer		pcm;
	unsigned int		records[MH_SOUND_CHANNELS];
	struct mh_effect_rate	steps[MH_SOUND_CHANNELS];

	/*
	 * Physical loop bounds survive flag changes and rejected busy starts.
	 */
	size_t	loop_start[MH_SOUND_CHANNELS], loop_end[MH_SOUND_CHANNELS];
	size_t			cursors[MH_SOUND_CHANNELS];
	unsigned long		fractions[MH_SOUND_CHANNELS];

	/*
	 * Mixed 22050 Hz PCM neighbors and rational output phase. These are
	 * device output samples, not per-voice interpolated source samples.
	 */
	unsigned long		held[2], phase;
	int			output_ready;
};

/*
 * Source frames per 22050 Hz device frame, with a 32-bit fractional word. The
 * integer step is limited by the saved loop span even while looping is
 * disabled. Zero becomes one whole source frame.
 */
enum mh_error_code	mh_effects_rate(float frequency, size_t loop_frames,
	struct mh_effect_rate *rate, struct mh_error *error_out);

enum mh_error_code	mh_effects_init(unsigned long rate, double gain,
	struct mh_effects_mixer *effects, struct mh_error *error_out);

/*
 * Stop physical playback only. Logical records, pending requests and cached
 * scores survive until pool update.
 */
void	mh_effects_stop_device(struct mh_effects_mixer *effects);

/*
 * Apply explicit producer stops without admitting requests, refreshing
 * gains or advancing PCM. Call before releasing synchronized bank writes.
 */
enum mh_error_code	mh_effects_flush_stops(struct mh_effects_mixer *effects,
	const struct mh_sound_pool *pool, struct mh_error *error_out);

/*
 * Walk channel/pending references, stop only the physical channels actually
 * released, then publish activity to every logical bank. Unreachable activity
 * and old physical aliases survive. Errors are atomic.
 */
enum mh_error_code	mh_effects_clear(struct mh_effects_mixer *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count, struct mh_error *error_out);

/*
 * Banks occupy consecutive MH_MIXER_VOICES windows, borrow pool, and retain
 * logical activity until the scheduler consumes physical completion. No PCM
 * advances during update. Errors preserve pool, banks and backend.
 */
enum mh_error_code	mh_effects_update(struct mh_effects_mixer *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count, struct mh_error *error_out);

/*
 * Original buffer service using the same physical effects channels. These
 * functions produce raw signed-16/22050 Hz PCM in driver lock regions and
 * mirror physical cursors to logical banks. They do not refresh the pool or
 * apply output volume. Do not interleave with prefetched float rendering.
 */
enum mh_error_code	mh_effects_buffer_fill(struct mh_effects_mixer *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count, struct mh_effect_buffer *buffer,
	const struct mh_effect_buffer_driver *driver, void *context,
	unsigned long offset, int *filled, struct mh_error *error_out);
enum mh_error_code	mh_effects_buffer_poll(struct mh_effects_mixer *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count, struct mh_effect_buffer *buffer,
	const struct mh_effect_buffer_driver *driver, void *context,
	struct mh_error *error_out);

/*
 * Render original signed-16/22050 Hz physical PCM, then convert the mixed
 * signal to the requested float output rate. Bank cursors mirror the original
 * published positions, including retained entry positions on completion.
 * Completed logical records stay active until the next update. Call update
 * after logical bank changes and before rendering. Pause freezes everything.
 * The legacy gain is per-voice mix headroom; .125 reproduces unity device PCM.
 * Other rates use linear conversion of already-mixed PCM with one frame of
 * lookahead. Frames <=8192; errors preserve state and caller output.
 */
enum mh_error_code	mh_effects_render(struct mh_effects_mixer *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count, float *stereo, size_t frames, int paused,
	struct mh_error *error_out);

#endif
