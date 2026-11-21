#ifndef MH_COLLISION_DECODER_BACKEND_H
#define MH_COLLISION_DECODER_BACKEND_H

#include "interfaces/collision/decoder.h"

/*
 * Output arrays are separately malloc-owned. Validate array spans/references,
 * tree depth/cycles, finite grid transforms and unit plane normals before
 * publishing output. Preserve surface IDs and list order. Failure frees partial
 * state and leaves output unchanged. Context/input are not retained by output.
 */
struct mh_collision_decoder {
	void			*context;

	/* NULL retains suffixes; otherwise 1..16 lowercase letters/digits. */
	const char		*extension;
	enum mh_error_code (*decode)(void *context, const unsigned char *data,
		size_t size, struct mh_collision *output,
		struct mh_error	*error_out);
};

#endif
