#ifndef MH_COLLISION_DECODER_H
#define MH_COLLISION_DECODER_H

#include "engine/core/error.h"
#include "interfaces/collision/data.h"

struct mh_collision_decoder;

/*
 * Decode fully validated, owned collision data. No input bytes survive the
 * call. Output is unchanged on failure; release successful output with
 * mh_collision_free before reusing it. The descriptor/context are borrowed
 * during the call. Diagnostics remain valid until the next provider operation.
 */
enum mh_error_code	mh_collision_decode(
	const struct mh_collision_decoder *decoder, const unsigned char *data,
	size_t size, struct mh_collision *output, struct mh_error *error_out);

/* Borrowed extension without dot; NULL retains the logical reference suffix. */
enum mh_error_code	mh_collision_decoder_extension(
	const struct mh_collision_decoder *decoder, const char **output,
	struct mh_error *error_out);

#endif
