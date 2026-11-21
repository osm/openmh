#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/glenz.h"
#include "engine/render/light_product.h"
#include "engine/render/material_color.h"
#include "engine/render/object_light.h"

#define POSE_LIMIT 1e6f
#define COLOR_NORMALIZER 255.0f

enum {
	BRIGHTNESS_LIMIT = 200,
	SHADE_COLOR_SCALE = 256,
	COLOR_BYTE_MAX = 255,
	TEXTURED_BASE = 200,
	TEXTURED_TRIANGLE = 2,
	TEXTURED_QUAD = 3,
	PRECOLORED_FIRST = 4,
	PRECOLORED_LAST = 7,
	PRECOLORED_TEXTURED_TRIANGLE = 6,
	PRECOLORED_TEXTURED_QUAD = 7,
	EMISSIVE = 0x10,
	ENVIRONMENT = 0x60,
	SMOOTH_TRIANGLE = 8,
	SMOOTH_QUAD = 9
};

enum light_policy {
	LIGHT_GLIDE,
	LIGHT_GLENZ,
	LIGHT_MATERIAL
};

enum {
	NORMAL_CACHE_SIZE = 128
};

struct normal_light {
	float		normal[3], light[3];
	unsigned int	mode;
};

static struct normal_light *
normal_slot(struct normal_light cache[NORMAL_CACHE_SIZE], const float normal[3])
{
	const unsigned char *bytes;
	unsigned int hash;
	size_t i;

	bytes = (const unsigned char *)normal;
	hash = 2166136261U;

	for (i = 0; i < 3 * sizeof(*normal); i++)
		hash = (hash ^ bytes[i]) * 16777619U;

	/* Fold high bits so mirrored normals need not share a cache slot. */
	hash ^= hash >> 8;

	return &cache[hash % NORMAL_CACHE_SIZE];
}

static enum mh_error_code
shade_normal(struct normal_light cache[NORMAL_CACHE_SIZE],
	const struct mh_prepared_light *lights, size_t light_count,
	const float source[3], const float pose[16], const double ambient[3],
	unsigned int mode, float light[3], struct mh_error *error_out)
{
	struct normal_light *cached;
	double normal[3];
	unsigned int axis, basis;
	enum mh_error_code status;

	cached = normal_slot(cache, source);

	if (cached->mode == mode &&
		memcmp(cached->normal, source, sizeof(cached->normal)) == 0) {
		memcpy(light, cached->light, sizeof(cached->light));
		return MH_ERROR_NONE;
	}

	for (axis = 0; axis < 3; axis++) {
		normal[axis] = 0;

		for (basis = 0; basis < 3; basis++)
			normal[axis] += pose[basis * 4 + axis] * source[basis];
	}

	status = mh_light_shade_mode(
		lights, light_count, normal, ambient, mode, light, error_out);

	if (status == MH_ERROR_NONE) {
		memcpy(cached->normal, source, sizeof(cached->normal));
		memcpy(cached->light, light, sizeof(cached->light));
		cached->mode = mode;
	}

	return status;
}

/* Keep earlier channels on failure, matching partial vertex publication. */
static enum mh_error_code
shade_glide_color(const unsigned char rgb[3], const double brightness[3],
	const float light[3], int textured, float color[3],
	struct mh_error *error_out)
{
	float base, value;

	mh_light_product product;
	unsigned int channel;

	for (channel = 0; channel < 3; channel++) {
		/*
		 * Use 200 for ordinary textured faces; emissive and environment
		 * materials use the stored RGB.
		 */
		product = (mh_light_product)(float)brightness[channel] *
			(textured ? TEXTURED_BASE : rgb[channel]);

		if (!(product >= -FLT_MAX && product <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"object brightness product overflow");

		base = (float)product;
		product = (mh_light_product)base * light[channel];
		value = product < 0		   ? 0
			: product > COLOR_BYTE_MAX ? COLOR_BYTE_MAX
						: (float)product;
		color[channel] = value / COLOR_NORMALIZER;
	}

	return MH_ERROR_NONE;
}

static size_t
matching_normal(const struct mh_mesh *mesh, size_t first, size_t vertex)
{
	size_t previous;

	for (previous = first; previous < vertex; previous++)
		if (memcmp(mesh->vertices[previous].normal,
			mesh->vertices[vertex].normal,
			sizeof(mesh->vertices[vertex].normal)) == 0)
			return previous;

	return vertex;
}

static enum mh_error_code
shade_mesh(const struct mh_mesh *mesh, const struct mh_prepared_light *lights,
	size_t light_count, const double ambient[3], const double brightness[3],
	const float pose[16], unsigned int mode, enum light_policy policy,
	int reflections, struct mh_vertex *vertices, size_t capacity,
	float *material_rgb, struct mh_error *error_out)
{
	const struct mh_mesh_face *face;
	const struct mh_vertex *source;
	struct mh_vertex *vertex;
	struct normal_light normal_cache[NORMAL_CACHE_SIZE];
	size_t face_index, vertex_index, previous, end;
	unsigned int channel, axis, shift;
	float light[3], stored_brightness[3], color[3];
	enum mh_error_code status;
	int textured, emissive, glenz_face, environment, smooth;
	unsigned long packed;
	enum mh_material_kind material;

	if (mesh == NULL || pose == NULL || brightness == NULL ||
		ambient == NULL || vertices == NULL || mesh->vertices == NULL ||
		mesh->faces == NULL || capacity < mesh->vertex_count ||
		vertices == mesh->vertices ||
		(mode != MH_LIGHT_OBJECT && mode != MH_LIGHT_CAR))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid object lighting mesh");

	for (axis = 0; axis < 16; axis++)
		if (!(pose[axis] >= -POSE_LIMIT && pose[axis] <= POSE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid object lighting pose");

	for (channel = 0; channel < 3; channel++)
		if (!(brightness[channel] >=
				-(double)FLT_MAX / SHADE_COLOR_SCALE &&
			brightness[channel] <= BRIGHTNESS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid object brightness");

	/*
	 * A call has one pose and light set; cache shading, never material RGB.
	 */
	memset(normal_cache, 0, sizeof(normal_cache));
	end = 0;

	for (face_index = 0; face_index < mesh->face_count; face_index++) {
		face = &mesh->faces[face_index];

		if (face->first != end ||
			(face->count != 3 && face->count != 6) ||
			end > mesh->vertex_count ||
			face->count > mesh->vertex_count - end)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lit face span");

		end += face->count;
		textured = face->type == TEXTURED_TRIANGLE ||
			face->type == TEXTURED_QUAD ||
			(policy == LIGHT_MATERIAL &&
				(face->type == PRECOLORED_TEXTURED_TRIANGLE ||
					face->type ==
						PRECOLORED_TEXTURED_QUAD));
		emissive = (face->flags & EMISSIVE) != 0;
		environment = reflections &&
			(face->type == SMOOTH_TRIANGLE ||
				face->type == SMOOTH_QUAD) &&
			(face->flags & ENVIRONMENT) != 0;
		glenz_face = policy == LIGHT_GLENZ && !emissive && environment;
		smooth = !emissive &&
			(face->type == SMOOTH_TRIANGLE ||
				face->type == SMOOTH_QUAD);
		material = environment ? MH_MATERIAL_ENVIRONMENT
			: textured     ? MH_MATERIAL_TEXTURED
				: MH_MATERIAL_SOLID;

		for (vertex_index = face->first; vertex_index < end;
			vertex_index++) {
			source = &mesh->vertices[vertex_index];
			vertex = &vertices[vertex_index];
			*vertex = *source;

			if (policy != LIGHT_MATERIAL &&
				face->type >= PRECOLORED_FIRST &&
				face->type <= PRECOLORED_LAST)
				continue;

			/*
			 * Expanded triangles repeat corners; flat faces often
			 * repeat every normal. Within one face, identical
			 * normals share the exact lighting/color result, not
			 * just a rounded approximation. Retain each corner's
			 * other attributes.
			 */
			previous = policy == LIGHT_MATERIAL && !smooth
				? face->first
				: matching_normal(
					mesh, face->first, vertex_index);

			if (previous != vertex_index) {
				memcpy(vertex->color, vertices[previous].color,
					sizeof(source->color));

				if (material_rgb != NULL)
					memcpy(material_rgb + vertex_index * 3,
						material_rgb + previous * 3,
						3 * sizeof(float));

				continue;
			}

			for (axis = 0; axis < 3; axis++)
				light[axis] = 1;

			if (!emissive) {
				status = shade_normal(normal_cache, lights,
					light_count, source->normal, pose,
					ambient,
					glenz_face ? MH_LIGHT_CAR : mode, light,
					error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

			if (policy == LIGHT_MATERIAL) {
				for (channel = 0; channel < 3; channel++)
					stored_brightness[channel] =
						(float)brightness[channel];

				status = emissive
					? mh_emissive_color(face->color,
						stored_brightness, color,
						error_out)
					: mh_material_color(material, mode,
						face->color,
						stored_brightness, light,
						color, error_out);

				if (status == MH_ERROR_NONE)
					status = mh_material_color_pack(
						color, &packed, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				if (material_rgb != NULL)
					memcpy(material_rgb + vertex_index * 3,
						color, sizeof(color));

				for (channel = 0; channel < 3; channel++) {
					shift = 16 - channel * 8;
					vertex->color[channel] =
						(float)((packed >> shift) &
							COLOR_BYTE_MAX) /
						COLOR_NORMALIZER;
				}

				continue;
			}

			if (glenz_face) {
				for (channel = 0; channel < 3; channel++)
					stored_brightness[channel] =
						(float)brightness[channel];

				status = mh_glenz_car_color(face->color,
					stored_brightness, light, color,
					error_out);

				if (status != MH_ERROR_NONE)
					return status;

				for (channel = 0; channel < 3; channel++)
					vertex->color[channel] =
						color[channel] /
						COLOR_NORMALIZER;

				continue;
			}

			status = shade_glide_color(face->color, brightness,
				light, textured && !emissive, vertex->color,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return end == mesh->vertex_count
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete lit face spans");
}

enum mh_error_code
mh_mesh_object_light(const struct mh_mesh *mesh,
	const struct mh_prepared_light *lights, size_t light_count,
	const double ambient[3], const double brightness[3],
	const float model_to_world[16], unsigned int mode,
	struct mh_vertex *vertices, size_t capacity, struct mh_error *error_out)
{
	return shade_mesh(mesh, lights, light_count, ambient, brightness,
		model_to_world, mode, LIGHT_GLIDE, 1, vertices, capacity, NULL,
		error_out);
}

enum mh_error_code
mh_mesh_glenz_light(const struct mh_mesh *mesh,
	const struct mh_prepared_light *lights, size_t light_count,
	const double ambient[3], const double brightness[3],
	const float model_to_world[16], unsigned int mode,
	struct mh_vertex *vertices, size_t capacity, struct mh_error *error_out)
{
	return shade_mesh(mesh, lights, light_count, ambient, brightness,
		model_to_world, mode, LIGHT_GLENZ, 1, vertices, capacity, NULL,
		error_out);
}

enum mh_error_code
mh_mesh_material_light(const struct mh_mesh *mesh,
	const struct mh_prepared_light *lights, size_t light_count,
	const double ambient[3], const double brightness[3],
	const float model_to_world[16], unsigned int mode, int reflections,
	struct mh_vertex *vertices, size_t capacity, float *material_rgb,
	struct mh_error *error_out)
{
	return shade_mesh(mesh, lights, light_count, ambient, brightness,
		model_to_world, mode, LIGHT_MATERIAL, reflections, vertices,
		capacity, material_rgb, error_out);
}
