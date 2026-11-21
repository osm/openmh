#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/animation_source.h"
#include "../support/atlas_source.h"
#include "../support/catalog_source.h"
#include "../support/collision_source.h"
#include "../support/geometry_source.h"
#include "../support/horn_source.h"
#include "../support/navigation_source.h"
#include "../support/profile_source.h"
#include "../support/retro_source.h"
#include "../support/surface_source.h"
#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/collision/provider.h"
#include "backends/font/provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/image/remap_provider.h"
#include "backends/image/tga_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/retro/provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/assets/mesh_load.h"
#include "engine/assets/object_assets.h"
#include "engine/audio/ambient_assets.h"
#include "engine/audio/menu_sound_assets.h"
#include "engine/audio/pause_sound_assets.h"
#include "engine/audio/race_sound_assets.h"
#include "engine/audio/surface_assets.h"
#include "engine/catalog/load.h"
#include "engine/catalog/profile_load.h"
#include "engine/core/text.h"
#include "engine/geometry/world.h"
#include "engine/render/scene.h"
#include "engine/render/scene_finish.h"
#include "engine/simulation/collision.h"
#include "engine/storage/asset_path.h"
#include "engine/ui/hud_assets.h"
#include "engine/ui/results_assets.h"
#include "engine/ui/software_hud_assets.h"
#include "interfaces/audio/horn_source.h"

#define SNAPSHOT_ENTRIES 512
#define SCENE_PROFILES 4
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct snapshot {
	const char			*root;
	struct mh_resource_entry	entries[SNAPSHOT_ENTRIES];
	size_t				count;
};

struct loaded_assets {
	struct mh_race_scene		scenes[SCENE_PROFILES];
	struct mh_hud_assets		hud;
	struct mh_software_hud_assets	software_hud;
	struct mh_results_assets	results[2];
	struct mh_ambient_assets	ambient;
	struct mh_menu_sound_assets	menu;
	struct mh_menu_horn_assets	menu_horns;
	struct mh_pause_sound_assets	pause;
	struct mh_race_sound_assets	race;
	struct mh_surface_assets	surfaces;
	struct mh_horn_catalog		horns;
	struct mh_profile_catalog	profiles;
	struct mh_track			track;
	struct mh_mesh			car, glide, software;
	struct mh_object_assets		objects;
	struct mh_file			archive_file;
	struct mh_world			world;
	char				car_name[128];
	unsigned long			random;
};

static enum mh_error_code
capture_read(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct snapshot *snapshot;
	struct mh_file file;
	const char *relative;
	unsigned char *data;
	char *key;
	size_t root_size, i;
	enum mh_error_code status;

	snapshot = context;
	root_size = strlen(snapshot->root);

	if (strncmp(path, snapshot->root, root_size) != 0 ||
		path[root_size] != '/')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"resource escaped fixture root");

	relative = path + root_size;
	status = mh_resource_source_read_bounded(
		mh_filesystem_resources(), path, limit, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < snapshot->count; i++) {
		if (strcmp(snapshot->entries[i].path + strlen("memory"),
			relative) == 0) {
			*output = file;
			return MH_ERROR_NONE;
		}
	}

	if (snapshot->count == SNAPSHOT_ENTRIES) {
		free(file.data);
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "fixture snapshot full");
	}

	key = malloc(sizeof("memory") + strlen(relative));
	data = malloc(file.size == 0 ? 1 : file.size);

	if (key == NULL || data == NULL) {
		free(key);
		free(data);
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"snapshot allocation failed");
	}

	strcpy(key, "memory");
	strcat(key, relative);

	if (file.size != 0)
		memcpy(data, file.data, file.size);

	snapshot->entries[i].path = key;
	snapshot->entries[i].data = data;
	snapshot->entries[i].size = file.size;
	snapshot->count++;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
capture_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_resource_source_size(
		mh_filesystem_resources(), path, output, error_out);
}

static enum mh_error_code
capture_list(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	(void)context;

	return mh_resource_source_list(
		mh_filesystem_resources(), path, visit, visitor, error_out);
}

static int
load_assets(const struct mh_retro_source *retro_source,
	const struct mh_profile_source *profile_source,
	const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_resource_source *source, const char *root, int padded,
	struct loaded_assets *assets)
{
	static const enum mh_render_profile profiles[SCENE_PROFILES] = {
		MH_RENDER_GLIDE, MH_RENDER_SOFTWARE, MH_RENDER_DIRECT3D,
		MH_RENDER_POWERVR};

	struct mh_av_settings settings;
	struct mh_error error;
	struct mh_image_collection *archive;
	unsigned int i;
	char path[1024];

	archive = NULL;
	assets->random = 31;
#define LOAD(e)                                                                \
	do {                                                                   \
		if ((e) != MH_ERROR_NONE) {                                    \
			fprintf(stderr, "%s\n", error.message);                \
			mh_image_collection_close(archive);                    \
			return 1;                                              \
		}                                                              \
	} while (0)
	LOAD(mh_asset_path_from(source, root, "Tracks/track1/Packed/tex1.pdi",
		path, sizeof(path), &error));
	LOAD(mh_resource_source_read(
		source, path, &assets->archive_file, &error));
	LOAD(mh_image_pdi_open(assets->archive_file.data,
		assets->archive_file.size, &archive, &error));
	LOAD(mh_horn_catalog_load_from(
		horn_source, source, root, &assets->horns, &error));
	LOAD(mh_profile_catalog_load_from(profile_source, source, root,
		&assets->horns, &assets->random, &assets->profiles, &error));
	LOAD(mh_track_config_load_from(
		catalog_source, source, root, 1, &assets->track, &error));
	LOAD(mh_car_mesh_load_from(catalog_source, model_decoder,
		mh_image_fallback_decoder(), source, root, 1, archive,
		&assets->car, assets->car_name, &error));
	LOAD(mh_mesh_environment_load_from(mh_image_fallback_decoder(), source,
		root, &assets->track, &assets->car, &error));
	LOAD(mh_world_source_load_from(world_decoder, source, root,
		&assets->track, 0, &assets->world, &error));
	LOAD(mh_world_mesh_build_from(animation_source, model_decoder,
		mh_image_fallback_decoder(), source, root, &assets->track,
		&assets->world, 1, 1, archive, MH_RENDER_GLIDE, &assets->glide,
		&error));
	mh_world_free(&assets->world);
	LOAD(mh_world_source_load_from(world_decoder, source, root,
		&assets->track, 1, &assets->world, &error));
	LOAD(mh_world_mesh_build_from(animation_source, model_decoder,
		mh_image_fallback_decoder(), source, root, &assets->track,
		&assets->world, 1, 1, archive, MH_RENDER_SOFTWARE,
		&assets->software, &error));
	mh_image_collection_close(archive);
	archive = NULL;
	LOAD(mh_object_assets_load_from(animation_source, model_decoder,
		collision_source, source, root, &assets->track,
		&assets->objects, &error));
	LOAD(mh_menu_sound_assets_load_from(mh_audio_fallback_decoder(), source,
		root, &assets->menu, &error));
	LOAD(mh_menu_horn_assets_load_from(mh_audio_fallback_decoder(), source,
		root, &assets->horns, &assets->menu_horns, &error));
	LOAD(mh_pause_sound_assets_load_from(mh_audio_fallback_decoder(),
		source, root, &assets->pause, &error));
	LOAD(mh_race_sound_assets_load_from(horn_source,
		mh_audio_fallback_decoder(), source, root, NULL, &assets->race,
		&error));
	LOAD(mh_ambient_assets_load_from(env_sound_source, animation_source,
		mh_audio_fallback_decoder(), source, root,
		assets->track.env_sound_file, assets->track.scene_file,
		&assets->ambient, &error));
	LOAD(mh_surface_assets_load_from(material_source, env_sound_source,
		mh_audio_fallback_decoder(), source, root,
		assets->track.materials_file, assets->track.env_sound_file,
		&assets->surfaces, &error));

	LOAD(mh_av_settings_init(&settings, 0, 0, &error));
	settings.texture_filter =
		padded ? MH_AV_FILTER_TRILINEAR : MH_AV_FILTER_ORIGINAL;

	for (i = 0; i < SCENE_PROFILES; i++) {
		LOAD(mh_race_scene_load_profile_from(horn_source,
			material_source, env_sound_source, catalog_source,
			navigation_source, animation_source, atlas_source,
			model_decoder, world_decoder, collision_source,
			mh_image_pdi_source(), mh_image_fallback_decoder(),
			source, root, 1, 1, 1,
			profiles[i] == MH_RENDER_DIRECT3D ? &settings : NULL,
			profiles[i], &assets->scenes[i], &error));
		LOAD(mh_race_scene_add_opponents_from(
			source, root, 1, 1, 1, &assets->scenes[i], &error));
		LOAD(mh_race_scene_load_finish(
			source, root, &assets->scenes[i], &error));
		LOAD(mh_race_scene_load_ai_from(
			source, root, &assets->scenes[i], &error));
		LOAD(mh_race_scene_load_retro_from(retro_source, source, root,
			&assets->scenes[i], &error));
	}

	LOAD(mh_hud_assets_load_from(mh_font_fallback_source(),
		mh_image_fallback_decoder(), source, root, &assets->hud,
		&error));
	LOAD(mh_hud_assets_map_from(mh_image_fallback_decoder(), source, root,
		&assets->track, &assets->hud, &error));
	LOAD(mh_software_hud_assets_load_from(mh_sprite_spr_source(),
		mh_font_fallback_source(), source, root, &assets->track, 320, 6,
		&assets->software_hud, &error));
	LOAD(mh_results_assets_load_from(profile_source,
		mh_font_fallback_source(), mh_image_fallback_decoder(),
		mh_image_tga_portrait_decoder(), source, root, 2, "Player",
		&assets->hud.atlas, &assets->results[0], &error));
	LOAD(mh_results_assets_load_from(profile_source,
		mh_font_fallback_source(), mh_image_fallback_decoder(),
		mh_image_tga_portrait_decoder(), source, root, 2, "Player",
		&assets->software_hud.atlas, &assets->results[1], &error));
#undef LOAD

	return 0;
}

static int
same_collision(const struct mh_collision *a, const struct mh_collision *b)
{
	size_t i;
	unsigned int child;

	CHECK(a->depth == b->depth && a->root_count == b->root_count &&
		a->node_count == b->node_count &&
		a->leaf_count == b->leaf_count &&
		a->polygon_count == b->polygon_count &&
		a->polygon_index_count == b->polygon_index_count);
	CHECK(memcmp(a->dimensions, b->dimensions, sizeof(a->dimensions)) == 0);
	CHECK(memcmp(a->roots, b->roots, sizeof(a->roots)) == 0);
	CHECK(memcmp(a->translation, b->translation, sizeof(a->translation)) ==
		0);
	CHECK(a->scale == b->scale && a->cell_size == b->cell_size);

	for (i = 0; i < a->root_count; i++) {
		CHECK(a->cells[i].kind == b->cells[i].kind &&
			a->cells[i].index == b->cells[i].index);
	}

	for (i = 0; i < a->node_count; i++) {
		for (child = 0; child < MH_COLLISION_CHILDREN; child++) {
			CHECK(a->nodes[i].children[child].kind ==
				b->nodes[i].children[child].kind);
			CHECK(a->nodes[i].children[child].index ==
				b->nodes[i].children[child].index);
		}
	}

	for (i = 0; i < a->leaf_count; i++) {
		CHECK(a->leaves[i].first == b->leaves[i].first &&
			a->leaves[i].count == b->leaves[i].count);
	}

	for (i = 0; i < a->polygon_index_count; i++)
		CHECK(a->polygon_indices[i] == b->polygon_indices[i]);

	for (i = 0; i < a->polygon_count; i++) {
		CHECK(a->polygons[i].material == b->polygons[i].material &&
			a->polygons[i].boundary_count ==
				b->polygons[i].boundary_count);
		CHECK(memcmp(a->polygons[i].plane, b->polygons[i].plane,
			sizeof(a->polygons[i].plane)) == 0);
		CHECK(memcmp(a->polygons[i].boundaries,
			b->polygons[i].boundaries,
			sizeof(a->polygons[i].boundaries)) == 0);
	}

	return 0;
}

static int
same_world_face(
	const struct mh_geometry_face *a, const struct mh_geometry_face *b)
{
	unsigned int corner;

	CHECK(a->type == b->type && a->count == b->count);
	CHECK(a->flags == b->flags && a->degenerate == b->degenerate);
	CHECK(a->texture == b->texture);
	CHECK(memcmp(a->color, b->color, sizeof(a->color)) == 0);
	CHECK(memcmp(a->plane, b->plane, sizeof(a->plane)) == 0);

	for (corner = 0; corner < a->count; corner++) {
		CHECK(memcmp(a->corners[corner].position,
			b->corners[corner].position,
			sizeof(a->corners[corner].position)) == 0);
		CHECK(memcmp(a->corners[corner].normal,
			b->corners[corner].normal,
			sizeof(a->corners[corner].normal)) == 0);
		CHECK(memcmp(a->corners[corner].uv, b->corners[corner].uv,
			sizeof(a->corners[corner].uv)) == 0);
		CHECK(memcmp(a->corners[corner].color, b->corners[corner].color,
			sizeof(a->corners[corner].color)) == 0);
	}

	return 0;
}

static int
same_world_model(const struct mh_world_model *a, const struct mh_world_model *b)
{
	size_t i;

	CHECK(a->radius == b->radius);
	CHECK(memcmp(a->center, b->center, sizeof(a->center)) == 0);
	CHECK(a->geometry.vertex_count == b->geometry.vertex_count);
	CHECK(a->geometry.face_count == b->geometry.face_count);
	CHECK(a->geometry.radius == b->geometry.radius);
	CHECK(memcmp(a->geometry.minimum, b->geometry.minimum,
		sizeof(a->geometry.minimum)) == 0);
	CHECK(memcmp(a->geometry.maximum, b->geometry.maximum,
		sizeof(a->geometry.maximum)) == 0);

	for (i = 0; i < a->geometry.face_count; i++) {
		CHECK(a->face_ids[i] == b->face_ids[i]);
		CHECK(same_world_face(&a->geometry.faces[i],
			&b->geometry.faces[i]) == 0);
	}

	return 0;
}

static int
same_world(const struct mh_world *a, const struct mh_world *b)
{
	size_t i;

	CHECK(a->width == b->width && a->height == b->height);
	CHECK(a->face_count == b->face_count &&
		a->model_count == b->model_count);
	CHECK(a->cell_count == b->cell_count &&
		a->texture_count == b->texture_count);
	CHECK(a->light_count == b->light_count &&
		a->link_count == b->link_count);
	CHECK(a->halo_count == b->halo_count &&
		a->has_directional == b->has_directional);
	CHECK(memcmp(a->minimum, b->minimum, sizeof(a->minimum)) == 0);
	CHECK(memcmp(a->maximum, b->maximum, sizeof(a->maximum)) == 0);
	CHECK(memcmp(a->grid_minimum, b->grid_minimum,
		sizeof(a->grid_minimum)) == 0);
	CHECK(memcmp(a->cell_size, b->cell_size, sizeof(a->cell_size)) == 0);

	if (a->has_directional) {
		CHECK(a->directional.intensity == b->directional.intensity);
		CHECK(memcmp(a->directional.color, b->directional.color,
			sizeof(a->directional.color)) == 0);
		CHECK(memcmp(a->directional.direction, b->directional.direction,
			sizeof(a->directional.direction)) == 0);
	}

	for (i = 0; i < a->texture_count; i++)
		CHECK(strcmp(a->textures[i], b->textures[i]) == 0);

	for (i = 0; i < a->model_count; i++) {
		CHECK(a->models[i].geometry.textures == a->textures);
		CHECK(b->models[i].geometry.textures == b->textures);
		CHECK(a->models[i].geometry.texture_count == a->texture_count);
		CHECK(b->models[i].geometry.texture_count == b->texture_count);
		CHECK(same_world_model(&a->models[i], &b->models[i]) == 0);
	}

	for (i = 0; i < a->cell_count; i++) {
		CHECK(a->cells[i].model == b->cells[i].model);
		CHECK(a->cells[i].light_head == b->cells[i].light_head);
		CHECK(a->cells[i].halo_first == b->cells[i].halo_first);
		CHECK(a->cells[i].halo_count == b->cells[i].halo_count);
	}

	for (i = 0; i < a->light_count; i++) {
		CHECK(a->lights[i].intensity == b->lights[i].intensity);
		CHECK(a->lights[i].radius == b->lights[i].radius);
		CHECK(memcmp(a->lights[i].position, b->lights[i].position,
			sizeof(a->lights[i].position)) == 0);
		CHECK(memcmp(a->lights[i].color, b->lights[i].color,
			sizeof(a->lights[i].color)) == 0);
	}

	for (i = 0; i < a->link_count; i++) {
		CHECK(a->links[i].light == b->links[i].light);
		CHECK(a->links[i].next == b->links[i].next);
	}

	for (i = 0; i < a->halo_count; i++) {
		CHECK(a->halos[i].resource == b->halos[i].resource);
		CHECK(memcmp(a->halos[i].endpoints, b->halos[i].endpoints,
			sizeof(a->halos[i].endpoints)) == 0);
		CHECK(memcmp(a->halos[i].color, b->halos[i].color,
			sizeof(a->halos[i].color)) == 0);
	}

	return 0;
}

static int
same_mesh(const struct mh_mesh *a, const struct mh_mesh *b)
{
	size_t i;

	CHECK(a->vertex_count != 0 && a->vertex_count == b->vertex_count);
	CHECK(a->draw_count == b->draw_count &&
		a->texture_count == b->texture_count);
	CHECK(a->primitive_count == b->primitive_count);
	CHECK(memcmp(a->vertices, b->vertices,
		a->vertex_count * sizeof(*a->vertices)) == 0);

	for (i = 0; i < a->draw_count; i++) {
		CHECK(a->draws[i].first == b->draws[i].first);
		CHECK(a->draws[i].count == b->draws[i].count);
		CHECK(a->draws[i].texture == b->draws[i].texture);
	}

	for (i = 0; i < a->texture_count; i++) {
		CHECK(a->textures[i].width == b->textures[i].width &&
			a->textures[i].height == b->textures[i].height);
		CHECK(memcmp(a->textures[i].rgba, b->textures[i].rgba,
			(size_t)a->textures[i].width *
				a->textures[i].height * 4) == 0);
	}

	CHECK((a->atlas_uv == NULL) == (b->atlas_uv == NULL));
	CHECK((a->atlas_images == NULL) == (b->atlas_images == NULL));

	if (a->atlas_uv != NULL)
		CHECK(memcmp(a->atlas_uv, b->atlas_uv,
			a->vertex_count * 2 * sizeof(*a->atlas_uv)) == 0);

	if (a->atlas_images != NULL)
		CHECK(memcmp(a->atlas_images, b->atlas_images,
			a->texture_count * sizeof(*a->atlas_images)) ==
			0);

	return 0;
}

static int
same_wave(const struct mh_audio_pcm *a, const struct mh_audio_pcm *b)
{
	CHECK(a->bytes == b->bytes && a->frames == b->frames);
	CHECK(a->rate == b->rate && a->channels == b->channels &&
		a->bits == b->bits);

	if (a->bytes != 0)
		CHECK(memcmp(a->pcm, b->pcm, a->bytes) == 0);

	return 0;
}

static int
same_audio(const struct loaded_assets *a, const struct loaded_assets *b)
{
	unsigned int i;

	CHECK(a->ambient.bank.count == b->ambient.bank.count);
	CHECK(a->surfaces.bank.count == b->surfaces.bank.count);
	CHECK(memcmp(&a->surfaces.bank.materials, &b->surfaces.bank.materials,
		sizeof(a->surfaces.bank.materials)) == 0);
	CHECK(memcmp(a->ambient.bank.sounds, b->ambient.bank.sounds,
		sizeof(a->ambient.bank.sounds)) == 0);
	CHECK(memcmp(&a->menu.bank.config, &b->menu.bank.config,
		sizeof(a->menu.bank.config)) == 0);

	for (i = 0; i < a->ambient.bank.count; i++)
		CHECK(same_wave(&a->ambient.bank.waves[i],
			&b->ambient.bank.waves[i]) == 0);

	for (i = 0; i < a->surfaces.bank.count; i++)
		CHECK(same_wave(&a->surfaces.bank.waves[i],
			&b->surfaces.bank.waves[i]) == 0);

	for (i = 0; i < MH_MENU_SOUND_SAMPLES; i++)
		CHECK(same_wave(&a->menu.bank.waves[i],
			&b->menu.bank.waves[i]) == 0);

	for (i = 0; i < MH_PAUSE_SOUND_SAMPLES; i++)
		CHECK(same_wave(&a->pause.bank.waves[i],
			&b->pause.bank.waves[i]) == 0);

	for (i = 0; i < a->horns.count; i++)
		CHECK(same_wave(&a->menu_horns.bank.waves[i],
			&b->menu_horns.bank.waves[i]) == 0);

	for (i = 0; i < MH_RACE_SAMPLES; i++)
		CHECK(same_wave(&a->race.bank.waves[i],
			&b->race.bank.waves[i]) == 0);

	for (i = 0; i < MH_TRAFFIC_MAX_CARS; i++) {
		CHECK(same_wave(&a->race.bank.horns[i].wave,
			&b->race.bank.horns[i].wave) == 0);
		CHECK(a->race.bank.horns[i].wave.pcm ==
			a->race.bank.horns[0].wave.pcm);
		CHECK(b->race.bank.horns[i].wave.pcm ==
			b->race.bank.horns[0].wave.pcm);
	}

	return 0;
}

static int
same_image(const struct mh_image *a, const struct mh_image *b)
{
	CHECK(a->width == b->width && a->height == b->height);
	CHECK(memcmp(a->rgba, b->rgba, (size_t)a->width * a->height * 4) == 0);

	return 0;
}

static int
same_indexed(const struct mh_indexed_image *a, const struct mh_indexed_image *b)
{
	CHECK(a->width == b->width && a->height == b->height &&
		a->colors == b->colors);
	CHECK(a->size == b->size);
	CHECK(memcmp(a->indices, b->indices, a->size) == 0);
	CHECK(memcmp(a->palette, b->palette, sizeof(a->palette)) == 0);

	return 0;
}

static int
same_road_queries(const struct mh_race_scene *a, const struct mh_race_scene *b)
{
	struct mh_contact first, second;
	double start[3], end[3];
	size_t i, hits;

	hits = 0;

	for (i = 0; i < a->assets.route.motion.key_count; i++) {
		memcpy(start, a->assets.route.motion.keys[i].value,
			sizeof(start));
		memcpy(end, start, sizeof(end));
		start[1] += 5;
		end[1] -= 50;
		CHECK(mh_collision_cast(&a->assets.world_collision, start, end,
			&first, NULL) == MH_ERROR_NONE);
		CHECK(mh_collision_cast(&b->assets.world_collision, start, end,
			&second, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&first, &second, sizeof(first)) == 0);
		hits += first.hit != 0;
	}

	CHECK(hits != 0);

	return 0;
}

static int
same_motion(const struct mh_scene_object *a, const struct mh_scene_object *b)
{
	const struct mh_scene_key *first, *second;
	double values_a[9], values_b[9];
	size_t i;

	CHECK(a->key_count == b->key_count &&
		a->end_behavior == b->end_behavior);
	CHECK(strcmp(a->model, b->model) == 0);
	CHECK(memcmp(a->position, b->position, sizeof(a->position)) == 0);
	CHECK(memcmp(a->rotation, b->rotation, sizeof(a->rotation)) == 0);
	CHECK(memcmp(a->color, b->color, sizeof(a->color)) == 0);
	CHECK(a->sound_class == b->sound_class &&
		a->collision_sound_class == b->collision_sound_class);
	CHECK(a->has_edge_color == b->has_edge_color &&
		a->polygon_size == b->polygon_size);
	CHECK(a->unseen_by_rays == b->unseen_by_rays &&
		a->has_shadow_options == b->has_shadow_options);
	CHECK(a->has_polygon_size == b->has_polygon_size);

	for (i = 0; i < a->key_count; i++) {
		first = &a->keys[i];
		second = &b->keys[i];
		CHECK(first->time == second->time &&
			first->linear == second->linear);
		CHECK(first->tension == second->tension &&
			first->continuity == second->continuity);
		CHECK(first->bias == second->bias);
		CHECK(memcmp(first->value, second->value,
			sizeof(first->value)) == 0);
		CHECK(mh_scene_channels(a, first->time * 0.75, values_a,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_scene_channels(b, first->time * 0.75, values_b,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(values_a, values_b, sizeof(values_a)) == 0);
	}

	return 0;
}

static int
same_animation(const struct mh_scene *a, const struct mh_scene *b)
{
	size_t i;

	CHECK(a->object_count == b->object_count &&
		a->key_count == b->key_count);
	CHECK(a->frames_per_second == b->frames_per_second);

	for (i = 0; i < a->object_count; i++)
		CHECK(same_motion(&a->objects[i], &b->objects[i]) == 0);

	return 0;
}

static int
same_scene(const struct mh_race_scene *a, const struct mh_race_scene *b)
{
	const struct mh_software_scene_assets *sa, *sb;
	size_t i, frame;

	CHECK(same_world(&a->assets.lighting_world,
		&b->assets.lighting_world) == 0);
	CHECK(a->assets.profile == b->assets.profile);
	CHECK(memcmp(&a->assets.materials, &b->assets.materials,
		sizeof(a->assets.materials)) == 0);
	CHECK(same_road_queries(a, b) == 0);
	CHECK(same_collision(&a->assets.world_collision,
		&b->assets.world_collision) == 0);

	for (i = 0; i < MH_RACE_MAX_CARS; i++)
		CHECK(same_collision(&a->assets.car_hulls[i],
			&b->assets.car_hulls[i]) == 0);

	for (i = 0; i < MH_DRIVE_MESHES + MH_CAR_MESHES; i++)
		CHECK(same_mesh(&a->assets.meshes[i], &b->assets.meshes[i]) ==
			0);

	for (i = 0; i < MH_FINISH_SIGNS; i++)
		CHECK(same_mesh(&a->assets.finish.meshes[i],
			&b->assets.finish.meshes[i]) == 0);

	CHECK(same_motion(&a->assets.route.motion, &b->assets.route.motion) ==
		0);
	CHECK(same_motion(&a->assets.finish.motion, &b->assets.finish.motion) ==
		0);
	CHECK(same_animation(
		&a->assets.objects.scene, &b->assets.objects.scene) == 0);

	for (i = 0; i < 2; i++)
		CHECK(same_motion(&a->assets.start_camera[i],
			&b->assets.start_camera[i]) == 0);

	CHECK(a->assets.camera_motion.count == b->assets.camera_motion.count);

	for (i = 0; i < a->assets.camera_motion.count; i++) {
		const struct mh_camera_key *first, *second;

		first = &a->assets.camera_motion.keys[i];
		second = &b->assets.camera_motion.keys[i];
		CHECK(first->frame == second->frame &&
			first->selector == second->selector);
		CHECK(first->focal == second->focal &&
			first->far_factor == second->far_factor);
		CHECK(first->distance_cap == second->distance_cap);
		CHECK(memcmp(first->anchor, second->anchor,
			sizeof(first->anchor)) == 0);
		CHECK(memcmp(first->parameter, second->parameter,
			sizeof(first->parameter)) == 0);
	}

	CHECK(a->assets.ai_path.count == b->assets.ai_path.count);

	for (i = 0; i < a->assets.ai_path.count; i++) {
		CHECK(a->assets.ai_path.points[i].x ==
			b->assets.ai_path.points[i].x);
		CHECK(a->assets.ai_path.points[i].z ==
			b->assets.ai_path.points[i].z);
	}

	CHECK(memcmp(a->assets.retro, b->assets.retro,
		sizeof(*a->assets.retro)) == 0);
	CHECK(a->assets.texture_atlas.count == b->assets.texture_atlas.count);

	for (i = 0; i < a->assets.texture_atlas.count; i++)
		CHECK(same_image(&a->assets.texture_atlas.pages[i],
			&b->assets.texture_atlas.pages[i]) == 0);

	if (a->assets.profile != MH_RENDER_SOFTWARE)
		return 0;

	sa = &a->assets.software;
	sb = &b->assets.software;
	CHECK(same_image(&sa->sky, &sb->sky) == 0);
	CHECK(same_indexed(&sa->lens, &sb->lens) == 0);

	for (i = 0; i < 3; i++)
		CHECK(same_indexed(&sa->environment[i], &sb->environment[i]) ==
			0);

	for (i = 0; i < MH_PARTICLE_ANIMATIONS; i++) {
		CHECK(sa->particles.animations[i].count ==
			sb->particles.animations[i].count);

		for (frame = 0; frame < sa->particles.animations[i].count;
			frame++)
			CHECK(same_indexed(&sa->particles.animations[i]
						.frames[frame],
				&sb->particles.animations[i]
					.frames[frame]) == 0);
	}

	CHECK(sa->track_halos.count == sb->track_halos.count);

	for (i = 0; i < sa->track_halos.count; i++)
		CHECK(same_indexed(&sa->track_halos.images[i],
			&sb->track_halos.images[i]) == 0);

	for (i = 0; i < 2; i++) {
		CHECK(sa->car_halos[i].count == sb->car_halos[i].count);

		for (frame = 0; frame < sa->car_halos[i].count; frame++)
			CHECK(same_indexed(&sa->car_halos[i].images[frame],
				&sb->car_halos[i].images[frame]) == 0);
	}

	return 0;
}

static void
free_assets(struct loaded_assets *assets)
{
	unsigned int i;

	for (i = 0; i < SCENE_PROFILES; i++)
		mh_race_scene_free(&assets->scenes[i]);

	free(assets->hud.atlas.image.rgba);
	mh_software_hud_assets_free(&assets->software_hud);
	mh_ambient_assets_free(&assets->ambient);
	mh_menu_sound_assets_free(&assets->menu);
	mh_menu_horn_assets_free(&assets->menu_horns);
	mh_pause_sound_assets_free(&assets->pause);
	mh_race_sound_assets_free(&assets->race);
	mh_surface_assets_free(&assets->surfaces);
	mh_mesh_free(&assets->car);
	mh_mesh_free(&assets->glide);
	mh_mesh_free(&assets->software);
	mh_object_assets_free(&assets->objects);
	mh_world_free(&assets->world);
	free(assets->archive_file.data);
	free(assets);
}

/*
 * Convert only scene/start/finish references; route/camera readers stay
 * separate.
 */
static int
convert_animations(struct snapshot *snapshot, const struct mh_track *track,
	struct mh_test_animation_source *animations)
{
	const struct mh_resource_source *resources;
	char references[4][1024], path[1024];
	size_t i, j, entries[4];

	strcpy(references[0], track->scene_file);
	sprintf(references[1], "%s/%s", track->base_path,
		track->start_camera[0]);
	sprintf(references[2], "%s/%s", track->base_path,
		track->start_camera[1]);
	strcpy(references[3], "Data/Finish/finish.mot");
	CHECK(mh_memory_resources_open(snapshot->entries, snapshot->count,
		&resources, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 4; i++) {
		CHECK(mh_asset_path_from(resources, "memory", references[i],
			path, sizeof(path), NULL) == MH_ERROR_NONE);

		for (j = 0; j < snapshot->count; j++) {
			if (strcmp(snapshot->entries[j].path, path) != 0)
				continue;

			entries[i] = j;
			break;
		}

		CHECK(j != snapshot->count);
	}

	mh_memory_resources_close(resources);

	/* Lookup borrows the old names; close it before replacing them. */
	for (i = 0; i < 4; i++)
		CHECK(mh_test_animation_convert(animations,
			&snapshot->entries[entries[i]], i != 0,
			NULL) == MH_ERROR_NONE);

	return 0;
}

static int
convert_navigation(struct snapshot *snapshot, const struct mh_track *track,
	struct mh_test_navigation_source *navigation)
{
	const struct mh_resource_source *resources;
	char references[MH_TEST_NAVIGATION_KINDS][1024], path[1024];
	size_t i, j, entries[MH_TEST_NAVIGATION_KINDS], length;

	strcpy(references[MH_TEST_ROUTE], track->spline_file);
	strcpy(references[MH_TEST_CAMERA], track->camera_file);
	length = strlen(track->ai_path);
	CHECK(length != 0);
	sprintf(references[MH_TEST_AI], "%s%sai/ai.dat", track->ai_path,
		track->ai_path[length - 1] == '/' ||
				track->ai_path[length - 1] == '\\'
			? ""
			: "/");
	CHECK(mh_memory_resources_open(snapshot->entries, snapshot->count,
		&resources, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_TEST_NAVIGATION_KINDS; i++) {
		CHECK(mh_asset_path_from(resources, "memory", references[i],
			path, sizeof(path), NULL) == MH_ERROR_NONE);

		for (j = 0; j < snapshot->count; j++) {
			if (strcmp(snapshot->entries[j].path, path) != 0)
				continue;

			entries[i] = j;
			break;
		}

		CHECK(j != snapshot->count);
	}

	mh_memory_resources_close(resources);

	for (i = 0; i < MH_TEST_NAVIGATION_KINDS; i++)
		CHECK(mh_test_navigation_convert(navigation,
			&snapshot->entries[entries[i]],
			(enum mh_test_navigation_kind)i,
			NULL) == MH_ERROR_NONE);

	return 0;
}

static int
convert_profiles(struct snapshot *snapshot, struct mh_test_profile_source *data)
{
	struct mh_resource_entry *entry;
	const char *extension;
	size_t i, retained, removed, length;

	retained = removed = 0;

	for (i = 0; i < snapshot->count; i++) {
		entry = &snapshot->entries[i];
		extension = strrchr(entry->path, '.');

		if (extension != NULL && mh_ascii_equal(extension, ".adp")) {
			free((void *)entry->path);
			free((void *)entry->data);
			removed++;
		} else
			snapshot->entries[retained++] = *entry;
	}

	snapshot->count = retained;
	CHECK(removed == data->catalog.count);

	for (i = 0; i < data->catalog.count; i++) {
		length = strlen(data->catalog.files[i]);
		CHECK(length >= 4);
		memcpy(data->aliases[i], data->catalog.files[i], length - 4);
		data->aliases[i][length - 4] = 0;
		sprintf(data->catalog.files[i], "drivers/%lu.converted",
			(unsigned long)i);
	}

	return 0;
}

static int
remove_horn_files(struct snapshot *snapshot, unsigned int expected)
{
	struct mh_resource_entry *entry;
	const char *extension;
	size_t i, retained, removed;

	retained = removed = 0;

	for (i = 0; i < snapshot->count; i++) {
		entry = &snapshot->entries[i];
		extension = strrchr(entry->path, '.');

		if (extension != NULL && mh_ascii_equal(extension, ".chf")) {
			free((void *)entry->path);
			free((void *)entry->data);
			removed++;
		} else
			snapshot->entries[retained++] = *entry;
	}

	snapshot->count = retained;
	CHECK(removed == expected);

	return 0;
}

static int
remove_surface_files(struct snapshot *snapshot)
{
	struct mh_resource_entry *entry;
	const char *extension;
	size_t i, retained, removed;

	retained = removed = 0;

	for (i = 0; i < snapshot->count; i++) {
		entry = &snapshot->entries[i];
		extension = strrchr(entry->path, '.');

		if (extension != NULL &&
			(mh_ascii_equal(extension, ".mat") ||
				mh_ascii_equal(extension, ".srf") ||
				mh_ascii_equal(extension, ".esd"))) {
			free((void *)entry->path);
			free((void *)entry->data);
			removed++;
		} else
			snapshot->entries[retained++] = *entry;
	}

	snapshot->count = retained;
	CHECK(removed == 3);

	return 0;
}

static void
remove_catalog_files(struct snapshot *snapshot)
{
	struct mh_resource_entry *entry;
	const char *extension;
	size_t i, retained;

	retained = 0;

	for (i = 0; i < snapshot->count; i++) {
		entry = &snapshot->entries[i];
		extension = strrchr(entry->path, '.');

		if (mh_ascii_equal_n(entry->path, "memory/Game/", 12) &&
			extension != NULL &&
			(mh_ascii_equal(extension, ".car") ||
				mh_ascii_equal(extension, ".trk"))) {
			free((void *)entry->path);
			free((void *)entry->data);
		} else
			snapshot->entries[retained++] = *entry;
	}

	snapshot->count = retained;
}

static int
remove_retro_files(struct snapshot *snapshot)
{
	size_t i, retained, removed;
	struct mh_resource_entry *entry;
	int part;

	retained = removed = 0;

	for (i = 0; i < snapshot->count; i++) {
		entry = &snapshot->entries[i];
		part = mh_ascii_equal_n(entry->path, "memory/Data/Retro/", 18);

		if (part || mh_ascii_equal(entry->path, "memory/motor.exe")) {
			free((void *)entry->path);
			free((void *)entry->data);

			if (part)
				removed++;
		} else
			snapshot->entries[retained++] = *entry;
	}

	snapshot->count = retained;
	CHECK(removed == MH_RETRO_PART_COUNT);

	return 0;
}

int
main(int argc, char *argv[])
{
	static const struct mh_resource_ops ops = {
		capture_read, capture_size, capture_list, NULL, NULL};

	struct snapshot snapshot;
	struct mh_test_catalog_source catalog_data;
	struct mh_test_surface_source surface_data;
	struct mh_test_horn_source horn_data;
	struct mh_test_profile_source *profile_data;
	struct mh_test_retro_source *retro_data;
	struct mh_retro_source retro;
	struct mh_profile_source profiles;
	struct mh_horn_source horns;
	struct mh_material_source materials;
	struct mh_env_sound_source environment;
	struct mh_catalog_source catalogs;
	struct mh_test_collision_source collisions;
	struct mh_test_geometry_source geometry;
	struct mh_test_atlas_source atlas_maps;
	struct mh_test_animation_source animation_data;
	struct mh_test_navigation_source navigation_data;
	struct mh_navigation_source navigation;
	struct mh_animation_source animations;
	struct mh_atlas_source atlases;
	struct mh_model_decoder models;
	struct mh_world_decoder worlds;
	struct mh_collision_source decoder;
	struct mh_resource_source recording;
	const struct mh_resource_source *memory;
	struct loaded_assets *native, *copied;
	size_t i;

	CHECK(argc == 2 || (argc == 3 && strcmp(argv[2], "--padded") == 0));
	memset(&snapshot, 0, sizeof(snapshot));
	memset(&catalog_data, 0, sizeof(catalog_data));
	memset(&surface_data, 0, sizeof(surface_data));
	memset(&horn_data, 0, sizeof(horn_data));
	mh_test_horn_source_init(&horn_data, &horns);
	mh_test_surface_sources_init(&surface_data, &materials, &environment);
	mh_test_catalog_source_init(&catalog_data, &catalogs);
	memset(&collisions, 0, sizeof(collisions));
	memset(&geometry, 0, sizeof(geometry));
	memset(&atlas_maps, 0, sizeof(atlas_maps));
	memset(&animation_data, 0, sizeof(animation_data));
	memset(&navigation_data, 0, sizeof(navigation_data));
	mh_test_navigation_source_init(&navigation_data, &navigation);
	mh_test_animation_source_init(&animation_data, &animations);
	mh_test_atlas_source_init(&atlas_maps, &atlases);
	mh_test_geometry_decoders(&geometry, &models, &worlds);
	mh_test_collision_source_init(&collisions, &decoder);
	profile_data = calloc(1, sizeof(*profile_data));
	CHECK(profile_data != NULL);
	mh_test_profile_source_init(profile_data, &profiles);
	snapshot.root = argv[1];
	native = calloc(1, sizeof(*native));
	copied = calloc(1, sizeof(*copied));
	CHECK(native != NULL && copied != NULL);
	CHECK(mh_resource_source_init(&recording, &ops, &snapshot, NULL) ==
		MH_ERROR_NONE);
	CHECK(load_assets(mh_retro_fallback_source(),
		mh_profile_fallback_source(), mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(),
		mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), &recording, argv[1],
		argc == 3, native) == 0);
	CHECK(snapshot.count != 0);
	retro_data = calloc(1, sizeof(*retro_data));
	CHECK(retro_data != NULL);
	retro_data->assets = *native->scenes[0].assets.retro;
	mh_test_retro_source_init(retro_data, &retro);
	CHECK(remove_retro_files(&snapshot) == 0);
	CHECK(mh_car_catalog_load_from(mh_catalog_fallback_source(), &recording,
		argv[1], &catalog_data.cars, NULL) == MH_ERROR_NONE);
	CHECK(mh_track_catalog_load_from(mh_catalog_fallback_source(),
		&recording, argv[1], &catalog_data.tracks,
		NULL) == MH_ERROR_NONE);
	remove_catalog_files(&snapshot);
	CHECK(mh_profile_catalog_read_from(mh_profile_fallback_source(),
		&recording, argv[1], &profile_data->catalog,
		NULL) == MH_ERROR_NONE);
	CHECK(convert_profiles(&snapshot, profile_data) == 0);
	horn_data.catalog = native->horns;
	CHECK(remove_horn_files(&snapshot, horn_data.catalog.count) == 0);
	surface_data.materials = native->surfaces.bank.materials;
	CHECK(mh_env_sounds_load_from(mh_env_sounds_esd_source(), &recording,
		argv[1], native->track.env_sound_file,
		&surface_data.environment, NULL) == MH_ERROR_NONE);
	CHECK(remove_surface_files(&snapshot) == 0);
	strcpy(catalog_data.tracks.entries[0].materials_file,
		MH_TEST_MATERIAL_REFERENCE);
	strcpy(catalog_data.tracks.entries[0].env_sound_file,
		MH_TEST_ENVIRONMENT_REFERENCE);
	CHECK(convert_animations(&snapshot, &native->track, &animation_data) ==
		0);

	for (i = 0; i < snapshot.count; i++) {
		CHECK(mh_test_collision_convert(&collisions,
			&snapshot.entries[i], NULL) == MH_ERROR_NONE);
		CHECK(mh_test_geometry_convert(&geometry, &snapshot.entries[i],
			NULL) == MH_ERROR_NONE);
		CHECK(mh_test_atlas_convert(&atlas_maps, &snapshot.entries[i],
			NULL) == MH_ERROR_NONE);
	}

	CHECK(convert_navigation(&snapshot, &native->track, &navigation_data) ==
		0);
	CHECK(collisions.count != 0);
	CHECK(mh_memory_resources_open(snapshot.entries, snapshot.count,
		&memory, NULL) == MH_ERROR_NONE);
	CHECK(load_assets(&retro, &profiles, &horns, &materials, &environment,
		&catalogs, &navigation, &animations, &atlases, &models,
		&worlds, &decoder, memory, "memory", argc == 3,
		copied) == 0);
	mh_memory_resources_close(memory);
	CHECK(retro_data->reads == SCENE_PROFILES);
	printf("Alternate retro: %u scene loads; prepared retro files and executable removed\n",
		retro_data->reads);
	free(retro_data);
	CHECK(profile_data->catalogs == 1 && profile_data->reads == 2);
	printf("Alternate profiles: %u catalog and %u individual loads; renamed keys and no ADP resources\n",
		profile_data->catalogs, profile_data->reads);
	CHECK(strcmp(copied->profiles.files[0], "drivers/0.converted") == 0);
	free(profile_data);
	CHECK(horn_data.reads == 2);
	printf("Alternate horns: %u selected catalog loads; original CHF resources removed\n",
		horn_data.reads);
	memset(&horn_data, 0, sizeof(horn_data));
	CHECK(catalog_data.car_reads > SCENE_PROFILES &&
		catalog_data.track_reads > SCENE_PROFILES &&
		catalog_data.car_counts >= SCENE_PROFILES);
	printf("Alternate catalogs: %u car and %u track loads; original CAR/TRK resources removed\n",
		catalog_data.car_reads, catalog_data.track_reads);
	mh_test_catalog_source_free(&catalog_data);
	CHECK(surface_data.material_reads == SCENE_PROFILES + 1 &&
		surface_data.environment_reads == 2);
	printf("Alternate definitions: %u material and %u environment loads; renamed references and no MAT/SRF/ESD resources\n",
		surface_data.material_reads, surface_data.environment_reads);
	memset(&surface_data, 0, sizeof(surface_data));
	CHECK(collisions.reads > collisions.count);
	printf("Alternate collision source: %lu decoded assets, %lu selected loads; no COL resources remain\n",
		(unsigned long)collisions.count,
		(unsigned long)collisions.reads);
	mh_test_collision_source_free(&collisions);
	CHECK(geometry.model_count != 0 && geometry.world_count == 2);
	CHECK(geometry.model_reads > geometry.model_count);
	CHECK(geometry.world_reads > geometry.world_count);
	printf("Alternate geometry: %lu models, %lu worlds; %lu model and %lu world loads; no MYO/MYW resources remain\n",
		(unsigned long)geometry.model_count,
		(unsigned long)geometry.world_count,
		(unsigned long)geometry.model_reads,
		(unsigned long)geometry.world_reads);
	mh_test_geometry_source_free(&geometry);
	CHECK(atlas_maps.count == (argc == 3 ? 6 : 3) &&
		atlas_maps.reads > atlas_maps.count);
	printf("Alternate atlas maps: %lu decoded maps, %lu loads; no remap BIN resources remain\n",
		(unsigned long)atlas_maps.count,
		(unsigned long)atlas_maps.reads);
	mh_test_atlas_source_free(&atlas_maps);
	CHECK(animation_data.count == 4 && animation_data.scene_reads > 4 &&
		animation_data.motion_reads >= 12);
	printf("Alternate animation: %lu templates, %lu scene and %lu motion loads; original scene/start/finish resources removed\n",
		(unsigned long)animation_data.count,
		(unsigned long)animation_data.scene_reads,
		(unsigned long)animation_data.motion_reads);
	mh_test_animation_source_free(&animation_data);

	for (i = 0; i < MH_TEST_NAVIGATION_KINDS; i++)
		CHECK(navigation_data.present[i] &&
			navigation_data.reads[i] >= SCENE_PROFILES);

	printf("Alternate navigation: %lu route, %lu camera and %lu AI loads; original navigation resources removed\n",
		(unsigned long)navigation_data.reads[MH_TEST_ROUTE],
		(unsigned long)navigation_data.reads[MH_TEST_CAMERA],
		(unsigned long)navigation_data.reads[MH_TEST_AI]);
	mh_test_navigation_source_free(&navigation_data);

	/*
	 * Prepared bytes and the provider are gone; loaded assets still own
	 * data.
	 */
	for (i = 0; i < snapshot.count; i++) {
		free((void *)snapshot.entries[i].path);
		free((void *)snapshot.entries[i].data);
	}

	CHECK(same_audio(native, copied) == 0);
	CHECK(same_animation(&native->ambient.scene, &copied->ambient.scene) ==
		0);

	for (i = 0; i < SCENE_PROFILES; i++)
		CHECK(same_scene(&native->scenes[i], &copied->scenes[i]) == 0);

	CHECK(same_image(&native->hud.atlas.image, &copied->hud.atlas.image) ==
		0);
	CHECK(same_image(&native->software_hud.atlas.image,
		&copied->software_hud.atlas.image) == 0);
	CHECK(memcmp(native->hud.message_advance, copied->hud.message_advance,
		sizeof(native->hud.message_advance)) == 0);
	CHECK(native->software_hud.font.width ==
			copied->software_hud.font.width &&
		native->software_hud.font.rows ==
			copied->software_hud.font.rows);
	CHECK(memcmp(native->software_hud.font.bytes,
		copied->software_hud.font.bytes,
		(size_t)native->software_hud.font.width *
			native->software_hud.font.rows) == 0);
	CHECK(native->random == copied->random);
	CHECK(memcmp(&native->horns, &copied->horns, sizeof(native->horns)) ==
		0);
	CHECK(native->profiles.count == copied->profiles.count);
	CHECK(memcmp(native->profiles.profiles, copied->profiles.profiles,
		sizeof(native->profiles.profiles)) == 0);
	CHECK(strcmp(native->car_name, copied->car_name) == 0);
	CHECK(strcmp(native->track.world_file, copied->track.world_file) == 0);
	CHECK(same_mesh(&native->car, &copied->car) == 0);
	CHECK(same_mesh(&native->glide, &copied->glide) == 0);
	CHECK(same_mesh(&native->software, &copied->software) == 0);
	CHECK(same_world(&native->world, &copied->world) == 0);

	CHECK(native->objects.count == copied->objects.count);
	CHECK(native->objects.model_count == copied->objects.model_count);

	for (i = 0; i < native->objects.model_count; i++)
		CHECK(same_collision(&native->objects.models[i].hull,
			&copied->objects.models[i].hull) == 0);

	free_assets(native);
	free_assets(copied);
	printf("Prepared memory matches filesystem catalogs, scenes, HUD/results and sound banks (%lu resources)\n",
		(unsigned long)snapshot.count);

	return 0;
}
