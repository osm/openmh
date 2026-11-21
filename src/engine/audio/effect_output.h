#ifndef MH_AUDIO_EFFECT_OUTPUT_H
#define MH_AUDIO_EFFECT_OUTPUT_H

#include "engine/audio/effect_buffer.h"
#include "engine/core/error.h"

/*
 * Native driver policy, not constants from DirectSound. The half-block initial
 * lead keeps successful fill endpoints off the exact ring boundary, whose
 * original unnormalized range can stall polling.
 */
#define MH_EFFECT_OUTPUT_BYTES 16384U
#define MH_EFFECT_OUTPUT_LEAD 2048U

struct mh_effect_output {
	struct mh_effect_buffer	buffer;
	unsigned char		*data;
	size_t			bytes;
	unsigned long		rate; /* Output frames per second. */
	unsigned long		lead_bytes, play_offset;
	unsigned long		phase; /* Fraction with denominator rate. */
	unsigned long		consumed_frames, fills, wraps;
	size_t			locked_offset;
	int			locked;
};

/*
 * Caller-owned raw 22050 Hz stereo ring; capacity and lead are four-byte
 * aligned. Output rate is 4000..192000. Zero the ring and begin at play zero,
 * write lead; call the effects buffer's initial fill before enabling output.
 * Errors leave storage/output unchanged. Objects/storage must not overlap.
 */
enum mh_error_code			 mh_effect_output_init(
	unsigned char *storage, size_t bytes, unsigned long lead_bytes,
	unsigned long rate, struct mh_effect_output *output,
	struct mh_error *error_out);
const struct mh_effect_buffer_driver	*mh_effect_output_driver(void);

/*
 * Consume existing ring contents only, never generate physical effects.
 * Cursor counts source frames submitted to the platform, not a hardware
 * speaker timestamp. Linear conversion and gain are native output policy;
 * gain .125 preserves original PCM amplitude. Frames <=8192. Pause returns
 * silence without advancing. Caller output must not overlap ring/state.
 */
enum mh_error_code	mh_effect_output_render(struct mh_effect_output *output,
	double gain, float *stereo, size_t frames, int paused,
	struct mh_error *error_out);

/*
 * Full effects-device departure discards its ring. Ordinary channel stops,
 * restarts and listener changes must retain already mixed ring contents.
 */
enum mh_error_code	mh_effect_output_clear(struct mh_effect_output *output,
	struct mh_error *error_out);

#endif
