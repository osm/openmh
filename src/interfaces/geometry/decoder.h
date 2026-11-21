#ifndef MH_GEOMETRY_DECODER_H
#define MH_GEOMETRY_DECODER_H

#include "engine/core/error.h"
#include "interfaces/geometry/model.h"
#include "interfaces/geometry/world.h"

struct mh_model_decoder;
struct mh_world_decoder;

/*
 * Return independently owned, validated geometry. Returned data retains no
 * input or provider memory. Output is unchanged on failure; free successful
 * output with mh_model_free/mh_world_free before reusing it. Provider/context
 * are borrowed during the call. Diagnostics survive until the next operation.
 */
enum mh_error_code	mh_model_decode(const struct mh_model_decoder *decoder,
	const unsigned char *data, size_t size, struct mh_model *output,
	struct mh_error *error_out);
enum mh_error_code	mh_world_decode(const struct mh_world_decoder *decoder,
	const unsigned char *data, size_t size, struct mh_world *output,
	struct mh_error *error_out);

/* Borrowed extension without dot; NULL preserves the logical suffix. */
enum mh_error_code	mh_model_decoder_extension(
	const struct mh_model_decoder *decoder, const char **output,
	struct mh_error *error_out);
enum mh_error_code	mh_world_decoder_extension(
	const struct mh_world_decoder *decoder, const char **output,
	struct mh_error *error_out);

#endif
