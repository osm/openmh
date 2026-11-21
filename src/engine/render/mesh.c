#include <stdlib.h>
#include <string.h>

#include "engine/render/mesh_internal.h"

void
mh_mesh_free(struct mh_mesh *mesh)
{
	size_t i;
	struct mh_mesh_binding *binding;

	if (mesh->world != NULL) {
		free(mesh->world->models);
		free(mesh->world->cells);
		free(mesh->world->planes);
		free(mesh->world);
	}

	if (mesh->scenery != NULL) {
		while (mesh->scenery->bindings != NULL) {
			binding = mesh->scenery->bindings;
			mesh->scenery->bindings = binding->next;
			free(binding->source.keys);
			free(binding->local_vertices);
			free(binding->faces);
			free(binding->staged_vertices);
			free(binding->halos);
			free(binding);
		}

		free(mesh->scenery);
	}

	for (i = 0; i < mesh->texture_count; i++) {
		free(mesh->textures[i].rgba);
	}

	for (i = 0; i < mesh->indexed_texture_count; i++)
		free(mesh->indexed_textures[i].indices);

	free(mesh->indexed_textures);
	free(mesh->textures);
	free(mesh->texture_formats);
	free(mesh->vertices);
	free(mesh->material_rgb);
	free(mesh->draws);
	free(mesh->draw_hidden);
	free(mesh->primitive_hidden);
	free(mesh->draw_groups);
	free(mesh->atlas_images);
	free(mesh->atlas_uv);
	free(mesh->faces);
	free(mesh->primitives);
	free(mesh->halos);
	free(mesh->world_colors);
	memset(mesh, 0, sizeof(*mesh));
}

size_t
mh_mesh_scene_span_count(const struct mh_mesh *mesh)
{
	return mesh == NULL || mesh->world == NULL
		? 0
		: 1 + mesh->world->scene_count;
}

int
mh_mesh_scene_span(
	const struct mh_mesh *mesh, size_t index, struct mh_mesh_span *span)
{
	const struct mh_mesh_binding *binding;
	struct mh_mesh_span selected;

	if (span == NULL || index >= mh_mesh_scene_span_count(mesh))
		return 0;

	selected = mesh->world->span;

	if (index != 0) {
		if (mesh->scenery == NULL)
			return 0;

		for (binding = mesh->scenery->bindings; binding != NULL;
			binding = binding->next)
			if (binding->scene_index == index - 1)
				break;

		if (binding == NULL)
			return 0;

		selected.draw_first = binding->draw_first;
		selected.draw_count = binding->draw_count;
		selected.primitive_first = binding->primitive_first;
		selected.primitive_count = binding->primitive_count;
	}

	if (selected.draw_first > mesh->draw_count ||
		selected.draw_count > mesh->draw_count - selected.draw_first ||
		selected.primitive_first > mesh->primitive_count ||
		selected.primitive_count >
			mesh->primitive_count - selected.primitive_first)
		return 0;

	*span = selected;

	return 1;
}

int
mh_mesh_scene_object(
	const struct mh_mesh *mesh, size_t index, struct mh_mesh_object *object)
{
	const struct mh_mesh_binding *binding;
	struct mh_mesh_object selected;
	unsigned int i, k;

	if (mesh == NULL || object == NULL || mesh->scenery == NULL ||
		mesh->scenery->fit_radius != 1 ||
		mesh->scenery->fit_center[0] != 0 ||
		mesh->scenery->fit_center[1] != 0 ||
		mesh->scenery->fit_center[2] != 0 || index == (size_t)-1 ||
		!mh_mesh_scene_span(mesh, index + 1, &selected.span))
		return 0;

	for (binding = mesh->scenery->bindings; binding != NULL;
		binding = binding->next)
		if (binding->scene_index == index)
			break;

	if (binding == NULL || binding->local_vertices == NULL ||
		binding->faces == NULL ||
		binding->face_count != selected.span.primitive_count ||
		binding->vertex_first > mesh->vertex_count ||
		binding->vertex_count >
			mesh->vertex_count - binding->vertex_first)
		return 0;

	selected.vertices = binding->local_vertices;
	selected.faces = binding->faces;
	selected.face_count = binding->face_count;
	selected.vertex_first = binding->vertex_first;
	selected.vertex_count = binding->vertex_count;
	memset(selected.model, 0, sizeof(selected.model));
	selected.model[15] = 1;

	for (i = 0; i < 3; i++) {
		selected.model[12 + i] = (float)binding->pose.position[i];
		selected.tint[i] = (float)binding->pose.color[i];

		for (k = 0; k < 3; k++)
			selected.model[k * 4 + i] =
				(float)binding->pose.rotation[k * 3 + i];
	}

	*object = selected;

	return 1;
}
