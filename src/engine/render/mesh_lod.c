#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/mesh.h"

static int
add_array_count(size_t *total, size_t count, size_t element_size)
{
	if (count > (size_t)-1 / element_size - *total)
		return 0;

	*total += count;

	return 1;
}

static enum mh_error_code
allocate_arrays(struct mh_mesh *mesh, struct mh_error *error_out)
{
	mesh->vertices = (struct mh_vertex *)calloc(
		mesh->vertex_count, sizeof(*mesh->vertices));
	mesh->draws = (struct mh_draw *)calloc(
		mesh->draw_count, sizeof(*mesh->draws));
	mesh->faces = (struct mh_mesh_face *)calloc(
		mesh->face_count, sizeof(*mesh->faces));
	mesh->primitives = (struct mh_mesh_primitive *)calloc(
		mesh->primitive_count, sizeof(*mesh->primitives));
	mesh->halos = (struct mh_geometry_halo *)calloc(
		mesh->halo_count, sizeof(*mesh->halos));
	mesh->textures = (struct mh_image *)calloc(
		mesh->texture_count, sizeof(*mesh->textures));

	if ((mesh->vertex_count != 0 && mesh->vertices == NULL) ||
		(mesh->draw_count != 0 && mesh->draws == NULL) ||
		(mesh->face_count != 0 && mesh->faces == NULL) ||
		(mesh->primitive_count != 0 && mesh->primitives == NULL) ||
		(mesh->halo_count != 0 && mesh->halos == NULL) ||
		(mesh->texture_count != 0 && mesh->textures == NULL)) {
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory packing model details");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
pack_level(struct mh_mesh *mesh, struct mh_mesh *source,
	const struct mh_mesh_lod *range, size_t texture_first)
{
	size_t i;

	if (source->vertex_count != 0)
		memcpy(mesh->vertices + range->vertex_first, source->vertices,
			source->vertex_count * sizeof(*source->vertices));

	for (i = 0; i < source->draw_count; i++) {
		mesh->draws[range->draw_first + i] = source->draws[i];
		mesh->draws[range->draw_first + i].first += range->vertex_first;
		mesh->draws[range->draw_first + i].texture += texture_first;
	}

	for (i = 0; i < source->face_count; i++) {
		mesh->faces[range->face_first + i] = source->faces[i];
		mesh->faces[range->face_first + i].first += range->vertex_first;
	}

	for (i = 0; i < source->primitive_count; i++) {
		mesh->primitives[range->primitive_first + i] =
			source->primitives[i];
		mesh->primitives[range->primitive_first + i].first +=
			range->vertex_first;
		mesh->primitives[range->primitive_first + i].draw +=
			range->draw_first;
	}

	if (source->halo_count != 0)
		memcpy(mesh->halos + range->halo_first, source->halos,
			source->halo_count * sizeof(*source->halos));

	for (i = 0; i < source->texture_count; i++) {
		mesh->textures[texture_first + i] = source->textures[i];
		source->textures[i].rgba = NULL;
	}
}

enum mh_error_code
mh_mesh_build_lods_profile(const struct mh_image_decoder_provider *decoder,
	const struct mh_model *models, unsigned int count,
	const struct mh_image_collection *collection,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	struct mh_mesh levels[MH_MESH_LODS], packed;
	struct mh_mesh *source;
	struct mh_mesh_lod *range;
	enum mh_error_code status;
	size_t texture_first;
	unsigned int level;

	if (models == NULL || mesh == NULL || count < 1 || count > MH_MESH_LODS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid model detail chain");

	memset(levels, 0, sizeof(levels));
	memset(&packed, 0, sizeof(packed));
	packed.radius = (float)models[0].radius;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (level = 0; level < count; level++) {
		source = &levels[level];
		range = &packed.lods[level];
		status = mh_mesh_build_native_profile(decoder, &models[level],
			collection, profile, source, error_out);

		if (status != MH_ERROR_NONE)
			goto done;

		range->vertex_first = packed.vertex_count;
		range->vertex_count = source->vertex_count;
		range->draw_first = packed.draw_count;
		range->draw_count = source->draw_count;
		range->face_first = packed.face_count;
		range->face_count = source->face_count;
		range->primitive_first = packed.primitive_count;
		range->primitive_count = source->primitive_count;
		range->halo_first = packed.halo_count;
		range->halo_count = source->halo_count;

		if (!add_array_count(&packed.vertex_count, source->vertex_count,
			sizeof(*packed.vertices)) ||
			!add_array_count(&packed.draw_count, source->draw_count,
				sizeof(*packed.draws)) ||
			!add_array_count(&packed.face_count, source->face_count,
				sizeof(*packed.faces)) ||
			!add_array_count(&packed.primitive_count,
				source->primitive_count,
				sizeof(*packed.primitives)) ||
			!add_array_count(&packed.halo_count, source->halo_count,
				sizeof(*packed.halos)) ||
			!add_array_count(&packed.texture_count,
				source->texture_count,
				sizeof(*packed.textures))) {
			status = mh_error_set(error_out, MH_ERROR_RANGE,
				"model detail arrays overflow");
			goto done;
		}
	}

	status = allocate_arrays(&packed, error_out);

	if (status != MH_ERROR_NONE)
		goto done;

	texture_first = 0;

	for (level = 0; level < count; level++) {
		source = &levels[level];
		range = &packed.lods[level];

		pack_level(&packed, source, range, texture_first);

		texture_first += source->texture_count;
	}

	packed.lod_count = count;
	*mesh = packed;
	memset(&packed, 0, sizeof(packed));

done:
	/*
	 * mh_mesh_free expects a present image table when its count is nonzero.
	 */
	if (packed.textures == NULL)
		packed.texture_count = 0;

	mh_mesh_free(&packed);

	for (level = 0; level < MH_MESH_LODS; level++)
		mh_mesh_free(&levels[level]);

	return status;
}

static int
range_fits(size_t first, size_t count, size_t capacity)
{
	return first <= capacity && count <= capacity - first;
}

enum mh_error_code
mh_mesh_active_lod(const struct mh_mesh *mesh, struct mh_mesh_lod *range,
	struct mh_error *error_out)
{
	struct mh_mesh_lod result;

	if (mesh == NULL || range == NULL || mesh->lod_count > MH_MESH_LODS ||
		(mesh->lod_count == 0 ? mesh->lod != 0
				: mesh->lod >= mesh->lod_count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid active model detail");

	memset(&result, 0, sizeof(result));

	if (mesh->lod_count != 0)
		result = mesh->lods[mesh->lod];
	else {
		result.vertex_count = mesh->vertex_count;
		result.draw_count = mesh->draw_count;
		result.face_count = mesh->face_count;
		result.primitive_count = mesh->primitive_count;
		result.halo_count = mesh->halo_count;
	}

	if (!range_fits(result.vertex_first, result.vertex_count,
		mesh->vertex_count) ||
		!range_fits(result.draw_first, result.draw_count,
			mesh->draw_count) ||
		!range_fits(result.face_first, result.face_count,
			mesh->face_count) ||
		!range_fits(result.primitive_first, result.primitive_count,
			mesh->primitive_count) ||
		!range_fits(
			result.halo_first, result.halo_count, mesh->halo_count))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"model detail range exceeds mesh");

	*range = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
