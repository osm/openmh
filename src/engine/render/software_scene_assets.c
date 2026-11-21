#include <stdlib.h>
#include <string.h>

#include "engine/image/load.h"
#include "engine/render/scene_internal.h"
#include "engine/render/software_shadow_mesh.h"

void
mh_software_scene_assets_free(struct mh_software_scene_assets *assets)
{
	unsigned int i;

	free(assets->sky.rgba);
	free(assets->lens.indices);
	mh_name_images_free(&assets->names);

	for (i = 0; i < 3; i++)
		free(assets->environment[i].indices);

	mh_software_particle_assets_free(&assets->particles);
	mh_software_halo_assets_free(&assets->track_halos);

	for (i = 0; i < MH_RACE_MAX_CARS; i++)
		mh_software_halo_assets_free(&assets->car_halos[i]);

	memset(assets, 0, sizeof(*assets));
}

enum mh_error_code
mh_race_scene_load_software(const struct mh_resource_source *resources,
	const char *root, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	struct mh_software_scene_assets *assets;
	unsigned int i;
	enum mh_error_code status;

	assets = &scene->assets.software;
	status = mh_image_load_from(scene->image_decoder, resources, root,
		scene->assets.track.background, NULL, &assets->sky, NULL,
		error_out);

	for (i = 0; i < 3 && status == MH_ERROR_NONE; i++)
		status = mh_image_load_indexed_from(scene->image_decoder,
			resources, root, scene->assets.track.environment[i],
			&assets->environment[i], error_out);

	if (status == MH_ERROR_NONE)
		status = mh_software_particle_assets_load_from(
			scene->image_decoder, resources, root,
			&assets->particles, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_software_halo_assets_load_from(scene->image_decoder,
			resources, root, &scene->assets.track.halos,
			&assets->track_halos, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_software_lens_image_load_from(scene->image_decoder,
			resources, root, &assets->lens, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_software_shadow_mesh_create(
			&scene->visuals.shadow_mesh, error_out);

	if (status == MH_ERROR_NONE)
		mh_race_scene_reset_particles(scene);

	return status;
}
