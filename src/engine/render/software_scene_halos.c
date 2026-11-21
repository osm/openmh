#include <stdlib.h>
#include <string.h>

#include "engine/render/scene.h"
#include "engine/render/software_scene.h"

enum {
	HEADLIGHT_RESOURCE = 0,
	TAILLIGHT_RESOURCE = 1
};

enum mh_error_code
mh_software_halo_style(enum mh_software_halo_kind kind,
	const struct mh_geometry_halo *source, int braking,
	struct mh_renderer_halo *output, struct mh_error *error_out)
{
	struct mh_renderer_halo next;
	unsigned int i, j;

	if (source == NULL || output == NULL || kind < MH_SOFTWARE_HALO_WORLD ||
		kind > MH_SOFTWARE_HALO_SCENERY)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software halo style");

	memset(&next, 0, sizeof(next));
	memcpy(next.color, source->color, sizeof(next.color));
	next.flags = MH_HALO_GLOW;
	next.intensity = 1;
	next.size = .5f;

	for (i = 0; i < 2; i++)
		for (j = 0; j < 3; j++)
			next.endpoints[i][j] = (float)source->endpoints[i][j];

	if (kind == MH_SOFTWARE_HALO_WORLD) {
		/* Software world glows use their own image palette. */
		next.flags |= MH_HALO_FLARES | MH_HALO_WORLD;
		next.size = 1;
		next.flare = .65f;
		memset(next.color, 100, 3);
	} else if (kind == MH_SOFTWARE_HALO_SCENERY) {
		/* RGB tint comes from the current object pose. */
		next.intensity = .6f;
		next.tinted = 1;
	} else if (source->resource == HEADLIGHT_RESOURCE) {
		/* Headlights. */
		next.flags = MH_HALO_HEADLIGHT;
		next.intensity = .4f;
		next.size = 1;
		next.flare = .5f;
		next.direction[0] =
			source->endpoints[0][0] < 0 ? -.1961f : .1961f;
		next.direction[2] = .9806f;
		memset(next.color, 100, 3);
	} else if (source->resource == TAILLIGHT_RESOURCE) {
		/* Tail and brake lights. */
		next.flags = MH_HALO_TAILLIGHT;
		next.intensity = braking ? .75f : .5f;
		next.size = braking ? (float)((double).3f * 1.75) : .3f;
		next.direction[2] = -1;
		next.color[0] = 201;
		next.color[1] = next.color[2] = 0;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_prepare_software_halos(struct mh_race_scene *scene,
	unsigned int cars, struct mh_error *error_out)
{
	struct mh_software_scene_halos *halos;
	const struct mh_world *world;
	struct mh_world_cell cell;
	struct mh_renderer_halo *sources;
	size_t count, i, extra;

	if (scene == NULL || cars < 1 || cars > MH_RACE_MAX_CARS ||
		scene->assets.profile != MH_RENDER_SOFTWARE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software halo scene");

	world = &scene->assets.lighting_world;
	count = mh_mesh_scene_halo_count(&scene->assets.meshes[0]);

	for (i = 0; i < world->cell_count; i++) {
		cell = world->cells[i];

		if (cell.halo_count > MH_LAMP_LIMIT ||
			count > MH_LAMP_LIMIT - cell.halo_count)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software world halo list exceeds capacity");

		count += cell.halo_count;
	}

	for (i = 0; i < cars; i++) {
		extra = scene->assets.meshes[1 + i * MH_CAR_MESHES].halo_count;

		if (extra > MH_LAMP_LIMIT || count > MH_LAMP_LIMIT - extra)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software car halo list exceeds capacity");

		count += extra;
	}

	halos = &scene->visuals.software_halos;

	if (count <= halos->capacity)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	sources = calloc(count, sizeof(*sources));

	if (sources == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing software halos");

	free(halos->sources);
	halos->sources = sources;
	halos->count = 0;
	halos->capacity = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_source(struct mh_software_scene_halos *halos,
	enum mh_software_halo_kind kind, const struct mh_geometry_halo *source,
	int braking, const struct mh_software_halo_assets *assets, size_t part,
	size_t before, const float *model, const float *tint, int lenses,
	struct mh_error *error_out)
{
	struct mh_renderer_halo next;
	enum mh_error_code status;

	if (halos->count >= halos->capacity ||
		source->resource >= assets->count)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software halo source or image exceeds prepared pool");

	status =
		mh_software_halo_style(kind, source, braking, &next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.part = part;
	next.before_primitive = before;
	next.model = model;
	next.image = &assets->images[source->resource];
	next.lenses = lenses;

	if (tint != NULL)
		memcpy(next.tint, tint, sizeof(next.tint));

	halos->sources[halos->count++] = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
world_sources(struct mh_race_scene *scene, const struct mh_world_view *view,
	int lenses, struct mh_error *error_out)
{
	const struct mh_world *world;
	struct mh_world_cell cell;
	struct mh_geometry_halo source;
	struct mh_geometry_halo halo;
	size_t index, before;
	unsigned long x, z, n;
	enum mh_error_code status;

	world = &scene->assets.lighting_world;

	for (z = view->first[1]; z < view->end[1]; z++)
		for (x = view->first[0]; x < view->end[0]; x++) {
			index = (size_t)z * world->width + x;

			if (index >= world->cell_count)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software halo world cell");

			cell = world->cells[index];

			if (cell.halo_count == 0)
				continue;

			if (!mh_mesh_world_cell_primitive(
				&scene->assets.meshes[0], index, &before))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software halo cell insertion");

			for (n = 0; n < cell.halo_count; n++) {
				if (cell.halo_first + n >= world->halo_count)
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"invalid software world halo");

				source = world->halos[cell.halo_first + n];
				halo.resource = source.resource;
				memcpy(halo.endpoints, source.endpoints,
					sizeof(halo.endpoints));
				memcpy(halo.color, source.color,
					sizeof(halo.color));
				status = append_source(
					&scene->visuals.software_halos,
					MH_SOFTWARE_HALO_WORLD, &halo, 0,
					&scene->assets.software.track_halos, 0,
					before, NULL, NULL, lenses, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
car_sources(struct mh_race_scene *scene, const struct mh_field *field,
	const float *models, const struct mh_world_view *view, int lenses,
	struct mh_error *error_out)
{
	const struct mh_mesh *mesh;
	struct mh_mesh_lod lod;
	unsigned int car, root;
	size_t n, part;
	enum mh_error_code status;

	for (car = 0; car < field->count; car++) {
		root = 1 + car * MH_CAR_MESHES;
		mesh = &scene->assets.meshes[root];

		if (!mh_race_scene_part_visible(scene, field, root) ||
			(field->unavailable_mask & (1U << car)) ||
			!mh_frustum_sphere_profile(MH_RENDER_SOFTWARE,
				&view->frustum, models + root * 16 + 12,
				mesh->radius))
			continue;

		status = mh_mesh_active_lod(mesh, &lod, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		part = mh_race_scene_software_car_part(car);

		for (n = 0; n < lod.halo_count; n++) {
			status = append_source(&scene->visuals.software_halos,
				MH_SOFTWARE_HALO_CAR,
				&mesh->halos[lod.halo_first + n],
				field->cars[car].vehicle.brake_active != 0,
				&scene->assets.software.car_halos[car], part, 0,
				models + root * 16, NULL, lenses, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
scenery_sources(struct mh_race_scene *scene, unsigned int cars,
	const struct mh_world_view *view, int lenses,
	struct mh_error *error_out)
{
	const struct mh_mesh *world;
	struct mh_geometry_halo halo;
	float position[3], radius, tint[3];
	size_t spans, i, n, first, count, part;
	enum mh_error_code status;

	world = &scene->assets.meshes[0];
	spans = mh_mesh_scene_span_count(world);

	for (i = 1; i < spans; i++) {
		if (!mh_mesh_scene_halo_span(world, i - 1, &first, &count))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software scenery halo span");

		if (count == 0)
			continue;

		if (!mh_mesh_scene_halo_bounds(world, first, position, &radius))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software scenery halo bounds");

		if (!mh_frustum_sphere_profile(MH_RENDER_SOFTWARE,
			&view->frustum, position, radius))
			continue;

		part = mh_race_scene_software_world_part(cars, i);

		for (n = 0; n < count; n++) {
			if (!mh_mesh_scene_halo(world, first + n, &halo, tint))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software scenery halo pose");

			status = append_source(&scene->visuals.software_halos,
				MH_SOFTWARE_HALO_SCENERY, &halo, 0,
				&scene->assets.software.track_halos, part, 0,
				NULL, tint, lenses, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_update_software_halos(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models,
	const struct mh_world_view *view, const struct mh_av_settings *settings,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	int lenses;

	if (scene == NULL || field == NULL || models == NULL || view == NULL ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS ||
		view->profile != MH_RENDER_SOFTWARE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software halo frame");

	scene->visuals.software_halos.count = 0;

	if (settings != NULL && !settings->halos)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	lenses = settings == NULL || settings->lens_flares;
	status = world_sources(scene, view, lenses, error_out);

	if (status == MH_ERROR_NONE)
		status = car_sources(
			scene, field, models, view, lenses, error_out);

	if (status == MH_ERROR_NONE)
		status = scenery_sources(
			scene, field->count, view, lenses, error_out);

	return status;
}
