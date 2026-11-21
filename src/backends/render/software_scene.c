#include <stdlib.h>
#include <string.h>

#include "backends/render/software_renderer_internal.h"
#include "engine/render/submission.h"

/* Per-entry reserve for skids, particles and names; halos are separate. */
#define EFFECT_TRIANGLES_MAX 4

/* Frame-local assembly cursors; published texture state stays unchanged. */
struct scene_build {
	struct mh_software_face_view	view;
	struct mh_software_fog		fog;
	struct mh_software_order	root_order, child_order, shadow_order;
	size_t	triangle_count, span_count, halo_cursor, name_cursor;
};

static int
default_part(const struct mh_software_renderer *software, size_t part,
	struct mh_renderer_scene_part *output)
{
	const struct mh_mesh *mesh;
	size_t i, part_count;

	memset(output, 0, sizeof(*output));

	for (i = 0; i < software->mesh_count; i++) {
		mesh = software->meshes[i].source;
		part_count = mesh->scenery == NULL
			? 1
			: mh_mesh_scene_span_count(mesh);

		if (part >= part_count) {
			part -= part_count;
			continue;
		}

		output->mesh = i;
		output->active_lod = mesh->scenery == NULL;
		output->model_role = mesh->world != NULL && part == 0
			? MH_RENDER_MODEL_WORLD
			: MH_RENDER_MODEL_ROOT;

		return output->active_lod ||
			mh_mesh_scene_span(mesh, part, &output->span);
	}

	return 0;
}

static int
identity_model(const float *model)
{
	unsigned int i;

	if (model == NULL)
		return 1;

	for (i = 0; i < 16; i++)
		if (model[i] != (i % 5 == 0 ? 1 : 0))
			return 0;

	return 1;
}

enum mh_error_code
mh_software_scene_reserve_effects(
	struct mh_software_renderer *software, struct mh_error *error_out)
{
	struct mh_software_draw_triangle *triangles;
	struct mh_software_order_span *spans;
	size_t triangle_count, span_count, effect_count;

	effect_count = software->skids.count + software->particles.count +
		software->names.count;

	if (effect_count > ((size_t)-1 - software->mesh_triangle_capacity) /
				EFFECT_TRIANGLES_MAX ||
		effect_count > (size_t)-1 - software->mesh_span_capacity)
		goto overflow;

	triangle_count = software->mesh_triangle_capacity +
		effect_count * EFFECT_TRIANGLES_MAX;
	span_count = software->mesh_span_capacity + effect_count;

	/* One marker plus two triangles per deferred glow/lens quad. */
	if (software->halos.count > ((size_t)-1 - triangle_count) /
				(1 + 2 * MH_SOFTWARE_HALO_FACES) ||
		software->halos.count > (size_t)-1 - span_count)
		goto overflow;

	triangle_count +=
		software->halos.count * (1 + 2 * MH_SOFTWARE_HALO_FACES);
	span_count += software->halos.count;

	if (triangle_count > (size_t)-1 / sizeof(*triangles) ||
		span_count > (size_t)-1 / sizeof(*spans))
		goto overflow;

	/*
	 * Grow before preparation: triangle texture pointers refer into
	 * scratch.
	 */
	if (triangle_count > software->triangle_capacity) {
		triangles = realloc(software->triangles,
			triangle_count * sizeof(*triangles));

		if (triangles == NULL)
			goto memory;

		software->triangles = triangles;
		software->triangle_capacity = triangle_count;
	}

	if (span_count > software->span_capacity) {
		spans = realloc(software->spans, span_count * sizeof(*spans));

		if (spans == NULL)
			goto memory;

		software->spans = spans;
		software->span_capacity = span_count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

overflow:
	return mh_error_set(error_out, MH_ERROR_RANGE,
		"software effect scene capacity overflow");

memory:

	return mh_error_set(error_out, MH_ERROR_MEMORY,
		"out of memory preparing effect scene");
}

static int
append_skids(struct mh_software_renderer *software, size_t *cursor,
	size_t *span_count)
{
	const struct mh_software_skid_face *face;
	struct mh_software_draw_triangle *triangle;
	struct mh_software_order_span *span;
	size_t i;
	unsigned int j;

	for (i = 0; i < software->skids.count; i++) {
		face = software->skids.faces + i;

		if (*cursor > software->triangle_capacity ||
			face->count > software->triangle_capacity - *cursor ||
			*span_count >= software->span_capacity)
			return 0;

		span = &software->spans[(*span_count)++];
		span->first = *cursor;
		span->count = face->count;
		span->bucket = face->bucket;

		for (j = 0; j < face->count; j++) {
			triangle = &software->triangles[(*cursor)++];
			memset(triangle, 0, sizeof(*triangle));
			triangle->raster = face->triangles[j];
		}
	}

	return 1;
}

static int
append_particles(struct mh_software_renderer *software, size_t *cursor,
	size_t *span_count)
{
	const struct mh_software_particle_face *particle;
	struct mh_software_triangle prepared[2];
	struct mh_software_material material;
	struct mh_software_texture texture;
	struct mh_software_draw_triangle *triangle;
	struct mh_software_order_span *span;
	size_t i;
	unsigned int count, j;

	memset(&texture, 0, sizeof(texture));
	memset(&material, 0, sizeof(material));
	material.texture = &texture;
	material.mapping = MH_TEXTURE_AFFINE;
	material.perspective_shift = 2;

	for (i = 0; i < software->particles.count; i++) {
		particle = &software->particles.faces[i];

		if (*cursor > software->triangle_capacity ||
			software->triangle_capacity - *cursor < 2 ||
			*span_count >= software->span_capacity ||
			mh_software_face_prepare(&particle->face, &material,
				prepared, &count, NULL) != MH_ERROR_NONE)
			return 0;

		span = &software->spans[(*span_count)++];
		span->first = *cursor;
		span->count = count;
		span->bucket = particle->bucket;

		for (j = 0; j < count; j++) {
			triangle = &software->triangles[(*cursor)++];
			memset(triangle, 0, sizeof(*triangle));
			triangle->raster = prepared[j];
			triangle->image = particle->image;

			/*
			 * Bind to stable per-triangle storage before returning.
			 */
			triangle->raster.texture = &triangle->texture;
		}
	}

	return 1;
}

static int
append_halo(struct mh_software_renderer *software, size_t index, size_t *cursor,
	size_t *span_count)
{
	struct mh_software_halo_entry *entry;
	struct mh_software_order_span *span;
	struct mh_software_draw_triangle *triangle;
	struct mh_software_triangle prepared[2];
	struct mh_software_material material;
	struct mh_software_texture texture;
	unsigned int i, j, count;

	entry = &software->halos.entries[index];

	if (entry->geometry.count == 0)
		return 1;

	if (*cursor > software->triangle_capacity ||
		1 + 2 * entry->geometry.count >
			software->triangle_capacity - *cursor ||
		*span_count >= software->span_capacity)
		return 0;

	span = &software->spans[(*span_count)++];
	span->first = *cursor;
	span->count = 1;
	span->bucket = entry->geometry.bucket;
	triangle = &software->triangles[(*cursor)++];
	memset(triangle, 0, sizeof(*triangle));
	triangle->halo = index + 1;
	entry->first = *cursor;
	memset(&texture, 0, sizeof(texture));
	memset(&material, 0, sizeof(material));
	material.texture = &texture;
	material.mapping = MH_TEXTURE_AFFINE;
	material.perspective_shift = 2;

	for (i = 0; i < entry->geometry.count; i++) {
		if (mh_software_face_prepare(&entry->geometry.faces[i],
			&material, prepared, &count, NULL) != MH_ERROR_NONE)
			return 0;

		for (j = 0; j < count; j++) {
			triangle = &software->triangles[(*cursor)++];
			memset(triangle, 0, sizeof(*triangle));
			triangle->raster = prepared[j];
			triangle->image = entry->geometry.images[i];
			triangle->raster.texture = &triangle->texture;
		}
	}

	entry->count = *cursor - entry->first;

	return 1;
}

static int
append_names_at(struct mh_software_renderer *software, size_t part,
	size_t *name_cursor, size_t *cursor, size_t *span_count)
{
	const struct mh_software_name_entry *entry;
	struct mh_software_order_span *span;
	struct mh_software_draw_triangle *triangle;
	struct mh_software_triangle prepared[2];
	struct mh_software_material material;
	struct mh_software_texture texture;
	unsigned int count, j;

	while (*name_cursor < software->names.count) {
		entry = &software->names.entries[*name_cursor];

		if (entry->part != part)
			break;

		(*name_cursor)++;

		if (entry->geometry.face.count == 0)
			continue;

		memset(&texture, 0, sizeof(texture));
		texture.zero_transparent =
			(entry->geometry.face.flags &
				MH_SOFTWARE_FACE_CUTOUT) != 0;
		memset(&material, 0, sizeof(material));
		material.texture = &texture;
		material.mapping = MH_TEXTURE_AFFINE;
		material.perspective_shift = 2;

		if (*cursor > software->triangle_capacity ||
			software->triangle_capacity - *cursor < 2 ||
			*span_count >= software->span_capacity ||
			mh_software_face_prepare(&entry->geometry.face,
				&material, prepared, &count,
				NULL) != MH_ERROR_NONE)
			return 0;

		span = &software->spans[(*span_count)++];
		span->first = *cursor;
		span->count = count;
		span->bucket = entry->geometry.bucket;

		for (j = 0; j < count; j++) {
			triangle = &software->triangles[(*cursor)++];
			memset(triangle, 0, sizeof(*triangle));
			triangle->raster = prepared[j];
			triangle->image = entry->image;
			triangle->texture.zero_transparent =
				texture.zero_transparent;
			triangle->raster.texture = &triangle->texture;
		}
	}

	return 1;
}

static int
append_halos_at(struct mh_software_renderer *software, size_t part,
	size_t primitive, size_t *halo_cursor, size_t *cursor,
	size_t *span_count)
{
	const struct mh_software_halo_entry *entry;

	while (*halo_cursor < software->halos.count) {
		entry = &software->halos.entries[*halo_cursor];

		if (entry->part != part || entry->before_primitive != primitive)
			break;

		if (!append_halo(software, *halo_cursor, cursor, span_count))
			return 0;

		(*halo_cursor)++;
	}

	return 1;
}

static int
prepare_scene_view(struct mh_software_renderer *software,
	const float matrix[16], const struct mh_projection *projection,
	const struct mh_depth_cue *cue, struct scene_build *scene,
	size_t *part_count)
{
	struct mh_software_mesh *mesh;
	const struct mh_mesh *source;
	size_t i, mesh_parts;

	memset(scene, 0, sizeof(*scene));
	scene->view.matrix = matrix;
	scene->view.projection = projection;
	*part_count = software->part_count;

	if (software->parts == NULL)
		for (i = 0; i < software->mesh_count; i++) {
			source = software->meshes[i].source;
			mesh_parts = source->scenery == NULL
				? 1
				: mh_mesh_scene_span_count(source);

			if (mesh_parts > (size_t)-1 - *part_count)
				return 0;

			*part_count += mesh_parts;
		}

	if (cue != NULL) {
		if (projection == NULL ||
			mh_software_fog_setup(projection->clip.near_distance,
				projection->far_distance, cue->start,
				&scene->fog, NULL) != MH_ERROR_NONE)
			return 0;

		scene->view.fog = &scene->fog;
		scene->view.cue = cue;
	}

	/*
	 * Shared storage may occur in many spans; validate each visible mesh
	 * once.
	 */
	for (i = 0; i < software->mesh_count; i++) {
		mesh = &software->meshes[i];

		if (mesh->visible &&
			(!mh_software_mesh_supported(mesh->source) ||
				mesh->vertex_count !=
					mesh->source->vertex_count))
			return 0;
	}

	if (projection != NULL) {
		if (mh_software_order_setup(projection->clip.near_distance,
			projection->far_distance, MH_SOFTWARE_ORDER_ROOT,
			&scene->root_order, NULL) != MH_ERROR_NONE ||
			mh_software_order_setup(projection->clip.near_distance,
				projection->far_distance,
				MH_SOFTWARE_ORDER_CHILD, &scene->child_order,
				NULL) != MH_ERROR_NONE ||
			mh_software_order_setup(projection->clip.near_distance,
				projection->far_distance,
				MH_SOFTWARE_ORDER_SHADOW, &scene->shadow_order,
				NULL) != MH_ERROR_NONE)
			return 0;
	}

	return 1;
}

static int
prepare_part_view(struct mh_software_mesh *mesh,
	const struct mh_renderer_scene_part *part, const float *models,
	const float view[16], const struct scene_build *scene,
	struct mh_software_face_view *frame)
{
	const struct mh_mesh *source;
	const struct mh_projection *projection;

	source = mesh->source;
	projection = frame->projection;
	frame->model = models == NULL ? NULL : models + part->mesh * 16;
	frame->object = NULL;
	frame->lighting = &mesh->lighting;

	if (source->scenery != NULL &&
		(projection == NULL || !identity_model(frame->model)))
		return 0;

	/* World geometry calls the face producer in mode 1. */
	frame->producer_mode = mesh->shadow || source->world != NULL ||
		part->model_role == MH_RENDER_MODEL_WORLD;

	if (projection != NULL)
		frame->order = mesh->shadow ? &scene->shadow_order
			: part->model_role == MH_RENDER_MODEL_CHILD
			? &scene->child_order
			: &scene->root_order;

	if (mesh->shadow &&
		(projection == NULL || !identity_model(frame->model) ||
			!mh_software_mesh_shadow(source)))
		return 0;

	if (projection != NULL &&
		!mh_software_camera_transform(
			view, frame->model, frame->camera))
		return 0;

	if (mesh->lighting.enabled &&
		(projection == NULL ||
			!mh_software_model_light_prepare(
				&mesh->lighting, frame->model)))
		return 0;

	if (source->scenery == NULL && mesh->world_light.lighting.enabled) {
		if (projection == NULL ||
			!mh_software_world_light_prepare(&mesh->world_light,
				frame->model,
				models == NULL
					? NULL
					: models + mesh->light_root * 16))
			return 0;

		frame->lighting = &mesh->world_light.lighting;
	}

	return 1;
}

static int
prepare_scenery_view(struct mh_software_mesh *mesh, size_t primitive_index,
	enum mh_renderer_model_role role, const float view[16],
	struct mh_mesh_object *object, struct mh_software_face_view *frame)
{
	const struct mh_mesh *source;
	const struct mh_mesh_primitive *primitive;

	source = mesh->source;
	primitive = &source->primitives[primitive_index];

	if (source->scenery != NULL &&
		primitive->first >= source->world_vertex_count) {
		if (frame->object == NULL ||
			primitive_index < object->span.primitive_first ||
			primitive_index - object->span.primitive_first >=
				object->span.primitive_count) {
			if (!mh_software_mesh_object(
				source, primitive_index, object) ||
				!mh_software_world_light_prepare(
					&mesh->world_light, object->model,
					object->model) ||
				!mh_software_camera_transform(
					view, object->model, frame->camera))
				return 0;

			frame->object = object;
			frame->lighting = &mesh->world_light.lighting;
			frame->producer_mode = 0;
		}
	} else if (frame->object != NULL) {
		frame->object = NULL;
		frame->lighting = &mesh->lighting;
		frame->producer_mode = mesh->shadow || source->world != NULL ||
			role == MH_RENDER_MODEL_WORLD;

		if (!mh_software_camera_transform(
			view, frame->model, frame->camera))
			return 0;
	}

	return 1;
}

static int
prepare_part_primitives(struct mh_software_renderer *software,
	struct mh_software_mesh *mesh,
	const struct mh_renderer_scene_part *part,
	const struct mh_mesh_span *range, size_t part_index,
	const float view[16], struct mh_software_face_view *frame,
	struct scene_build *scene)
{
	const struct mh_mesh *source;
	const struct mh_mesh_primitive *primitive;
	struct mh_mesh_object object;
	struct mh_software_order_span *span;
	size_t j, first;
	unsigned int bucket;

	source = mesh->source;
	memset(&object, 0, sizeof(object));

	for (j = range->primitive_first;
		j < range->primitive_first + range->primitive_count; j++) {
		if (frame->projection != NULL &&
			!append_halos_at(software, part_index,
				j - range->primitive_first, &scene->halo_cursor,
				&scene->triangle_count, &scene->span_count))
			return 0;

		primitive = &source->primitives[j];

		if (source->primitive_hidden != NULL &&
			source->primitive_hidden[j])
			continue;

		if (source->draw_hidden != NULL &&
			source->draw_hidden[primitive->draw])
			continue;

		if (!prepare_scenery_view(
			mesh, j, part->model_role, view, &object, frame))
			return 0;

		first = scene->triangle_count;

		if (!mh_software_primitive_prepare(software, mesh, primitive,
			frame, &scene->triangle_count, &bucket))
			return 0;

		if (scene->triangle_count != first) {
			span = &software->spans[scene->span_count++];
			span->first = first;
			span->count = scene->triangle_count - first;
			span->bucket = bucket;
		}
	}

	return 1;
}

static int
prepare_part(struct mh_software_renderer *software, size_t part_index,
	const float *models, const float view[16], struct scene_build *scene)
{
	struct mh_renderer_scene_part part;
	struct mh_software_mesh *mesh;
	const struct mh_mesh *source;
	const struct mh_projection *projection;
	struct mh_mesh_span range;
	struct mh_mesh_lod detail;
	struct mh_software_face_view frame;
	size_t index;

	projection = scene->view.projection;

	if (software->parts != NULL)
		part = software->parts[part_index];
	else if (!default_part(software, part_index, &part))
		return 0;

	index = part.mesh;
	mesh = &software->meshes[index];
	source = mesh->source;

	memset(&range, 0, sizeof(range));

	if (!part.active_lod) {
		range = part.span;

		if (mh_scene_span_check(source, &range, NULL) != MH_ERROR_NONE)
			return 0;
	} else {
		if (mh_mesh_active_lod(source, &detail, NULL) != MH_ERROR_NONE)
			return 0;

		range.primitive_first = detail.primitive_first;
		range.primitive_count = detail.primitive_count;
	}

	if (!mesh->visible) {
		while (scene->name_cursor < software->names.count &&
			software->names.entries[scene->name_cursor].part ==
				part_index)
			scene->name_cursor++;

		if (projection != NULL)
			while (scene->halo_cursor < software->halos.count &&
				software->halos.entries[scene->halo_cursor]
						.part == part_index) {
				if (software->halos.entries[scene->halo_cursor]
						.before_primitive >
					range.primitive_count)
					return 0;

				scene->halo_cursor++;
			}

		return 1;
	}

	if (projection != NULL &&
		!append_names_at(software, part_index, &scene->name_cursor,
			&scene->triangle_count, &scene->span_count))
		return 0;

	frame = scene->view;

	if (!prepare_part_view(mesh, &part, models, view, scene, &frame))
		return 0;

	if (!prepare_part_primitives(software, mesh, &part, &range, part_index,
		view, &frame, scene))
		return 0;

	if (projection != NULL &&
		(!append_halos_at(software, part_index, range.primitive_count,
			&scene->halo_cursor, &scene->triangle_count,
			&scene->span_count) ||
			(scene->halo_cursor < software->halos.count &&
				software->halos.entries[scene->halo_cursor]
						.part <= part_index)))
		return 0;

	return 1;
}

static int
validate_draw_order(
	struct mh_software_renderer *software, size_t span_count, int lens)
{
	struct mh_software_texture_state texture_state;
	const struct mh_software_order_span *span;
	size_t i, j, index, first, halo_count;

	texture_state = software->texture_state;
	halo_count = 0;

	/*
	 * Stateful texture fallback must be checked in the actual draw order.
	 */
	for (i = 0; i < span_count; i++) {
		span = &software->spans[i];

		for (j = span->first; j < span->first + span->count; j++) {
			if (software->triangles[j].halo != 0) {
				software->halos.draw_order[halo_count++] =
					software->triangles[j].halo - 1;
				continue;
			}

			if (!mh_software_triangle_validate(software,
				&software->triangles[j], &texture_state,
				lens))
				return 0;
		}
	}

	/*
	 * Deferred chains share the post-geometry texture state, in marker
	 * order.
	 */
	for (i = 0; i < halo_count; i++) {
		index = software->halos.draw_order[i];
		first = software->halos.entries[index].first;

		for (j = first;
			j < first + software->halos.entries[index].count; j++)
			if (!mh_software_triangle_validate(software,
				&software->triangles[j], &texture_state, 1))
				return 0;
	}

	return 1;
}

int
mh_software_scene_prepare(struct mh_software_renderer *software,
	const float matrix[16], const float *models,
	const struct mh_projection *projection, const float view[16],
	const struct mh_depth_cue *cue, size_t *count)
{
	struct scene_build scene;
	size_t i, part_count;

	if (!prepare_scene_view(
		software, matrix, projection, cue, &scene, &part_count))
		return 0;

	for (i = 0; i < part_count; i++)
		if (!prepare_part(software, i, models, view, &scene))
			return 0;

	if (projection != NULL &&
		(!append_halos_at(software, part_count, 0, &scene.halo_cursor,
			&scene.triangle_count, &scene.span_count) ||
			scene.halo_cursor != software->halos.count ||
			scene.name_cursor != software->names.count))
		return 0;

	/* World/models, then skids, then particles. */
	if (projection != NULL &&
		(!append_skids(
			software, &scene.triangle_count, &scene.span_count) ||
			!append_particles(software, &scene.triangle_count,
				&scene.span_count)))
		return 0;

	if (projection != NULL &&
		mh_software_order_sort(software->spans, scene.span_count,
			scene.triangle_count, scene.root_order.buckets,
			NULL) != MH_ERROR_NONE)
		return 0;

	if (!validate_draw_order(
		software, scene.span_count, projection != NULL))
		return 0;

	*count = scene.span_count;

	return 1;
}
