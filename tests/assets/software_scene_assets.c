#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "backends/render/software_provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/race/internal.h"
#include "client/session/race_context.h"
#include "engine/core/text.h"
#include "interfaces/render/backend.h"
#include "interfaces/storage/resource_backend.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_context race_context;
static struct mh_resource_source resources;
static int software_only, fail_lens, fail_font;
static unsigned int forbidden_reads, club_overlays;

static int
path_contains(const char *path, const char *part)
{
	size_t length;

	length = strlen(part);

	for (; *path != 0; path++)
		if (mh_ascii_equal_n(path, part, length))
			return 1;

	return 0;
}

static enum mh_error_code
read_resource(void *unused, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	(void)unused;

	if (software_only &&
		(path_contains(path, "/hardware/") ||
			path_contains(path, "/d3d/") ||
			path_contains(path, "/tex2.pdi") ||
			path_contains(path, "/remap") ||
			path_contains(path, "/hwhex.iff") ||
			path_contains(path, "/halofix.iff"))) {
		forbidden_reads++;
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"software requested hardware resources");
	}

	if (fail_lens && path_contains(path, "/hex.iff"))
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"injected missing software lens");

	if (fail_font && path_contains(path, "/font2.fnt"))
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"injected missing notification font");

	return mh_resource_source_read_bounded(
		mh_filesystem_resources(), path, limit, output, error_out);
}

static enum mh_error_code
resource_size(void *unused, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	(void)unused;

	return mh_resource_source_size(
		mh_filesystem_resources(), path, output, error_out);
}

static enum mh_error_code
list_resources(void *unused, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	(void)unused;

	return mh_resource_source_list(
		mh_filesystem_resources(), path, visit, visitor, error_out);
}

static int
has_text(const struct mh_ui_list *list, const char *value)
{
	const struct mh_ui_word_text *text;
	unsigned int i;

	for (i = 0; i < list->text_count; i++) {
		text = &list->texts[i];

		if (text->length == strlen(value) &&
			memcmp(list->text + text->offset, value,
				text->length) == 0)
			return 1;
	}

	return 0;
}

static int
check_overlays(struct mh_renderer *renderer, unsigned int cars)
{
	static struct mh_race_loop loop;
	static struct mh_race_history history;
	static struct mh_field field;
	static struct mh_messages messages;
	unsigned int i;

	memset(&loop, 0, sizeof(loop));
	memset(&history, 0, sizeof(history));
	memset(&field, 0, sizeof(field));
	mh_messages_init(&messages);
	loop.context = &race_context;
	loop.history = &history;
	loop.field = &field;
	loop.width = 320;
	loop.height = 200;
	loop.success = loop.running = 1;
	loop.hud.info_detail = 2;
	loop.hud.lap = 1;
	loop.hud.laps = 3;
	loop.hud.position = 1;
	loop.hud.cars = cars;
	loop.hud.speed = 123;
	loop.hud.revs = .5;
	loop.hud.gear = 3;
	loop.hud.start = MH_START_THREE;
	loop.hud.gap_mode = 2;
	loop.hud.gap_count = cars;
	loop.hud.info.count = cars;
	loop.hud.map_mode = 2;
	loop.hud.marker_count = cars;
	strcpy(loop.hud.info.track, race_context.scene.assets.track.name);

	for (i = 0; i < cars; i++) {
		loop.hud.gaps[i].position = i + 1;
		loop.hud.gaps[i].color = 0xffcc88UL;
		loop.hud.gaps[i].milliseconds = 1234 * (long)i;
		sprintf(loop.hud.gaps[i].name, "Driver %u", i + 1);
		strcpy(loop.hud.info.rows[i].name, loop.hud.gaps[i].name);
		strcpy(loop.hud.info.rows[i].team, "SWE");
		loop.hud.info.rows[i].position = i + 1;
		loop.hud.info.rows[i].control_type = 1;
		loop.hud.info.rows[i].color = 0xffcc88UL;
		loop.hud.markers[i].active = 1;
		loop.hud.markers[i].viewed = i == 0;
		loop.hud.markers[i].rank = i + 1;
		loop.hud.markers[i].color = 0xffcc88UL;
		loop.hud.markers[i].world[0] =
			race_context.software_hud.map_info[0];
		loop.hud.markers[i].world[2] =
			race_context.software_hud.map_info[2];
	}

	for (i = 0; i < MH_MESSAGE_VISIBLE_ROWS; i++) {
		sprintf(messages.rows[i].text, "Notification %u", i);
		messages.rows[i].color = 0x88aaffUL;
	}

	loop.hud.messages = &messages;
	loop.hud.message_ms = 1000;
	loop.hud.chat_text = "Race chat";
	loop.hud.chat_ms = 251;
	field.attract = 1;
	loop.attract_frame.label = 1;

	if (mh_ascii_equal(race_context.scene.assets.track.name, "RuhrStadt")) {
		history.software_hud.visits = 201;
		loop.hud.markers[0].world[0] = 476;
		loop.hud.markers[0].world[2] = -188;
	}

	CHECK(mh_race_build_overlay(&loop));
	CHECK(loop.ui.count != 0 && has_text(&loop.ui, "DEMO"));

	if (mh_ascii_equal(race_context.scene.assets.track.name, "RuhrStadt")) {
		club_overlays++;
		CHECK(history.software_hud.visits == 202);
		CHECK(history.software_hud.club_credits);
		CHECK(has_text(&loop.ui, "WELCOME TO TBL CLUB"));
	}

	CHECK(has_text(&loop.ui, "Race chat"));
	CHECK(mh_renderer_ui_frame(renderer, &loop.ui));

	loop.hud.show_info = 1;
	CHECK(mh_race_build_overlay(&loop));
	CHECK(loop.ui.count == 0 && has_text(&loop.ui, "Pos:"));
	CHECK(!has_text(&loop.ui, "DEMO"));
	CHECK(has_text(&loop.ui, "Race chat"));
	CHECK(cars != 8 || loop.ui.text_count > 128);
	CHECK(mh_renderer_ui_frame(renderer, &loop.ui));

	loop.hud.show_info = 0;
	loop.menu.visible = 1;
	loop.menu_output.display.distance = 100;
	loop.menu_output.display.perspective = 16;
	CHECK(mh_race_build_overlay(&loop));
	CHECK(loop.ui.count == 0 && !has_text(&loop.ui, "Pos:"));
	CHECK(!has_text(&loop.ui, "DEMO"));
	CHECK(has_text(&loop.ui, "Race chat"));
	CHECK(mh_renderer_ui_frame(renderer, &loop.ui));

	loop.menu.visible = 0;
	field.attract = 0;
	loop.hud.paused = 1;
	CHECK(mh_race_build_overlay(&loop));
	CHECK(has_text(&loop.ui, "PAUSED"));
	CHECK(mh_renderer_ui_frame(renderer, &loop.ui));
	CHECK(loop.success && loop.running);

	return 0;
}

static int
check_scene(const char *root, unsigned int track, unsigned int car,
	unsigned int opponents, unsigned int detail,
	enum mh_render_profile profile)
{
	struct mh_av_settings settings;
	struct mh_race_scene *scene;
	const struct mh_mesh *meshes[MH_FIELD_MESHES];
	const struct mh_indexed_image *maps[3];
	struct mh_renderer *renderer;
	struct mh_world_view view;
	struct mh_standing_sample samples[MH_TRAFFIC_MAX_CARS];
	struct mh_standings standings;
	static struct mh_ui_list list;
	struct mh_error error;
	unsigned int i, mesh_count;
	enum mh_error_code status;

	CHECK(mh_av_settings_init(&settings, 255, 255, NULL) == MH_ERROR_NONE);
	settings.width = 320;
	settings.height = 200;
	settings.car_detail = detail;
	settings.track_detail = detail != 0;
	mesh_count = MH_DRIVE_MESHES + opponents * MH_CAR_MESHES;
	software_only = profile == MH_RENDER_SOFTWARE;
	status = mh_race_context_load_profile_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), mh_sprite_spr_source(),
		mh_font_fallback_source(), mh_image_pdi_source(),
		mh_image_fallback_decoder(), mh_image_tga_portrait_decoder(),
		&resources, root, car, track, 1, &settings, profile,
		&race_context, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "track %u: %s\n", track, error.message);

	CHECK(status == MH_ERROR_NONE);
	scene = &race_context.scene;
	CHECK(scene->assets.profile == profile &&
		race_context.settings == &settings);
	CHECK(mh_race_scene_add_opponents_from(&resources, root, car, track,
		opponents, scene, &error) == MH_ERROR_NONE);
	mh_race_scene_reset_particles(scene);
	CHECK(scene->visuals.particle_system.hardware == !software_only);
	CHECK(mh_race_context_load_hud(root, &race_context, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_race_context_load_map(root, &race_context, &error) ==
		MH_ERROR_NONE);
	CHECK(mh_results_assets_load_from(mh_profile_fallback_source(),
		mh_font_fallback_source(), mh_image_fallback_decoder(),
		mh_image_tga_portrait_decoder(), &resources, root,
		opponents + 1, "Player",
		mh_race_context_atlas(&race_context),
		&race_context.results, &error) == MH_ERROR_NONE);
	CHECK(race_context.hud.message_advance['A'] != 0);

	if (profile == MH_RENDER_GLIDE) {
		CHECK(scene->assets.texture_atlas.pages != NULL);
		CHECK(scene->assets.software.sky.rgba == NULL);
		mh_race_context_free(&race_context);
		return 0;
	}

	CHECK(forbidden_reads == 0);
	CHECK(race_context.hud.atlas.image.rgba == NULL);
	CHECK(mh_race_context_atlas(&race_context) ==
		&race_context.software_hud.atlas);
	CHECK(race_context.software_hud.map.width != 0);
	CHECK(race_context.software_hud.font.bytes != NULL);

	CHECK(scene->assets.texture_atlas.pages == NULL);
	CHECK(scene->visuals.particle_mesh.vertices == NULL);
	CHECK(scene->visuals.sky_mesh.vertices == NULL);
	CHECK(scene->visuals.skid_mesh.vertices == NULL);
	CHECK(scene->visuals.shadow_mesh.vertices != NULL);
	CHECK(scene->assets.software.sky.rgba != NULL);
	CHECK(scene->assets.software.lens.width == 32);
	CHECK(scene->assets.software.particles.animations[0].count == 17);
	CHECK(scene->assets.software.particles.animations[3].count == 20);

	for (i = 0; i <= opponents; i++)
		CHECK(scene->assets.software.car_halos[i].count > 0);

	for (i = 0; i < mesh_count; i++) {
		meshes[i] = &scene->assets.meshes[i];
		CHECK(meshes[i]->atlas == NULL && meshes[i]->atlas_uv == NULL);
		CHECK(meshes[i]->vertices != NULL);
	}

	for (i = 0; i < 3; i++) {
		maps[i] = &scene->assets.software.environment[i];
		CHECK(maps[i]->indices != NULL);
	}

	memset(&view, 0, sizeof(view));
	view.profile = MH_RENDER_SOFTWARE;
	view.end[0] = scene->assets.lighting_world.width;
	view.end[1] = scene->assets.lighting_world.height;
	CHECK(mh_mesh_world_cull(&scene->assets.meshes[0], &view, &error) ==
		MH_ERROR_NONE);
	status = mh_renderer_open(mh_software_backend(), NULL, meshes,
		mesh_count, 320, 200, 0, &renderer, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "renderer: %s\n", error.message);

	CHECK(status == MH_ERROR_NONE);
	CHECK(mh_renderer_set_environment(renderer, maps, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_ui_upload(
		renderer, mh_race_context_atlas(&race_context)));
	memset(samples, 0, sizeof(samples));

	for (i = 0; i <= opponents; i++) {
		samples[i].finished = 1;
		samples[i].total_ms = 120000 + 1234 * i;
		samples[i].best_lap = 1;
		samples[i].best_ms = 60000 + 100 * i;
	}

	CHECK(mh_standings_rank(samples, opponents + 1, &standings, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_results_draw(&race_context.results,
		mh_race_context_atlas(&race_context), &standings, 320, 200,
		NULL, &list));
	CHECK(mh_renderer_ui_frame(renderer, &list));
	CHECK(check_overlays(renderer, opponents + 1) == 0);
	mh_renderer_destroy(renderer);
	mh_race_context_free(&race_context);
	CHECK(race_context.scene.assets.software.sky.rgba == NULL);
	CHECK(race_context.software_hud.atlas.image.rgba == NULL);
	CHECK(race_context.software_hud.font.bytes == NULL);

	return 0;
}

int
main(int argc, char *argv[])
{
	static const struct mh_resource_ops ops = {
		read_resource, resource_size, list_resources, NULL, NULL};

	struct mh_error error;
	enum mh_error_code status;
	unsigned int track;

	CHECK(argc == 2);
	CHECK(mh_resource_source_init(&resources, &ops, NULL, NULL) ==
		MH_ERROR_NONE);

	for (track = 1; track <= 8; track++)
		CHECK(check_scene(argv[1], track, track, 7, 2,
			MH_RENDER_SOFTWARE) == 0);

	CHECK(club_overlays != 0);
	CHECK(check_scene(argv[1], 1, 10, 0, 0, MH_RENDER_SOFTWARE) == 0);

	/*
	 * A late resource failure frees already loaded geometry and effect
	 * banks.
	 */
	fail_lens = 1;
	status = mh_race_context_load_profile_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), mh_sprite_spr_source(),
		mh_font_fallback_source(), mh_image_pdi_source(),
		mh_image_fallback_decoder(), mh_image_tga_portrait_decoder(),
		&resources, argv[1], 1, 1, 1, NULL, MH_RENDER_SOFTWARE,
		&race_context, &error);
	CHECK(status == MH_ERROR_NOT_FOUND);
	CHECK(race_context.scene.assets.meshes[0].vertices == NULL);
	CHECK(race_context.scene.assets.software.car_halos[0].count == 0);
	CHECK(race_context.scene.assets.software.sky.rgba == NULL);
	CHECK(race_context.software_hud.atlas.image.rgba == NULL);
	CHECK(race_context.software_hud.font.bytes == NULL);

	mh_race_context_free(&race_context);
	fail_lens = 0;
	status = mh_race_context_load_profile_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), mh_sprite_spr_source(),
		mh_font_fallback_source(), mh_image_pdi_source(),
		mh_image_fallback_decoder(), mh_image_tga_portrait_decoder(),
		&resources, argv[1], 1, 1, 0, NULL, MH_RENDER_SOFTWARE,
		&race_context, &error);
	CHECK(status == MH_ERROR_NONE);
	race_context.scene.assets.view_width = 320;
	fail_font = 1;
	CHECK(mh_race_context_load_hud(argv[1], &race_context, &error) ==
		MH_ERROR_NOT_FOUND);
	CHECK(race_context.software_hud.atlas.image.rgba == NULL);
	CHECK(race_context.software_hud.font.bytes == NULL);
	fail_font = 0;
	CHECK(mh_race_context_load_hud(argv[1], &race_context, &error) ==
		MH_ERROR_NONE);
	mh_race_context_free(&race_context);
	CHECK(check_scene(argv[1], 1, 1, 7, 2, MH_RENDER_GLIDE) == 0);
	puts("Software race resources load eight tracks and full fields without hardware assets");

	return 0;
}
