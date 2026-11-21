#ifndef MH_ASSETS_MESH_LOAD_H
#define MH_ASSETS_MESH_LOAD_H

#include "engine/core/error.h"
#include "engine/core/file.h"
#include "engine/render/mesh.h"
#include "interfaces/catalog/track.h"
#include "interfaces/geometry/decoder.h"

struct mh_catalog_source;
struct mh_animation_source;
struct mh_resource_source;

/*
 * Assemble owned meshes through the resource provider. Source model/image
 * bytes can be released after success. Names are copied to caller storage.
 * The source is borrowed for the load; returned assets own their bytes.
 */
enum mh_error_code	mh_car_mesh_load_from(
	const struct mh_catalog_source *catalog_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	unsigned int number, const struct mh_image_collection *collection,
	struct mh_mesh *mesh, char name[128], struct mh_error *error_out);

/* Apply the track's first environment map and material settings to a mesh. */
enum mh_error_code	mh_mesh_environment_load_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, struct mh_mesh *mesh,
	struct mh_error *error_out);

/* Build from the loaded track and world. Both remain owned by the caller. */
enum mh_error_code	mh_world_mesh_build_from(
	const struct mh_animation_source *animation_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, const struct mh_world *world, int scenery,
	int original_units, const struct mh_image_collection *collection,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out);

/* Decoded world owns its arrays; encoded input is released during loading. */
enum mh_error_code	mh_world_source_load_from(
	const struct mh_world_decoder *world_decoder,
	const struct mh_resource_source *source, const char *root,
	const struct mh_track *track, int standard_world,
	struct mh_world *world, struct mh_error *error_out);

#endif
