#ifndef MH_VIDEO_DECODER_H
#define MH_VIDEO_DECODER_H

#include <stddef.h>

#include "engine/core/error.h"

struct mh_video_decoder_provider;
struct mh_video_decoder;

/* Fixed-cadence clips retain the original frame count and interval. */
struct mh_video_decode_info {
	unsigned int	width, height;
	unsigned long	frames;
	double		interval_us;

	/* Zero rate/channels/bits means no audio track. */
	unsigned long	audio_rate;
	unsigned int	audio_channels, audio_bits;
};

/*
 * Tightly packed RGBA8 and interleaved unsigned-8 or signed-16 little-endian
 * PCM for one video frame. Views are borrowed until the next read or close.
 * A frame may have no PCM. Silent decoding always returns zero PCM bytes.
 */
struct mh_video_frame {
	const unsigned char	*rgba, *pcm;
	size_t			rgba_bytes, pcm_bytes;
};

/*
 * Encoded bytes are borrowed only during open; the provider owns any retained
 * encoding/codec state. Provider/table/context remain borrowed until close.
 * No path, archive layout, window, clock or audio device participates here.
 * Open leaves output unchanged on failure. Audio selects track decoding, not
 * device playback. Metadata remains valid until close.
 */
enum mh_error_code			 mh_video_decoder_open(
	const struct mh_video_decoder_provider *provider,
	const unsigned char *data, size_t size, int audio,
	struct mh_video_decoder **output, struct mh_error *error_out);
const struct mh_video_decode_info	*mh_video_decoder_info(
	const struct mh_video_decoder *decoder);

/*
 * Read sequentially from frame zero. At EOF, available is zero and output is
 * unchanged. Failed reads preserve output/available, but may invalidate a
 * preceding borrowed frame; close the decoder rather than retrying.
 */
enum mh_error_code	mh_video_decoder_read(struct mh_video_decoder *decoder,
	struct mh_video_frame *output, int *available,
	struct mh_error *error_out);
void			mh_video_decoder_close(
	struct mh_video_decoder *decoder);

#endif
