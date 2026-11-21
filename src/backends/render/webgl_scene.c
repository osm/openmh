#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_scene.h"
#include "engine/render/submission.h"

enum mh_error_code
mh_webgl_scene_open(const struct mh_mesh *const *meshes, size_t count,
	int powervr, struct mh_webgl_scene **output, struct mh_error *error_out)
{
	struct mh_webgl_scene *scene;
	enum mh_error_code status;
	size_t i;

	if (output == NULL || (count != 0 && meshes == NULL) ||
		count > (size_t)-1 / sizeof(*scene->meshes))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL scene inputs");

	scene = calloc(1, sizeof(*scene));

	if (scene == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL scene");

	scene->meshes = calloc(count ? count : 1, sizeof(*scene->meshes));

	if (scene->meshes == NULL) {
		free(scene);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebGL scene meshes");
	}

	scene->mesh_count = count;
	status = mh_webgl_images_create(
		&scene->images, meshes, count, powervr, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < count; i++)
		status = mh_webgl_mesh_create(&scene->meshes[i], meshes[i],
			&scene->images, i, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_scene_ranges(scene, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_scene_close(scene);
		return status;
	}

	*output = scene;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_webgl_scene_release(struct mh_webgl_scene *scene)
{
	size_t i;

	for (i = 0; i < scene->mesh_count; i++)
		mh_webgl_geometry_destroy(&scene->meshes[i].geometry);

	mh_webgl_images_release(&scene->images);
}

enum mh_error_code
mh_webgl_scene_restore(struct mh_webgl_scene *scene, struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t i;

	status = mh_webgl_images_upload(&scene->images, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < scene->mesh_count; i++)
		status = mh_webgl_mesh_restore(&scene->meshes[i], error_out);

	return status;
}

void
mh_webgl_scene_close(struct mh_webgl_scene *scene)
{
	size_t i;

	if (scene == NULL)
		return;

	for (i = 0; i < scene->mesh_count; i++)
		mh_webgl_mesh_destroy(&scene->meshes[i]);

	mh_webgl_images_destroy(&scene->images);
	free(scene->meshes);
	free(scene->parts);
	free(scene);
}

enum mh_error_code
mh_webgl_scene_order(struct mh_webgl_scene *scene,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out)
{
	struct mh_renderer_scene_part *copy;
	enum mh_error_code status;
	size_t i;

	if (scene == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing WebGL scene");

	status = mh_scene_submission_check(
		parts, count, scene->mesh_count, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < scene->mesh_count; i++)
		status = mh_scene_submission_mesh_check(
			scene->meshes[i].source, i, parts, count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	copy = NULL;

	if (count != 0) {
		copy = malloc(count * sizeof(*copy));

		if (copy == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot retain WebGL scene order");

		memcpy(copy, parts, count * sizeof(*copy));
	}

	free(scene->parts);
	scene->parts = copy;
	scene->part_count = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_scene_ranges(struct mh_webgl_scene *scene, struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t i;

	for (i = 0; i < scene->mesh_count; i++) {
		if (scene->meshes[i].source->vertex_count !=
				scene->meshes[i].geometry.count ||
			(scene->meshes[i].source->draw_count != 0 &&
				scene->meshes[i].source->draws == NULL) ||
			(scene->meshes[i].source->primitive_count != 0 &&
				scene->meshes[i].source->primitives == NULL))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid live WebGL scene ranges");

		status = mh_mesh_active_lod(scene->meshes[i].source,
			&scene->meshes[i].range, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < scene->part_count; i++) {
		if (scene->parts[i].active_lod)
			continue;

		status = mh_scene_span_check(
			scene->meshes[scene->parts[i].mesh].source,
			&scene->parts[i].span, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

size_t
mh_webgl_scene_part_count(const struct mh_webgl_scene *scene)
{
	return scene->parts == NULL ? scene->mesh_count : scene->part_count;
}

size_t
mh_webgl_scene_part(const struct mh_webgl_scene *scene, size_t index,
	struct mh_mesh_span *span)
{
	const struct mh_mesh_lod *range;
	size_t model;

	model = scene->parts == NULL ? index : scene->parts[index].mesh;
	range = &scene->meshes[model].range;

	if (scene->parts != NULL && !scene->parts[index].active_lod)
		*span = scene->parts[index].span;
	else {
		span->draw_first = range->draw_first;
		span->draw_count = range->draw_count;
		span->primitive_first = range->primitive_first;
		span->primitive_count = range->primitive_count;
	}

	return model;
}
