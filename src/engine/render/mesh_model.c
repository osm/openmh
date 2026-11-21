#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/image/collection_load.h"
#include "engine/render/mesh_internal.h"

#define PART_OFFSET_LIMIT 1e30

enum {
	FACE_ENVIRONMENT_FLAGS = 0x60,
	FACE_ALPHA_TEST_FLAG = 2
};

enum mh_error_code
mh_mesh_init_model(const struct mh_image_decoder_provider *decoder,
	const struct mh_model *model,
	const struct mh_image_collection *collection, size_t faces,
	struct mh_mesh *mesh, struct mh_error *error_out)
{
	struct mh_mesh built;
	struct mh_image_decode_options options;
	enum mh_error_code status;
	size_t texture_count;
	size_t i;

	memset(&built, 0, sizeof(built));

	if (faces == 0 || faces > (size_t)-1 / (6 * sizeof(*built.vertices)) ||
		faces > (size_t)-1 / sizeof(*built.draws) ||
		faces > (size_t)-1 / sizeof(*built.primitives)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"empty or oversized inspection mesh");
	}

	built.vertices =
		(struct mh_vertex *)malloc(faces * 6 * sizeof(*built.vertices));
	built.draws = (struct mh_draw *)malloc(faces * sizeof(*built.draws));
	built.primitives = (struct mh_mesh_primitive *)malloc(
		faces * sizeof(*built.primitives));
	texture_count = model->texture_count;

	if (texture_count >= (size_t)-1 / sizeof(*built.textures)) {
		mh_mesh_free(&built);
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many model textures");
	}

	built.textures = (struct mh_image *)calloc(
		texture_count + 1, sizeof(*built.textures));

	if (built.vertices == NULL || built.draws == NULL ||
		built.textures == NULL || built.primitives == NULL) {
		mh_mesh_free(&built);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory building inspection mesh");
	}

	built.texture_count = texture_count + 1;
	built.textures[0].width = 1;
	built.textures[0].height = 1;
	built.textures[0].rgba = (unsigned char *)malloc(4);

	if (built.textures[0].rgba == NULL) {
		mh_mesh_free(&built);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating white texture");
	}

	memset(built.textures[0].rgba, 255, 4);

	options.alpha = MH_IMAGE_ALPHA_INDEX_ZERO;
	options.palette = NULL;

	for (i = 1; i < built.texture_count; i++) {
		status = mh_image_collection_load(decoder, collection,
			model->textures[i - 1], &options, &built.textures[i],
			NULL, error_out);

		if (status != MH_ERROR_NONE) {
			mh_mesh_free(&built);
			return status;
		}
	}

	*mesh = built;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Software loading reduces per-corner materials to flat color. */
static void
software_face(struct mh_geometry_face *face)
{
	unsigned int channel, corner, sum;
	double weight;

	if (face->type < 4 || face->type > 7)
		return;

	if (face->type >= 6) {
		/* The triangle constant is 0.33, not one third. */
		weight = face->count == 3 ? 0.33 : 0.25;

		for (channel = 0; channel < 3; channel++) {
			sum = 0;

			for (corner = 0; corner < face->count; corner++)
				sum += face->corners[corner].color[channel];

			face->color[channel] = (unsigned char)(sum * weight);
		}
	}

	for (corner = 0; corner < face->count; corner++)
		memcpy(face->corners[corner].color, face->color, 3);

	face->type -= 4;
}

static void
retain_face(const struct mh_geometry_face *face, size_t count,
	struct mh_mesh *built)
{
	struct mh_mesh_face *retained;
	unsigned int axis;

	retained = &built->faces[built->face_count];
	retained->type = (unsigned char)face->type;
	retained->flags = (unsigned char)face->flags;
	retained->first = built->vertex_count;
	retained->count = (unsigned int)count;
	memcpy(retained->color, face->color, 3);

	for (axis = 0; axis < 4; axis++) {
		retained->plane[axis] = (float)face->plane[axis];
	}

	built->face_count++;
}

static void
append_vertices(const struct mh_geometry_face *face, size_t count,
	size_t material, int prelit, const double *brightness,
	struct mh_mesh *built)
{
	static const unsigned int triangles[6] = {0, 1, 2, 0, 2, 3};
	const struct mh_geometry_corner *corner;
	struct mh_vertex *vertex;
	size_t j, color_offset;
	unsigned int axis;
	double value;

	for (j = 0; j < count; j++) {
		corner = &face->corners[triangles[j]];
		vertex = &built->vertices[built->vertex_count++];
		vertex->projection_w = 0;

		/* Specialized passes prepare opacity before use. */
		vertex->opacity = 0;

		for (axis = 0; axis < 3; axis++) {
			vertex->position[axis] = (float)corner->position[axis];
			vertex->normal[axis] = (float)corner->normal[axis];

			/*
			 * World faces carry baked RGB, including textured
			 * faces. Ordinary car textures keep a neutral white
			 * tint.
			 */
			value = material == 0 || prelit ? corner->color[axis]
							: 255.0;

			if (built->world_colors != NULL) {
				color_offset =
					(built->vertex_count - 1) * 3 + axis;
				built->world_colors[color_offset] =
					(unsigned char)corner->color[axis];
			}

			if (brightness != NULL)
				value *= brightness[axis];

			if (value > 255.0)
				value = 255.0;

			vertex->color[axis] = (float)(value / 255.0);
		}

		vertex->uv[0] = (float)(corner->uv[0] /
			built->textures[material].width);
		vertex->uv[1] = (float)(corner->uv[1] /
			built->textures[material].height);
	}
}

enum mh_error_code
mh_mesh_append_model(const struct mh_model *model, const double *brightness,
	enum mh_render_profile profile, struct mh_mesh *built,
	struct mh_error *error_out)
{
	struct mh_geometry_face face;
	struct mh_draw *draw;
	size_t face_index;
	size_t count;
	size_t material;
	size_t initial_vertex;
	unsigned int axis;
	int environment;
	int prelit;
	int alpha_test;

	initial_vertex = built->vertex_count;

	for (face_index = 0; face_index < model->face_count; face_index++) {
		face = model->faces[face_index];

		if (face.degenerate)
			continue;

		if (profile == MH_RENDER_SOFTWARE)
			software_face(&face);

		material = face.texture == MH_GEOMETRY_NO_TEXTURE
			? 0
			: (size_t)face.texture + 1;
		environment = (face.type == 8 || face.type == 9) &&
			(face.flags & FACE_ENVIRONMENT_FLAGS) != 0;
		prelit = brightness != NULL ||
			(face.type >= 4 && face.type <= 7);
		alpha_test = material != 0 && !environment &&
			(face.flags & FACE_ALPHA_TEST_FLAG) != 0;
		count = face.count == 3 ? 3 : 6;

		if (built->faces != NULL)
			retain_face(&face, count, built);

		draw = built->vertex_count == initial_vertex
			? NULL
			: &built->draws[built->draw_count - 1];

		if (draw == NULL || draw->texture != material ||
			draw->environment != environment ||
			draw->prelit != prelit ||
			draw->alpha_test != alpha_test) {
			draw = &built->draws[built->draw_count++];
			draw->first = built->vertex_count;
			draw->count = 0;
			draw->texture = material;
			draw->environment = environment;
			draw->prelit = prelit;
			draw->alpha_test = alpha_test;
			draw->texture_name[0] = 0;

			if (material != 0) {
				const char *name;

				name = model->textures[material - 1];

				if (strlen(name) >= sizeof(draw->texture_name))
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"material texture name is too long");

				strcpy(draw->texture_name, name);
			}
		}

		draw->count += count;

		if (built->world != NULL &&
			built->primitive_count < built->world->plane_count)
			for (axis = 0; axis < 4; axis++)
				built->world
					->planes[built->primitive_count][axis] =
					(float)face.plane[axis];

		memset(&built->primitives[built->primitive_count], 0,
			sizeof(*built->primitives));
		built->primitives[built->primitive_count].first =
			built->vertex_count;
		built->primitives[built->primitive_count].draw =
			built->draw_count - 1;
		built->primitives[built->primitive_count].type =
			(unsigned char)face.type;
		built->primitives[built->primitive_count].flags =
			(unsigned char)face.flags;
		built->primitives[built->primitive_count++].count =
			(unsigned int)count;

		append_vertices(
			&face, count, material, prelit, brightness, built);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
retain_halos(const struct mh_model *model, struct mh_mesh *built,
	struct mh_error *error_out)
{
	if (model->halo_count > (size_t)-1 / sizeof(*built->halos))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many model halos");

	built->halos = (struct mh_geometry_halo *)calloc(
		model->halo_count, sizeof(*built->halos));

	if (built->halos == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining model halos");

	built->halo_count = model->halo_count;

	memcpy(built->halos, model->halos,
		model->halo_count * sizeof(*built->halos));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_build_native_profile(const struct mh_image_decoder_provider *decoder,
	const struct mh_model *model,
	const struct mh_image_collection *collection,
	enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	struct mh_mesh built;
	enum mh_error_code status;

	if ((unsigned int)profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid mesh rendering profile");

	status = mh_mesh_init_model(decoder, model, collection,
		model->face_count, &built, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	built.radius = (float)model->radius;
	built.faces = (struct mh_mesh_face *)calloc(
		model->face_count, sizeof(*built.faces));

	if (built.faces == NULL) {
		mh_mesh_free(&built);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining model faces");
	}

	status = mh_mesh_append_model(model, NULL, profile, &built, error_out);

	if (status == MH_ERROR_NONE && model->halo_count != 0)
		status = retain_halos(model, &built, error_out);

	if (status != MH_ERROR_NONE) {
		mh_mesh_free(&built);
		return status;
	}

	*mesh = built;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_build_native(const struct mh_image_decoder_provider *decoder,
	const struct mh_model *model,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	return mh_mesh_build_native_profile(
		decoder, model, collection, MH_RENDER_GLIDE, mesh, error_out);
}

enum mh_error_code
mh_mesh_build_parts(const struct mh_image_decoder_provider *decoder,
	const struct mh_mesh_part *parts, size_t count,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	struct mh_mesh built;
	struct mh_mesh part;
	const struct mh_model *model;
	enum mh_error_code status;
	size_t faces;
	size_t textures;
	size_t part_faces, part_textures;
	size_t part_index;
	size_t i;
	unsigned int axis;
	double minimum[3];
	double maximum[3];
	double center[3];
	double radius;
	double value;

	if (count == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "empty model assembly");

	faces = 0;
	textures = 0;

	for (part_index = 0; part_index < count; part_index++) {
		model = parts[part_index].model;
		part_faces = model->face_count;

		if (part_faces > (size_t)-1 - faces)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"oversized model assembly");

		faces += part_faces;
		part_textures = model->texture_count;

		if (part_textures == (size_t)-1 ||
			part_textures + 1 > (size_t)-1 - textures) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many assembly textures");
		}

		textures += part_textures + 1;

		for (axis = 0; axis < 3; axis++) {
			value = parts[part_index].offset[axis];

			if (!(value >= -PART_OFFSET_LIMIT &&
				value <= PART_OFFSET_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid part offset");

			value += model->minimum[axis];

			if (part_index == 0 || value < minimum[axis])
				minimum[axis] = value;

			value = parts[part_index].offset[axis] +
				model->maximum[axis];

			if (part_index == 0 || value > maximum[axis])
				maximum[axis] = value;
		}
	}

	if (faces == 0 || faces > (size_t)-1 / (6 * sizeof(*built.vertices)) ||
		faces > (size_t)-1 / sizeof(*built.draws) ||
		faces > (size_t)-1 / sizeof(*built.primitives) ||
		textures > (size_t)-1 / sizeof(*built.textures)) {
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "oversized model assembly");
	}

	memset(&built, 0, sizeof(built));
	built.vertices =
		(struct mh_vertex *)malloc(faces * 6 * sizeof(*built.vertices));
	built.draws = (struct mh_draw *)malloc(faces * sizeof(*built.draws));
	built.primitives = (struct mh_mesh_primitive *)malloc(
		faces * sizeof(*built.primitives));
	built.textures =
		(struct mh_image *)calloc(textures, sizeof(*built.textures));

	if (built.vertices == NULL || built.draws == NULL ||
		built.textures == NULL || built.primitives == NULL) {
		mh_mesh_free(&built);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory assembling models");
	}

	radius = 0.0;

	for (axis = 0; axis < 3; axis++) {
		center[axis] = (minimum[axis] + maximum[axis]) * 0.5;
		value = (maximum[axis] - minimum[axis]) * 0.5;
		radius += value * value;
	}

	radius = sqrt(radius);

	if (radius == 0.0)
		radius = 1.0;

	for (part_index = 0; part_index < count; part_index++) {
		status = mh_mesh_build_native(decoder, parts[part_index].model,
			collection, &part, error_out);

		if (status != MH_ERROR_NONE) {
			mh_mesh_free(&built);
			return status;
		}

		for (i = 0; i < part.vertex_count; i++) {
			for (axis = 0; axis < 3; axis++) {
				part.vertices[i].position[axis] =
					(float)((part.vertices[i].position
								[axis] +
							parts[part_index]
								.offset[axis] -
							center[axis]) /
						radius);
			}
		}

		for (i = 0; i < part.draw_count; i++) {
			part.draws[i].first += built.vertex_count;
			part.draws[i].texture += built.texture_count;
		}

		memcpy(built.vertices + built.vertex_count, part.vertices,
			part.vertex_count * sizeof(*part.vertices));
		memcpy(built.draws + built.draw_count, part.draws,
			part.draw_count * sizeof(*part.draws));
		memcpy(built.textures + built.texture_count, part.textures,
			part.texture_count * sizeof(*part.textures));

		for (i = 0; i < part.primitive_count; i++) {
			built.primitives[built.primitive_count] =
				part.primitives[i];
			built.primitives[built.primitive_count].first +=
				built.vertex_count;
			built.primitives[built.primitive_count++].draw +=
				built.draw_count;
		}

		built.vertex_count += part.vertex_count;
		built.draw_count += part.draw_count;
		built.texture_count += part.texture_count;

		/*
		 * Image ownership moved into built; release only the
		 * containers.
		 */
		free(part.vertices);
		free(part.faces);
		free(part.primitives);
		free(part.halos);
		free(part.draws);
		free(part.textures);
	}

	*mesh = built;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_build(const struct mh_image_decoder_provider *decoder,
	const struct mh_model *model,
	const struct mh_image_collection *collection, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	struct mh_mesh_part part;

	part.model = model;
	part.offset[0] = 0.0;
	part.offset[1] = 0.0;
	part.offset[2] = 0.0;

	return mh_mesh_build_parts(
		decoder, &part, 1, collection, mesh, error_out);
}
