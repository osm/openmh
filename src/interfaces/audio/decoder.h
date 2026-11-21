#ifndef MH_AUDIO_DECODER_H
#define MH_AUDIO_DECODER_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_audio_decoder_provider;
struct mh_audio_decoder;

/*
 * Decoded interleaved PCM: mono/stereo, unsigned 8 or signed little-endian 16.
 * frames is the exact decoded frame count, not a container estimate.
 */
struct mh_audio_decode_info {
	size_t		frames;
	unsigned long	rate;
	unsigned int	channels, bits;
};

/* Borrowed lowercase file extension, without a dot; NULL if unavailable. */
const char	*mh_audio_decoder_extension(
	const struct mh_audio_decoder_provider *provider);

/* Metadata inspection must not decode/read the complete PCM payload. Atomic. */
enum mh_error_code	mh_audio_decoder_inspect(
	const struct mh_audio_decoder_provider *provider,
	const struct mh_resource_source *resources, const char *path,
	struct mh_audio_decode_info *output, struct mh_error *error_out);

/*
 * Provider and resources remain borrowed through close. The path is borrowed
 * only during open. Output is unchanged on failure; successful open owns one
 * decoder, initially positioned at frame zero. No audio device is involved.
 */
enum mh_error_code	mh_audio_decoder_open(
	const struct mh_audio_decoder_provider *provider,
	const struct mh_resource_source *resources, const char *path,
	struct mh_audio_decoder **output, struct mh_error *error_out);

/*
 * Metadata remains borrowed and unchanged through close. NULL for NULL decoder.
 */
const struct mh_audio_decode_info	*mh_audio_decoder_info(
	const struct mh_audio_decoder *decoder);

/*
 * Read up to requested complete frames into capacity bytes. Short reads are
 * allowed; zero frames means EOF. On error, count is unchanged and the buffer
 * may contain partial data. Decoder position remains unchanged on failure.
 * Calls on a decoder are serialized by its owner, not internally synchronized.
 */
enum mh_error_code	mh_audio_decoder_read(struct mh_audio_decoder *decoder,
	void *output, size_t capacity, size_t requested, size_t *count,
	struct mh_error *error_out);

/* Absolute decoded-frame offset, including EOF. Failure preserves position. */
enum mh_error_code	mh_audio_decoder_seek(struct mh_audio_decoder *decoder,
	size_t frame, struct mh_error *error_out);
void			mh_audio_decoder_close(
	struct mh_audio_decoder *decoder);

#endif
