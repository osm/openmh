#include <stdlib.h>

#include "engine/image/load.h"
#include "engine/render/scene_glenz.h"
#include "engine/render/scene_internal.h"

#define NO_ATLAS_PAGE ((size_t)-1)
#define PROJECTION_W_MIN 0.000001f
#define SCREEN_LIMIT 1000000.0f

/* Append private maps without disturbing existing atlas slots or UVs. */
static enum mh_error_code
append_map(const struct mh_image_decoder_provider *decoder,
	struct mh_mesh *mesh, const struct mh_file *file,
	enum mh_texture_format format, size_t *index,
	struct mh_error *error_out)
{
	struct mh_image image, *textures;
	size_t *pages;
	enum mh_error_code status;

	if (mesh->texture_count >= (size_t)-1 / sizeof(*textures) - 1)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "too many Glenz textures");

	status = mh_image_decode(
		decoder, file->data, file->size, NULL, &image, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_image_texture_format(&image, format, error_out);

	if (status != MH_ERROR_NONE) {
		free(image.rgba);
		return status;
	}

	textures = (struct mh_image *)realloc(
		mesh->textures, (mesh->texture_count + 1) * sizeof(*textures));

	if (textures == NULL) {
		free(image.rgba);
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "cannot add Glenz texture");
	}

	mesh->textures = textures;

	if (mesh->atlas != NULL) {
		pages = (size_t *)realloc(mesh->atlas_images,
			(mesh->texture_count + 1) * sizeof(*pages));

		if (pages == NULL) {
			free(image.rgba);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot extend Glenz image bindings");
		}

		mesh->atlas_images = pages;
		pages[mesh->texture_count] = NO_ATLAS_PAGE;
	}

	*index = mesh->texture_count;
	mesh->textures[mesh->texture_count++] = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_load_glenz(const struct mh_resource_source *resources,
	const char *root, struct mh_race_scene *scene,
	struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_mesh *mesh;
	struct mh_scene_glenz *glenz;
	enum mh_error_code status;
	unsigned int map;
	size_t i;

	if (scene == NULL ||
		(scene->assets.profile != MH_RENDER_GLIDE &&
			scene->assets.profile != MH_RENDER_DIRECT3D))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Glenz requires GPU scene assets");

	glenz = &scene->visuals.glenz;

	for (map = 0; map < 2; map++) {
		status = mh_image_read_from(scene->image_decoder, resources,
			root, scene->assets.track.environment[map + 1], &file,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 1; i < MH_FIELD_MESHES && status == MH_ERROR_NONE;
			i++) {
			mesh = &scene->assets.meshes[i];

			if (mesh->vertex_count == 0 ||
				glenz->textures[i][map] != 0)
				continue;

			status = append_map(scene->image_decoder, mesh, &file,
				scene->assets.opaque_format,
				&glenz->textures[i][map], error_out);
		}

		free(file.data);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_glenz_scroll(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models, int shadows,
	struct mh_error *error_out)
{
	struct mh_scene_glenz *glenz;
	const float *model;
	enum mh_error_code status;
	unsigned int car, mask;

	glenz = &scene->visuals.glenz;

	for (car = 0; car < field->count; car++) {
		mask = 1U << car;

		if (!(field->active_mask & mask))
			continue;

		model = models + (1 + MH_CAR_MESHES * car) * 16;

		if (!shadows || (field->unavailable_mask & mask))
			continue;

		status = mh_glenz_scroll_update(
			&glenz->scroll[car], model + 12, model + 8, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
transform_vector(const float matrix[16], const float input[3], float output[3])
{
	unsigned int i;

	for (i = 0; i < 3; i++)
		output[i] = matrix[i] * input[0] + matrix[i + 4] * input[1] +
			matrix[i + 8] * input[2];
}

static enum mh_error_code
prepare_corner(const struct mh_vertex *vertex, const float model[16],
	const float matrix[16], const float view[16], const float local_eye[3],
	const float center[2], float phase,
	struct mh_glenz_coordinates *coordinates, struct mh_error *error_out)
{
	float position[3], normal[3], normal_view[3], clip[4], screen[2], w;
	enum mh_error_code status;
	unsigned int i;

	status = mh_glenz_reflection_uv(vertex->position, vertex->normal,
		local_eye, phase, coordinates->reflection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	transform_vector(model, vertex->position, position);
	transform_vector(model, vertex->normal, normal);
	transform_vector(view, normal, normal_view);

	for (i = 0; i < 3; i++)
		position[i] += model[12 + i];

	for (i = 0; i < 4; i++)
		clip[i] = matrix[i] * position[0] +
			matrix[i + 4] * position[1] +
			matrix[i + 8] * position[2] + matrix[i + 12];

	/*
	 * Bound the perspective divide for corners projected before clipping.
	 */
	w = clip[3] > PROJECTION_W_MIN ? clip[3] : PROJECTION_W_MIN;
	screen[0] = center[0] * (1 + clip[0] / w);
	screen[1] = center[1] * (1 - clip[1] / w);

	for (i = 0; i < 2; i++) {
		if (screen[i] > SCREEN_LIMIT)
			screen[i] = SCREEN_LIMIT;
		else if (screen[i] < -SCREEN_LIMIT)
			screen[i] = -SCREEN_LIMIT;
	}

	return mh_glenz_phong_uv(normal_view, screen, center, 1 / center[0],
		coordinates->phong, error_out);
}

enum mh_error_code
mh_race_scene_glenz_material(struct mh_race_scene *scene, size_t index,
	const float *models, const float matrix[16], const float view[16],
	const double eye[3], unsigned int width, unsigned int height,
	struct mh_renderer_glenz *material, struct mh_error *error_out)
{
	struct mh_scene_glenz *glenz;
	const struct mh_mesh *mesh;
	const struct mh_draw *draw;
	const float *model;
	float local_eye[3], center[2], phase, root_phase;
	size_t batch, corner;
	unsigned int axis;
	enum mh_error_code status;

	if (scene == NULL || material == NULL || index == 0 ||
		index >= MH_FIELD_MESHES || width == 0 || height == 0 ||
		models == NULL || matrix == NULL || view == NULL || eye == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Glenz scene material");

	glenz = &scene->visuals.glenz;
	mesh = &scene->assets.meshes[index];

	if (mesh->vertex_count == 0 ||
		mesh->vertex_count >
			(size_t)-1 / sizeof(*glenz->coordinates[index]) ||
		glenz->textures[index][0] == 0 ||
		glenz->textures[index][1] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing Glenz scene maps");

	if (glenz->coordinates[index] == NULL)
		glenz->coordinates[index] =
			(struct mh_glenz_coordinates *)calloc(
				mesh->vertex_count,
				sizeof(*glenz->coordinates[index]));

	if (glenz->coordinates[index] == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate Glenz coordinates");

	model = models + index * 16;

	/*
	 * Body and wheel transforms are rigid. The transpose is their inverse.
	 */
	for (axis = 0; axis < 3; axis++)
		local_eye[axis] =
			(float)((eye[0] - model[12]) * model[axis * 4] +
				(eye[1] - model[13]) * model[axis * 4 + 1] +
				(eye[2] - model[14]) * model[axis * 4 + 2]);

	center[0] = width * .5f;
	center[1] = height * .5f;
	root_phase = (index - 1) % MH_CAR_MESHES == 0
		? glenz->scroll[(index - 1) / MH_CAR_MESHES].phase
		: 0;

	for (batch = 0; batch < mesh->draw_count; batch++) {
		draw = &mesh->draws[batch];

		if (!draw->environment)
			continue;

		/*
		 * Copy the root transform, not its scroll history. Alternate
		 * body objects and wheel variants stay at phase zero.
		 */
		phase = root_phase;

		if (mesh->lod_count != 0 &&
			(batch < mesh->lods[0].draw_first ||
				batch - mesh->lods[0].draw_first >=
					mesh->lods[0].draw_count))
			phase = 0;

		for (corner = draw->first; corner < draw->first + draw->count;
			corner++) {
			status = prepare_corner(&mesh->vertices[corner], model,
				matrix, view, local_eye, center, phase,
				&glenz->coordinates[index][corner], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	material->coordinates = glenz->coordinates[index];
	material->count = mesh->vertex_count;
	material->reflection_texture = glenz->textures[index][0];
	material->phong_texture = glenz->textures[index][1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
