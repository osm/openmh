#ifndef MH_AUDIO_DECODER_BACKEND_H
#define MH_AUDIO_DECODER_BACKEND_H

#include "interfaces/audio/decoder.h"

/* Descriptor, operation table and context remain borrowed through close. */
struct mh_audio_decoder_provider {
	const char				*extension;
	const struct mh_audio_decoder_ops	*ops;
	void					*context;

	/*
	 * Optional logical sample lookup; NULL uses the provider extension.
	 * Borrow inputs for the call; write a terminated path within capacity.
	 */
	enum mh_error_code (*resolve)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, char *path, size_t capacity,
		struct mh_error			*error_out);
};

struct mh_audio_decoder_ops {
	enum mh_error_code (*inspect)(void *context,
		const struct mh_resource_source *resources, const char *path,
		struct mh_audio_decode_info *output,
		struct mh_error	*error_out);

	/*
	 * Clean partial state on failure; successful open receives one close.
	 * Copy any path needed later. Info describes the decoded output, not
	 * its container encoding. Failure leaves both output arguments
	 * unchanged.
	 */
	enum mh_error_code (*open)(void *context,
		const struct mh_resource_source *resources, const char *path,
		void **output, struct mh_audio_decode_info *info,
		struct mh_error	*error_out);

	/*
	 * The dispatcher guarantees enough space for requested complete frames.
	 */
	enum mh_error_code (*read)(void *context, void *decoder, void *output,
		size_t	requested, size_t *count, struct mh_error *error_out);
	enum mh_error_code (*seek)(void *context, void *decoder, size_t frame,
		struct mh_error	*error_out);
	void			(*close)(void *context, void *decoder);
};

#endif
