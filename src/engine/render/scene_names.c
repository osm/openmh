#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_fonts.h"
#include "engine/core/error.h"
#include "engine/render/name_mesh.h"
#include "engine/render/powervr_lamp_atlas.h"
#include "engine/render/scene.h"
#include "interfaces/font/source.h"

#define NAME_IMAGE_WIDTH 256
#define NAME_LEFT_MARGIN 2

static enum mh_error_code
clip_names(const struct mh_font *font, const struct mh_field *field,
	struct mh_profile profiles[MH_NAME_LABELS_MAX], unsigned int limit,
	struct mh_error *error_out)
{
	struct mh_profile *profile;
	unsigned int entry, glyph, width, code;

	for (entry = 0; entry < field->count; entry++) {
		if (!(field->active_mask & (1U << entry)))
			continue;

		profile = &profiles[entry];

		if (memchr(profile->nick, 0, sizeof(profile->nick)) == NULL) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated driver nickname");
		}

		/*
		 * The original can overflow its image or atlas cell for wide
		 * custom names. Clip only this display copy at a glyph
		 * boundary.
		 */
		width = NAME_LEFT_MARGIN;

		for (glyph = 0; profile->nick[glyph] != 0; glyph++) {
			code = (unsigned char)profile->nick[glyph];

			if (font->advance[code] > limit - width) {
				profile->nick[glyph] = 0;
				break;
			}

			width += font->advance[code];
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_names_prepare_from(const struct mh_font_source_provider *fonts,
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, const struct mh_field *field,
	const struct mh_profile roster[MH_NAME_LABELS_MAX], unsigned int count,
	struct mh_error *error_out)
{
	struct mh_font font;
	struct mh_name_mesh names;
	struct mh_name_images images;
	struct mh_profile profiles[MH_NAME_LABELS_MAX];
	enum mh_error_code status;

	if (scene == NULL || field == NULL || field->count < 1 ||
		field->count > MH_NAME_LABELS_MAX || roster == NULL ||
		count != field->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driver name roster");

	status = mh_font_load(fonts, resources, root,
		scene->assets.profile == MH_RENDER_SOFTWARE
			? MH_ASSET_FONT1_LOWERCASE
			: MH_ASSET_FONT2_LOWERCASE,
		&font, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(profiles, 0, sizeof(profiles));
	memcpy(profiles, roster, count * sizeof(*profiles));

	status = clip_names(&font, field, profiles,
		MH_RENDER_BUILD_POWERVR &&
				scene->assets.profile == MH_RENDER_POWERVR
			? MH_POWERVR_NAME_CELL_WIDTH
			: NAME_IMAGE_WIDTH,
		error_out);

	if (status != MH_ERROR_NONE) {
		free(font.image.rgba);
		return status;
	}

	if (scene->assets.profile == MH_RENDER_SOFTWARE) {
		status = mh_name_images_create(&font, profiles, field->count,
			field->active_mask, &images, error_out);
		free(font.image.rgba);

		if (status != MH_ERROR_NONE)
			return status;

		mh_name_images_free(&scene->assets.software.names);
		scene->assets.software.names = images;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (MH_RENDER_BUILD_POWERVR &&
		scene->assets.profile == MH_RENDER_POWERVR)
		status = mh_powervr_name_mesh_create(&font, profiles,
			field->count, field->active_mask,
			&scene->assets.lamp_atlas, scene->assets.lamp_palette,
			&names, error_out);
	else
		status = mh_name_mesh_create(&font, profiles, field->count,
			field->active_mask, &names, error_out);
	free(font.image.rgba);

	if (status != MH_ERROR_NONE)
		return status;

	mh_name_mesh_free(&scene->visuals.names);
	scene->visuals.names = names;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_names(struct mh_race_scene *scene, const struct mh_field *field,
	const float *models, const float view[16], unsigned int width,
	unsigned int height, int enabled, struct mh_error *error_out)
{
	struct mh_projection projection;
	struct mh_frustum frustum;
	const struct mh_drive *viewed;
	float positive_view[12], positions[MH_NAME_LABELS_MAX * 3],
		radii[MH_NAME_LABELS_MAX];
	float focal, far_distance;
	unsigned int i, j, mask, viewed_entry, root;
	enum mh_error_code status;
	int chase;

	if (scene == NULL || field == NULL || models == NULL || view == NULL ||
		field->count < 1 || field->count > MH_NAME_LABELS_MAX ||
		scene->visuals.names.count != field->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driver name frame");

	viewed_entry = mh_field_viewed(field);
	viewed = &field->cars[viewed_entry];
	focal = viewed->camera_goal || viewed->camera_mode == MH_CAMERA_TRACK
		? viewed->camera_focal
		: MH_CAMERA_RACE_FOCAL;
	far_distance = (float)mh_drive_camera_far(viewed);

	for (i = 0; i < 4; i++)
		for (j = 0; j < 3; j++)
			positive_view[i * 3 + j] =
				j == 2 ? -view[i * 4 + j] : view[i * 4 + j];

	status = mh_projection_setup(width, height, (float)width / height,
		focal, MH_CAMERA_RACE_NEAR, far_distance, &projection,
		error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_frustum(
			field, width, height, &frustum, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field->count; i++) {
		root = 1 + MH_CAR_MESHES * i;

		for (j = 0; j < 3; j++)
			positions[i * 3 + j] = models[root * 16 + 12 + j];

		radii[i] = scene->assets.meshes[root].radius;
	}

	chase = !viewed->camera_goal &&
		(viewed->camera_mode == MH_CAMERA_CHASE ||
			(viewed->camera_mode == MH_CAMERA_IN_CAR &&
				viewed->camera_roll.external));
	mask = enabled
		? mh_name_label_mask(field->count, field->active_mask, 0,
			viewed_entry, mh_drive_camera_in_car(viewed), chase)
		: 0;

	return mh_name_mesh_update(&scene->visuals.names, mask, positions,
		radii, positive_view, &projection, &frustum, error_out);
}
