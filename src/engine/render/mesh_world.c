#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/object_body.h"
#include "engine/core/error.h"
#include "engine/geometry/model.h"
#include "engine/render/mesh_internal.h"

#define MOTION_KEY_LIMIT 100000
#define BUILD_BRIGHTNESS_LIMIT 100.0

/* A triangulated quad retains six RGB vertex colors. */
enum {
	MAX_FACE_COLOR_BYTES = 18
};

static enum mh_error_code
retain_motion(struct mh_mesh *built, const struct mh_mesh *part,
	const struct mh_model *model, const struct mh_scene_object *object,
	size_t scene_index, struct mh_error *error_out)
{
	struct mh_mesh_binding *binding;
	struct mh_object_body body;
	float radius;
	enum mh_error_code status;

	/*
	 * Static objects also retain local normals/materials for point lights.
	 */
	if ((object->key_count != 0 && object->keys == NULL) ||
		object->key_count > MOTION_KEY_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid scenery motion");

	radius = part->radius;

	if (mh_object_has_body(object)) {
		/*
		 * Overwrite the visual root radius before copying it to the
		 * physics body. UnseenByRays bypasses that finalization.
		 */
		status = mh_object_body_model(object, model, &body, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		radius = (float)body.radius;
	}

	if (built->scenery == NULL) {
		built->scenery = (struct mh_mesh_scenery *)calloc(
			1, sizeof(*built->scenery));

		if (built->scenery == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory retaining scenery motion");
	}

	binding = (struct mh_mesh_binding *)calloc(1, sizeof(*binding));

	if (binding == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining scenery motion");

	binding->source = binding->pose = *object;
	binding->face_count = part->face_count;
	binding->faces = (struct mh_mesh_face *)malloc(
		part->face_count * sizeof(*part->faces));
	binding->source.keys = NULL;

	if (object->key_count != 0)
		binding->source.keys = (struct mh_scene_key *)malloc(
			object->key_count * sizeof(*object->keys));

	binding->scene_index = scene_index;
	binding->vertex_first = built->vertex_count;
	binding->draw_first = built->draw_count;
	binding->draw_count = part->draw_count;
	binding->primitive_first = built->primitive_count;
	binding->primitive_count = part->primitive_count;
	binding->vertex_count = part->vertex_count;
	binding->halo_count = part->halo_count;
	binding->radius = radius;

	if (part->halo_count != 0)
		binding->halos = (struct mh_geometry_halo *)malloc(
			part->halo_count * sizeof(*part->halos));

	binding->local_vertices = (struct mh_vertex *)malloc(
		binding->vertex_count * sizeof(*binding->local_vertices));
	binding->staged_vertices = (struct mh_vertex *)malloc(
		binding->vertex_count * sizeof(*binding->staged_vertices));
	binding->next = built->scenery->bindings;
	built->scenery->bindings = binding;

	if ((object->key_count != 0 && binding->source.keys == NULL) ||
		binding->local_vertices == NULL ||
		binding->staged_vertices == NULL || binding->faces == NULL ||
		(part->halo_count != 0 && binding->halos == NULL)) {
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining scenery motion");
	}

	if (object->key_count != 0)
		memcpy(binding->source.keys, object->keys,
			object->key_count * sizeof(*object->keys));

	memcpy(binding->faces, part->faces,
		part->face_count * sizeof(*part->faces));
	memcpy(binding->local_vertices, part->vertices,
		binding->vertex_count * sizeof(*binding->local_vertices));

	if (part->halo_count != 0)
		memcpy(binding->halos, part->halos,
			part->halo_count * sizeof(*part->halos));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_scenery(const struct mh_image_decoder_provider *decoder,
	const struct mh_scene_instance *instances, size_t count,
	const struct mh_image_collection *collection,
	enum mh_render_profile profile, struct mh_mesh *built,
	struct mh_error *error_out)
{
	struct mh_mesh part;
	struct mh_image *images;
	const struct mh_scene_object *pose;
	enum mh_error_code status;
	size_t first_instance;
	size_t instance_index;
	size_t i;
	size_t image_base;

	for (first_instance = 0; first_instance < count; first_instance++) {
		for (instance_index = 0; instance_index < first_instance;
			instance_index++) {
			if (instances[instance_index].model ==
				instances[first_instance].model)
				break;
		}

		if (instance_index != first_instance)
			continue;

		status = mh_mesh_build_native_profile(decoder,
			instances[first_instance].model, collection, profile,
			&part, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		image_base = built->texture_count;

		if (part.texture_count >
			(size_t)-1 / sizeof(*images) - image_base) {
			mh_mesh_free(&part);
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many scenery textures");
		}

		images = (struct mh_image *)realloc(built->textures,
			(image_base + part.texture_count) * sizeof(*images));

		if (images == NULL) {
			mh_mesh_free(&part);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory adding scenery textures");
		}

		built->textures = images;
		memcpy(images + image_base, part.textures,
			part.texture_count * sizeof(*images));
		built->texture_count += part.texture_count;

		/*
		 * The destination now owns the images, including on later
		 * failure.
		 */
		part.texture_count = 0;

		for (instance_index = first_instance; instance_index < count;
			instance_index++) {
			if (instances[instance_index].model !=
				instances[first_instance].model)
				continue;

			pose = instances[instance_index].pose;
			status = retain_motion(built, &part,
				instances[first_instance].model, pose,
				instance_index, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_mesh_transform_vertices(
					part.vertices,
					built->vertices + built->vertex_count,
					part.vertex_count, pose, NULL,
					error_out);

			if (status != MH_ERROR_NONE) {
				mh_mesh_free(&part);
				return status;
			}

			for (i = 0; i < part.primitive_count; i++) {
				built->primitives[built->primitive_count] =
					part.primitives[i];
				built->primitives[built->primitive_count]
					.first += built->vertex_count;
				built->primitives[built->primitive_count++]
					.draw += built->draw_count;
			}

			for (i = 0; i < part.draw_count; i++) {
				built->draws[built->draw_count] = part.draws[i];
				built->draws[built->draw_count].first +=
					built->vertex_count;
				built->draws[built->draw_count++].texture +=
					image_base;
			}

			built->vertex_count += part.vertex_count;
		}

		mh_mesh_free(&part);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
retain_world_grid(struct mh_mesh *mesh, const struct mh_world *world,
	struct mh_error *error_out)
{
	size_t i, next_model;
	size_t model;

	mesh->world = (struct mh_mesh_world *)calloc(1, sizeof(*mesh->world));

	if (mesh->world == NULL) {
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining world grid");
	}

	mesh->world->model_count = (size_t)world->model_count;
	mesh->world->cell_count = world->cell_count;
	mesh->world->width = world->width;
	mesh->world->height = world->height;
	mesh->world->plane_count = (size_t)world->face_count;
	mesh->world->ordered = 1;

	if (mesh->world->model_count >
			(size_t)-1 / sizeof(*mesh->world->models) ||
		mesh->world->cell_count >
			(size_t)-1 / sizeof(*mesh->world->cells) ||
		mesh->world->plane_count >
			(size_t)-1 / sizeof(*mesh->world->planes)) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"oversized retained world grid");
	}

	mesh->world->models = (struct mh_mesh_world_model *)calloc(
		mesh->world->model_count, sizeof(*mesh->world->models));
	mesh->world->cells = (struct mh_world_cell *)calloc(
		mesh->world->cell_count, sizeof(*mesh->world->cells));
	mesh->world->planes = (float (*)[4])calloc(
		mesh->world->plane_count, sizeof(*mesh->world->planes));

	if (mesh->world->models == NULL || mesh->world->cells == NULL ||
		mesh->world->planes == NULL) {
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining world grid");
	}

	next_model = 0;

	for (i = 0; i < world->cell_count; i++) {
		mesh->world->cells[i] = world->cells[i];

		model = mesh->world->cells[i].model;

		if (model == MH_WORLD_NO_MODEL)
			continue;

		if (model < next_model)
			mesh->world->ordered = 0;

		next_model = (size_t)model + 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_world_models(const struct mh_world *world, const double brightness[3],
	enum mh_render_profile profile, struct mh_mesh *built,
	struct mh_error *error_out)
{
	const struct mh_world_model *model;
	struct mh_mesh_world_model *retained;
	size_t index;
	enum mh_error_code status;

	for (index = 0; index < world->model_count; index++) {
		model = &world->models[index];
		retained = &built->world->models[index];
		retained->draw_first = built->draw_count;
		retained->primitive_first = built->primitive_count;
		memcpy(retained->center, model->center,
			sizeof(retained->center));
		retained->radius = model->radius;
		status = mh_mesh_append_model(&model->geometry, brightness,
			profile, built, error_out);
		retained->draw_count = built->draw_count - retained->draw_first;

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
build_world_scene(const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], const struct mh_scene_instance *instances,
	size_t count, int fitted, enum mh_render_profile profile,
	struct mh_mesh *mesh, struct mh_error *error_out)
{
	struct mh_mesh built;
	const struct mh_model *scenery_model;
	enum mh_error_code status;
	unsigned int axis;
	size_t i;
	size_t faces;
	size_t instance_faces;
	size_t rotation_index;
	double center[3];
	double radius;
	double value;

	if ((unsigned int)profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid mesh rendering profile");

	faces = (size_t)world->face_count;

	for (i = 0; i < count; i++) {
		scenery_model = instances[i].model;
		instance_faces = scenery_model->face_count;

		if (instance_faces > (size_t)-1 - faces)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many scenery faces");

		faces += instance_faces;

		for (axis = 0; axis < 3; axis++) {
			value = instances[i].pose->position[axis];

			if (!(value >= -MH_MESH_POSITION_LIMIT &&
				value <= MH_MESH_POSITION_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid scenery position");

			value = instances[i].pose->color[axis];

			if (!(value >= 0 && value <= MH_MESH_TINT_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid scenery color");
		}

		for (rotation_index = 0; rotation_index < 9; rotation_index++) {
			value = instances[i].pose->rotation[rotation_index];

			if (!(value >= -MH_MESH_ROTATION_LIMIT &&
				value <= MH_MESH_ROTATION_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid scenery rotation");
		}
	}

	radius = 0.0;

	for (axis = 0; axis < 3; axis++) {
		if (!(brightness[axis] >= 0.0 &&
			brightness[axis] <= BUILD_BRIGHTNESS_LIMIT)) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid world brightness");
		}

		center[axis] =
			(world->minimum[axis] + world->maximum[axis]) * 0.5;
		value = (world->maximum[axis] - world->minimum[axis]) * 0.5;
		radius += value * value;
	}

	radius = sqrt(radius);

	if (radius == 0.0)
		radius = 1.0;

	if (!fitted) {
		memset(center, 0, sizeof(center));
		radius = 1;
	}

	if (world->model_count == 0)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "world has no models");

	status = mh_mesh_init_model(decoder, &world->models[0].geometry,
		collection, faces, &built, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = retain_world_grid(&built, world, error_out);

	if (status != MH_ERROR_NONE) {
		mh_mesh_free(&built);
		return status;
	}

	built.world->native = !fitted;

	/* mh_mesh_init_model has already bounded faces*6*sizeof(mh_vertex). */
	built.world_colors =
		(unsigned char *)malloc(faces * MAX_FACE_COLOR_BYTES);

	if (built.world_colors == NULL) {
		mh_mesh_free(&built);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining world colors");
	}

	status = append_world_models(
		world, brightness, profile, &built, error_out);

	if (status != MH_ERROR_NONE) {
		mh_mesh_free(&built);
		return status;
	}

	built.world_vertex_count = built.vertex_count;

	for (axis = 0; axis < 3; axis++)
		built.world_brightness[axis] = (float)brightness[axis];

	built.world->span.draw_count = built.draw_count;
	built.world->span.primitive_count = built.primitive_count;
	built.world->scene_count = count;
	status = append_scenery(decoder, instances, count, collection, profile,
		&built, error_out);

	if (status != MH_ERROR_NONE) {
		mh_mesh_free(&built);
		return status;
	}

	if (built.scenery != NULL) {
		memcpy(built.scenery->fit_center, center, sizeof(center));
		built.scenery->fit_radius = radius;
	}

	built.draw_hidden = (unsigned char *)calloc(built.draw_count, 1);
	built.primitive_hidden =
		(unsigned char *)calloc(built.primitive_count, 1);

	if (built.draw_hidden == NULL || built.primitive_hidden == NULL) {
		mh_mesh_free(&built);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory retaining world visibility");
	}

	for (i = 0; i < built.vertex_count; i++) {
		for (axis = 0; axis < 3; axis++) {
			built.vertices[i].position[axis] =
				(float)((built.vertices[i].position[axis] -
						center[axis]) /
					radius);
		}
	}

	*mesh = built;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mesh_build_world_scene(const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], const struct mh_scene_instance *instances,
	size_t count, struct mh_mesh *mesh, struct mh_error *error_out)
{
	return build_world_scene(decoder, world, collection, brightness,
		instances, count, 1, MH_RENDER_GLIDE, mesh, error_out);
}

enum mh_error_code
mh_mesh_build_world_native(const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], const struct mh_scene_instance *instances,
	size_t count, struct mh_mesh *mesh, struct mh_error *error_out)
{
	return build_world_scene(decoder, world, collection, brightness,
		instances, count, 0, MH_RENDER_GLIDE, mesh, error_out);
}

enum mh_error_code
mh_mesh_build_world_native_profile(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], const struct mh_scene_instance *instances,
	size_t count, enum mh_render_profile profile, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	return build_world_scene(decoder, world, collection, brightness,
		instances, count, 0, profile, mesh, error_out);
}

enum mh_error_code
mh_mesh_build_world(const struct mh_image_decoder_provider *decoder,
	const struct mh_world *world,
	const struct mh_image_collection *collection,
	const double brightness[3], struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	return mh_mesh_build_world_scene(decoder, world, collection, brightness,
		NULL, 0, mesh, error_out);
}
