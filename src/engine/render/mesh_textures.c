#include <stdlib.h>
#include <string.h>

#include "engine/catalog/track.h"
#include "engine/core/error.h"
#include "engine/image/collection_load.h"
#include "engine/render/mesh_internal.h"

static enum mh_error_code
register_draws(const struct mh_mesh *mesh, size_t first, size_t count,
	const struct mh_atlas_map *remap, struct mh_texture_groups *groups,
	unsigned char *indices, struct mh_error *error_out)
{
	enum mh_error_code status;
	const char *name;
	size_t i;
	unsigned int group;

	if (first > mesh->draw_count || count > mesh->draw_count - first)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid registration draw span");

	for (i = first; i < first + count; i++) {
		name = mesh->draws[i].texture_name;

		if (memchr(name, 0, sizeof(mesh->draws[i].texture_name)) ==
			NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated material texture name");

		if (name[0] == 0)
			continue;

		status = mh_atlas_map_group(
			remap, name, groups, &group, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		indices[i] = (unsigned char)(group + 1);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_register_textures(struct mh_mesh *mesh,
	const struct mh_atlas_map *remap, struct mh_texture_groups *groups,
	struct mh_error *error_out)
{
	struct mh_texture_groups pending;
	struct mh_mesh_span span;
	const struct mh_mesh_world_model *model;
	enum mh_error_code status;
	unsigned char *indices;
	size_t i;
	size_t index;

	if (mesh == NULL || remap == NULL || groups == NULL ||
		groups->count > MH_TEXTURE_GROUPS ||
		(mesh->draw_count != 0 && mesh->draws == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid mesh registration");

	pending = *groups;
	indices = (unsigned char *)calloc(
		mesh->draw_count == 0 ? 1 : mesh->draw_count, 1);

	if (indices == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory registering mesh textures");

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mesh->world == NULL)
		status = register_draws(mesh, 0, mesh->draw_count, remap,
			&pending, indices, error_out);
	else {
		for (i = 0;
			status == MH_ERROR_NONE && i < mesh->world->cell_count;
			i++) {
			index = mesh->world->cells[i].model;

			if (index == MH_WORLD_NO_MODEL)
				continue;

			if (index >= mesh->world->model_count) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"invalid registration world model");
				break;
			}

			model = &mesh->world->models[index];
			status = register_draws(mesh, model->draw_first,
				model->draw_count, remap, &pending, indices,
				error_out);
		}

		for (i = 1; status == MH_ERROR_NONE &&
			i < mh_mesh_scene_span_count(mesh);
			i++) {
			if (!mh_mesh_scene_span(mesh, i, &span)) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"invalid registration scene span");
				break;
			}

			status = register_draws(mesh, span.draw_first,
				span.draw_count, remap, &pending, indices,
				error_out);
		}
	}

	for (i = 0; status == MH_ERROR_NONE && i < mesh->draw_count; i++)
		if (mesh->draws[i].texture_name[0] != 0 && indices[i] == 0)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"unregistered material draw");

	if (status != MH_ERROR_NONE) {
		free(indices);
		return status;
	}

	free(mesh->draw_groups);
	mesh->draw_groups = indices;
	*groups = pending;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_mesh_track_materials(struct mh_mesh *mesh, const struct mh_track *track)
{
	size_t i;

	for (i = 0; i < mesh->draw_count; i++) {
		if (!mesh->draws[i].environment &&
			mh_track_zero_transparent(
				track, mesh->draws[i].texture_name))
			mesh->draws[i].alpha_test = 1;
	}
}

enum mh_error_code
mh_mesh_prepare_track_textures(const struct mh_image_decoder_provider *decoder,
	struct mh_mesh *mesh,
	const struct mh_image_collection *cutout_collection,
	const struct mh_image_collection *opaque_collection,
	const struct mh_track *track, enum mh_texture_format opaque_format,
	enum mh_texture_format alpha_format, struct mh_error *error_out)
{
	struct mh_image *prepared;
	struct mh_image_decode_options options;
	unsigned char *formats;
	const struct mh_image_collection *collection;
	const struct mh_draw *draw;
	enum mh_error_code status;
	size_t i, texture_index;
	int cutout;

	if (mesh == NULL || cutout_collection == NULL ||
		opaque_collection == NULL || track == NULL ||
		mesh->textures == NULL ||
		(opaque_format != MH_TEXTURE_PALETTE_RGB &&
			opaque_format != MH_TEXTURE_RGB565 &&
			opaque_format != MH_TEXTURE_RGB555 &&
			opaque_format != MH_TEXTURE_XRGB8888) ||
		(alpha_format != MH_TEXTURE_ARGB1555 &&
			alpha_format != MH_TEXTURE_ARGB4444) ||
		mesh->texture_count == 0 ||
		mesh->texture_count > (size_t)-1 / sizeof(*prepared) ||
		(mesh->draw_count != 0 && mesh->draws == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track texture table");

	prepared = (struct mh_image *)calloc(
		mesh->texture_count, sizeof(*prepared));

	formats = (unsigned char *)calloc(mesh->texture_count, 1);

	if (prepared == NULL || formats == NULL) {
		free(prepared);
		free(formats);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing track textures");
	}

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	options.alpha = MH_IMAGE_ALPHA_INDEX_ZERO;
	options.palette = NULL;

	/*
	 * Repreparing named images must retain unrelated environment metadata.
	 */
	if (mesh->texture_format_count != 0)
		memcpy(formats, mesh->texture_formats,
			mesh->texture_format_count < mesh->texture_count
				? mesh->texture_format_count
				: mesh->texture_count);

	for (i = 0; i < mesh->draw_count && status == MH_ERROR_NONE; i++) {
		draw = &mesh->draws[i];
		texture_index = draw->texture;

		if (texture_index >= mesh->texture_count ||
			memchr(draw->texture_name, 0,
				sizeof(draw->texture_name)) == NULL) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid named track texture");
			break;
		}

		if (draw->texture_name[0] == 0 ||
			(mesh->environment_texture != 0 &&
				texture_index == mesh->environment_texture) ||
			prepared[texture_index].rgba != NULL)
			continue;

		cutout = mh_track_texture_cutout(track, draw->texture_name);
		formats[texture_index] =
			(unsigned char)(cutout ? alpha_format : opaque_format);
		collection = cutout ? cutout_collection : opaque_collection;

		status = mh_image_collection_load(decoder, collection,
			draw->texture_name, &options, &prepared[texture_index],
			NULL, error_out);

		if (status == MH_ERROR_NONE &&
			(prepared[texture_index].width !=
					mesh->textures[texture_index].width ||
				prepared[texture_index].height !=
					mesh->textures[texture_index].height))
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"track texture collection dimensions differ");

		if (status == MH_ERROR_NONE)
			status = mh_image_texture_brightness(
				&prepared[texture_index],
				track->texture_brightness, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_image_texture_format(
				&prepared[texture_index],
				(enum mh_texture_format)formats[texture_index],
				error_out);
	}

	for (i = 0; i < mesh->texture_count; i++) {
		if (status == MH_ERROR_NONE && prepared[i].rgba != NULL) {
			free(mesh->textures[i].rgba);
			mesh->textures[i] = prepared[i];
		} else
			free(prepared[i].rgba);
	}

	free(prepared);

	if (status == MH_ERROR_NONE) {
		free(mesh->texture_formats);
		mesh->texture_formats = formats;
		mesh->texture_format_count = mesh->texture_count;
	} else
		free(formats);

	return status;
}
