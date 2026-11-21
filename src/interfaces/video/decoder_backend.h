#ifndef MH_VIDEO_DECODER_BACKEND_H
#define MH_VIDEO_DECODER_BACKEND_H

#include "interfaces/video/decoder.h"

struct mh_video_decoder_provider {
	const struct mh_video_decoder_ops	*ops;
	void					*context;
};

struct mh_video_decoder_ops {
	/*
	 * Failure releases partial state; success receives exactly one close.
	 */
	enum mh_error_code (*open)(void *context, const unsigned char *data,
		size_t size, int audio, void **output,
		struct mh_video_decode_info	*info, struct mh_error *error_out);

	/* Called exactly once per expected frame, in order, until failure. */
	enum mh_error_code (*read)(void *context, void *decoder,
		struct mh_video_frame	*output, struct mh_error *error_out);
	void	(*close)(void *context, void *decoder);
};

#endif
