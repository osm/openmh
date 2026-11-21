#include <stdlib.h>

#include "engine/assets/path_retro.h"
#include "engine/core/error.h"
#include "engine/image/load.h"
#include "engine/render/atmosphere_sky.h"
#include "engine/render/scene_internal.h"
#include "interfaces/retro/source.h"

enum mh_error_code
mh_race_scene_load_retro_from(const struct mh_retro_source *source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	struct mh_retro_assets *retro;
	enum mh_error_code status;

	if (scene == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing driving assets");

	if (scene->assets.retro != NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	retro = (struct mh_retro_assets *)malloc(sizeof(*retro));
	status = retro == NULL ? mh_error_set(error_out, MH_ERROR_MEMORY,
					"out of memory loading retro resource")
			: mh_retro_load_from(source, resources, root,
					MH_ASSET_RETRO_DIR, retro, error_out);

	if (status != MH_ERROR_NONE) {
		free(retro);
		return status;
	}

	scene->assets.retro = retro;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_sky(const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = mh_image_read_from(scene->image_decoder, resources, root,
		scene->assets.track.background, &file, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_sky_mesh_create(scene->image_decoder, file.data,
			file.size, &scene->visuals.sky_mesh, error_out);
		free(file.data);
	}

	/* Glide retains palette RGB; the other profiles prepare opaque RGB. */
	if (status == MH_ERROR_NONE &&
		(scene->assets.profile == MH_RENDER_DIRECT3D ||
			scene->assets.profile == MH_RENDER_POWERVR))
		status = mh_image_texture_format(
			&scene->visuals.sky_mesh.textures[1],
			scene->assets.opaque_format, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_image_read_from(scene->image_decoder, resources,
			root, scene->assets.track.environment[0], &file,
			error_out);

		if (status == MH_ERROR_NONE) {
			status = mh_mesh_set_environment(scene->image_decoder,
				&scene->visuals.sky_mesh, file.data, file.size,
				scene->assets.opaque_format, error_out);
			free(file.data);
		}
	}

	/* Environment loading allocates the complete texture-format table. */
	if (status == MH_ERROR_NONE &&
		(scene->assets.profile == MH_RENDER_DIRECT3D ||
			scene->assets.profile == MH_RENDER_POWERVR))
		scene->visuals.sky_mesh.texture_formats[1] =
			(unsigned char)scene->assets.opaque_format;

	return status;
}

enum mh_error_code
mh_race_scene_load_effects(const struct mh_resource_source *resources,
	const char *root, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (MH_RENDER_BUILD_SOFTWARE &&
		scene->assets.profile == MH_RENDER_SOFTWARE)
		return mh_race_scene_load_software(
			resources, root, scene, error_out);

	status = mh_skid_mesh_create(
		MH_RACE_MAX_CARS, &scene->visuals.skid_mesh, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_particles_from(scene->image_decoder,
			resources, root, scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_shadow_mesh_create(
			&scene->visuals.shadow_mesh, error_out);

	if (status == MH_ERROR_NONE)
		status = load_sky(resources, root, scene, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_race_scene_load_lamps_from(resources, root, scene, error_out);
}
