#ifndef MH_ENGINE_GEOMETRY_LOAD_H
#define MH_ENGINE_GEOMETRY_LOAD_H

#include "engine/core/file.h"
#include "interfaces/geometry/decoder.h"

struct mh_resource_source;

/* Logical names are mapped through the explicitly selected decoder suffix. */
enum mh_error_code	mh_model_path_find_from(
	const struct mh_model_decoder *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *path, size_t capacity, int *found,
	struct mh_error *error_out);
enum mh_error_code	mh_model_load_from(
	const struct mh_model_decoder *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_model *output,
	struct mh_error *error_out);
enum mh_error_code	mh_world_load_from(
	const struct mh_world_decoder *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_world *output,
	struct mh_error *error_out);

#endif
