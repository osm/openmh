#include <stdio.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/catalog/provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_mesh_load.h"
#include "engine/assets/mesh_load.h"
#include "engine/catalog/load.h"
#include "engine/core/text.h"
#include "engine/geometry/world.h"
#include "interfaces/storage/resource_backend.h"

enum mh_error_code
mh_car_mesh_load(const char *root, unsigned int number,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	char name[128], struct mh_error *error_out)
{
	return mh_car_mesh_load_from(mh_catalog_fallback_source(),
		mh_model_myo_decoder(), mh_image_fallback_decoder(),
		mh_filesystem_resources(), root, number, collection, mesh, name,
		error_out);
}

enum mh_error_code
mh_mesh_environment_load(const char *root, unsigned int number,
	struct mh_mesh *mesh, struct mh_error *error_out)
{
	struct mh_track track;
	enum mh_error_code status;

	status = mh_track_config_load_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), root, number, &track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_mesh_environment_load_from(mh_image_fallback_decoder(),
		mh_filesystem_resources(), root, &track, mesh, error_out);

	if (status == MH_ERROR_NONE)
		printf("Track %u environment: %s\n", number,
			track.environment[0]);

	return status;
}

enum mh_error_code
mh_world_mesh_load(const char *root, unsigned int number, int standard_world,
	int scenery, int original_units,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	char name[256], struct mh_error *error_out)
{
	struct mh_track track;
	struct mh_world world;
	const struct mh_resource_source *resources;
	enum mh_error_code status;

	resources = mh_filesystem_resources();
	status = mh_track_config_load_from(mh_catalog_fallback_source(),
		resources, root, number, &track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_world_source_load_from(mh_world_myw_decoder(), resources,
		root, &track, standard_world, &world, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_text_copy(
		name, MH_TRACK_PATH_SIZE, track.world_file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_world_mesh_build_from(
			mh_animation_lightwave_source(), mh_model_myo_decoder(),
			mh_image_fallback_decoder(), resources, root, &track,
			&world, scenery, original_units, collection,
			MH_RENDER_GLIDE, mesh, error_out);

	if (status == MH_ERROR_NONE) {
		printf("Track %u: %lu x %lu cells, %lu faces; WorldBrightness %.2f %.2f %.2f\n",
			number, (unsigned long)world.width,
			(unsigned long)world.height,
			(unsigned long)world.face_count,
			track.world_brightness[0], track.world_brightness[1],
			track.world_brightness[2]);
	}

	mh_world_free(&world);

	return status;
}
