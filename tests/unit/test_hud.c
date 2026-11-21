#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/render/scene_matrices.h"
#include "engine/ui/hud.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
test_label(void)
{
	static struct mh_hud_assets assets;
	static struct mh_ui_list list;

	assets.atlas.image.width = assets.atlas.image.height = 32;
	assets.hardware_glyphs['A'].width = 5;
	assets.hardware_glyphs['A'].height = 8;
	assets.hardware_glyphs['B'] = assets.hardware_glyphs['A'];
	assets.hardware_advance['A'] = 6;
	assets.hardware_advance['B'] = 8;
	mh_ui_begin(&list, 320, 200);
	CHECK(mh_hud_label(&assets, "AB", 4, &list));
	CHECK(list.count == 24);

	/* Four quads: shadow and white text, advanced by glyph metrics. */
	CHECK(list.vertices[0].position[0] == 154);
	CHECK(list.vertices[0].position[1] == 5);
	CHECK(list.vertices[0].color[0] == 0);
	CHECK(list.vertices[6].position[0] == 153);
	CHECK(list.vertices[6].position[1] == 4);
	CHECK(list.vertices[6].color[0] == 1);
	CHECK(list.vertices[12].position[0] == 160);
	CHECK(list.vertices[18].position[0] == 159);
	CHECK(!mh_hud_label(NULL, "AB", 4, &list));
	CHECK(!mh_hud_label(&assets, NULL, 4, &list));
	CHECK(!mh_hud_label(&assets, "AB", 4, NULL));
}

static void
test_status(void)
{
	static struct mh_hud_assets assets;
	static struct mh_ui_list list;
	struct mh_hud_state state;
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	memset(&state, 0, sizeof(state));
	assets.atlas.image.width = assets.atlas.image.height = 1024;

	for (i = 0; i < MH_HUD_SPRITES; i++)
		assets.sprites[i].width = assets.sprites[i].height = 12;

	state.info_detail = 1;
	state.start = MH_START_RACING;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &list));
	i = list.count;
	state.status_flags = mh_hud_status_flags(0, 3, 1, 1);
	CHECK(state.status_flags == 3);
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &list) &&
		list.count == i + 12);
	CHECK(list.vertices[i].position[0] == 620 &&
		list.vertices[i].position[1] == 460);
	CHECK(list.vertices[i + 6].position[0] == 604 &&
		list.vertices[i + 6].position[1] == 460);
	state.clock_ms = 200;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &list) && list.count == i);
	state.clock_ms = 400;
	state.info_detail = 0;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &list) && list.count == 0);
	state.info_detail = 1;
	state.paused = 1;
	state.finished = 1;
	state.status_flags = mh_hud_status_flags(state.status_flags, 3, 0, 1);
	CHECK(state.status_flags == 2);
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_hud_status_draw(&assets, &state, &list) && list.count == 6);
	state.status_flags = 3;
	list.count = MH_UI_MAX_QUADS * 6 - i - 6;
	CHECK(!mh_hud_append(&assets, &state, 640, 480, &list) &&
		list.count == MH_UI_MAX_QUADS * 6 - i - 6);
	mh_ui_begin(&list, 640, 480);
	CHECK(!mh_hud_status_draw(NULL, &state, &list) && list.count == 0);
	list.height = 199;
	CHECK(!mh_hud_status_draw(&assets, &state, &list) && list.count == 0);
}

static void
prepare_gap_assets(struct mh_hud_assets *assets)
{
	unsigned int i;

	memset(assets, 0, sizeof(*assets));
	assets->atlas.image.width = assets->atlas.image.height = 4096;
	assets->hardware_height = 12;

	for (i = 0; i < 256; i++) {
		assets->hardware_advance[i] = i % 7 + 4;
		assets->hardware_glyphs[i].x = i * 16;
		assets->hardware_glyphs[i].width = i % 7 + 3;
		assets->hardware_glyphs[i].height = 12;
	}
}

static void
test_secrets(void)
{
	struct mh_hud_secrets secrets;
	struct mh_hud_state state;
	struct mh_ui_list list;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	float world[3];
	unsigned int i;
	static const float white[4] = {1, 1, 1, 1};

	memset(&secrets, 0, sizeof(secrets));
	world[0] = 476;
	world[1] = 100;
	world[2] = -188;

	for (i = 0; i < 201; i++) {
		mh_hud_secret_update(&secrets, "RuhrStadt", 2, 1, world);
		CHECK(secrets.notice == 0);
	}

	CHECK(secrets.visits == 201);
	mh_hud_secret_update(&secrets, "RUHRSTADT", 1, 1, world);
	CHECK(secrets.notice == 1 && secrets.visits == 201);
	world[0] = 496;
	mh_hud_secret_update(&secrets, "RuhrStadt", 0, 1, world);
	CHECK(secrets.notice == 1);
	mh_hud_secret_update(&secrets, "RuhrStadt", 2, 0, world);
	CHECK(secrets.notice == 1);
	mh_hud_secret_update(&secrets, "RuhrStadt", 2, 1, world);
	CHECK(secrets.notice == 0 && secrets.visits == 0);
	world[0] = 159;
	world[1] = 12;
	world[2] = -436;
	mh_hud_secret_update(&secrets, "AtLaNtIkA", 2, 1, world);
	CHECK(secrets.notice == 2);
	world[1] = 14;
	mh_hud_secret_update(&secrets, "Atlantika", 2, 1, world);
	CHECK(secrets.notice == 0);
	memset(&state, 0, sizeof(state));
	memset(&secrets, 0, sizeof(secrets));
	state.marker_count = 1;
	state.map_mode = 2;
	state.markers[0].active = state.markers[0].viewed = 1;
	world[1] = 12;
	memcpy(state.markers[0].world, world, sizeof(world));
	mh_hud_secrets_frame(&secrets, "Atlantika", &state, 640, 3);
	CHECK(state.secrets.notice == 0 && secrets.notice == 2);
	mh_hud_secrets_frame(&secrets, "Atlantika", &state, 640, 3);
	CHECK(state.secrets.notice == 2 && state.secrets.scroll_x == 690 &&
		secrets.scroll_x == 689);
	CHECK(secrets.bounce_phase == 10 && secrets.phase_y == 1034 &&
		secrets.phase_x == 10);
	state.map_mode = 0;
	secrets.scroll_x = 0;
	secrets.scroll_offset = 2;
	secrets.phase_x = secrets.phase_y = 4090;
	mh_hud_secrets_frame(&secrets, "Atlantika", &state, 640, 3);
	CHECK(secrets.notice == 2 && secrets.scroll_x == 8 &&
		secrets.scroll_offset == 0 && secrets.phase_x == 54 &&
		secrets.phase_y == 204);
	secrets.notice = 1;
	mh_hud_secrets_frame(&secrets, "other", &state, 640, 3);
	CHECK(secrets.club_credits);
	state.map_mode = 2;
	mh_hud_secrets_frame(&secrets, "other", &state, 640, 3);
	CHECK(secrets.notice == 0 && secrets.club_credits);
	memset(&atlas, 0, sizeof(atlas));
	atlas.image.width = atlas.image.height = 100;
	region.x = 10;
	region.y = 20;
	region.width = region.height = 8;
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_ui_quad_scaled(
		&list, &atlas, &region, -40, -20, 80, 80, white));
	CHECK(list.count == 6 && list.vertices[0].position[0] == 0 &&
		list.vertices[0].position[1] == 0);
	CHECK(list.vertices[0].uv[0] == .14f && list.vertices[0].uv[1] == .22f);
	CHECK(list.vertices[2].position[0] == 40 &&
		list.vertices[2].position[1] == 60);
	CHECK(!mh_ui_quad_scaled(&list, &atlas, &region, 0, 0, -1, 80, white) &&
		list.count == 6);
}

static void
prepare_map_assets(struct mh_hud_assets *assets)
{
	unsigned int i;

	memset(assets, 0, sizeof(*assets));
	assets->atlas.image.width = assets->atlas.image.height = 2048;

	for (i = 0; i < MH_HUD_SPRITES; i++) {
		assets->sprites[i].x = i * 24;
		assets->sprites[i].width = 3;
		assets->sprites[i].height = 5;
	}

	assets->sprites[MH_HUD_MARKER].width = 9;
	assets->sprites[MH_HUD_MARKER].height = 11;
	assets->sprites[MH_HUD_OTHER_MARKER].width = 7;
	assets->sprites[MH_HUD_OTHER_MARKER].height = 8;
	assets->sprites[MH_HUD_AVENGER].width = 17;
	assets->sprites[MH_HUD_AVENGER].height = 24;
	assets->sprites[MH_HUD_MAP_NUMBER].width = 4;
	assets->map.x = MH_HUD_SPRITES * 24;
}

static void
test_draw(void)
{
	struct mh_ui_atlas atlas;
	struct mh_ui_region region, previous;
	struct mh_image image;
	struct mh_ui_list list;
	unsigned char pixels[8 * 8 * 4];
	unsigned int i;
	static const float white[4] = {1, 1, 1, 1};

	memset(pixels, 255, sizeof(pixels));
	image.width = image.height = 8;
	image.rgba = pixels;
	CHECK(mh_ui_atlas_init(20, 20, &atlas, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 4; i++)
		CHECK(mh_ui_atlas_add(&atlas, &image, &region, NULL) ==
			MH_ERROR_NONE);

	CHECK(region.x == 11 && region.y == 11);
	previous = region;
	CHECK(mh_ui_atlas_add(&atlas, &image, &region, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&previous, &region, sizeof(region)) == 0);
	mh_ui_begin(&list, 320, 240);
	CHECK(mh_ui_quad(&list, &atlas, &region, -3, -2, white));
	CHECK(list.count == 6 && list.vertices[0].position[0] == 0 &&
		list.vertices[0].position[1] == 0);
	CHECK(list.vertices[0].uv[0] == 14.0f / 20 &&
		list.vertices[0].uv[1] == 13.0f / 20);
	CHECK(mh_ui_quad(&list, &atlas, &region, 319, 239, white));
	CHECK(list.vertices[8].position[0] == 320 &&
		list.vertices[8].position[1] == 240);
	CHECK(mh_ui_quad(&list, &atlas, &region, 320, 240, white) &&
		list.count == 12);

	for (i = 2; i < MH_UI_MAX_QUADS; i++)
		CHECK(mh_ui_quad(&list, &atlas, &region, 0, 0, white));

	CHECK(!mh_ui_quad(&list, &atlas, &region, 0, 0, white));
	CHECK(list.count == MH_UI_MAX_QUADS * 6);
	free(atlas.image.rgba);
}

static void
test_state(void)
{
	static struct mh_field field;
	struct mh_drive *drive;
	struct mh_hud_state state;

	memset(&field, 0, sizeof(field));
	field.count = 8;
	field.standings.cars[0].position = 4;
	drive = &field.cars[0];
	drive->vehicle.motion.velocity[2] = -50;
	drive->vehicle.speed = 180;
	drive->vehicle.engine.gear = 0;
	drive->vehicle.engine.rpm = 3000;
	drive->config.max_rpm = 6000;
	drive->race.lap = 2;
	drive->race.lap_limit = 5;
	drive->race.now_ms = 86639;
	drive->race.lap_start_ms = 73000;
	drive->race.lap_ms = 13639;
	drive->race.best_ms = 123;
	drive->race.split_hold_ms = 4000;
	drive->race.split_notice = 2;
	drive->race.split_ms = 10000;
	drive->race.last_ms = 73000;
	drive->race.lap_notice = 1;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(state.speed == 180 && state.gear == 0 &&
		state.revs == (double)(float)(3000 * (double).00008f));

	/*
	 * Presentation reads cached speed, not post-integration velocity, and
	 * car tuning does not change the meter's fixed RPM scale.
	 */
	drive->vehicle.motion.velocity[2] = 0;
	drive->config.max_rpm = 12000;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(state.speed == 180 &&
		state.revs == (double)(float)(3000 * (double).00008f));
	CHECK(state.total_ms == 86639 && state.lap_ms == 13639 &&
		state.best_ms == 0);
	CHECK(state.time_notices && state.split_ms == 10000 &&
		state.last_ms == 73000 && state.lap_notice == 1);
	drive->race.split_notice = 0;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(state.split_ms == 0);

	/* The crossing tick's stored lap clock differs from now-start. */
	drive->race.lap_ms = 73000;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(state.lap_ms == 73000);
	CHECK(state.lap == 2 && state.laps == 5 && state.position == 4 &&
		state.cars == 8);
	drive->race.finished = 1;
	drive->race.last_ms = drive->race.lap_ms = 71000;
	drive->race.best_lap = 1;
	drive->vehicle.engine.rpm = 15000;
	field.standings.complete = 1;
	mh_hud_state(&field, 1, &state, NULL);
	CHECK(state.lap_ms == 71000 && state.total_ms == 86639 &&
		state.best_ms == 123);
	CHECK(state.revs == 1 && state.paused && state.finished &&
		state.complete);
	drive->race.lap_limit = 0;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(!state.timed && !state.time_notices);
	field.active_mask = 255;
	field.viewed_entry = 3;
	field.cars[3] = field.cars[0];
	field.cars[3].vehicle.motion.velocity[2] = 10;
	field.cars[3].vehicle.speed = 36;
	field.cars[3].vehicle.engine.gear = 4;
	field.cars[3].race.lap_limit = 7;
	field.standings.cars[3].position = 2;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(state.speed == 36 && state.gear == 4 && state.laps == 7 &&
		state.position == 2);
	field.active_mask &= ~(1U << 3);
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(state.speed == 180 && !state.timed);
}

static void
test_layout(void)
{
	static struct mh_hud_assets assets;
	struct mh_hud_state state;
	struct mh_ui_list small, large;
	struct mh_ui_vertex prefix[6];
	unsigned int i, q, timer, seen;

	memset(&assets, 0, sizeof(assets));
	memset(&state, 0, sizeof(state));
	assets.atlas.image.width = assets.atlas.image.height = 512;

	for (i = 0; i < MH_HUD_SPRITES; i++) {
		assets.sprites[i].x = i * 10;
		assets.sprites[i].width = 6;
		assets.sprites[i].height = 15;
	}

	state.info_detail = 2;
	state.speed = 203;
	state.gear = 6;
	state.timed = 1;
	state.total_ms = 86639;
	state.lap_ms = 13639;
	state.best_ms = 72999;
	state.lap = 2;
	state.laps = 5;
	state.position = 4;
	state.cars = 8;
	state.start = MH_START_RACING;
	state.revs = .5;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &small));
	CHECK(mh_hud_draw(&assets, &state, 1024, 768, &large));
	CHECK(small.count == large.count);
	seen = 0;

	for (q = 0; q < small.count; q += 6) {
		CHECK(small.vertices[q].uv[0] == large.vertices[q].uv[0]);
		CHECK(small.vertices[q].position[1] ==
			large.vertices[q].position[1]);

		if (small.vertices[q].position[0] > 320) {
			CHECK(large.vertices[q].position[0] -
					small.vertices[q].position[0] ==
				384);

			if (small.vertices[q].position[1] == 30) {
				/*
				 * Right-to-left 01:26:63; no rounding at 9ms
				 * remainder.
				 */
				static const unsigned int expected[8] = {3, 6,
					MH_HUD_COLON, 6, 2, MH_HUD_COLON, 1, 0};

				timer = (unsigned int)(small.vertices[q].uv[0] *
						512) /
					10;
				CHECK(seen < 8 && timer == expected[seen]);
				seen++;
			}
		} else
			CHECK(small.vertices[q].position[0] ==
				large.vertices[q].position[0]);
	}

	CHECK(seen == 8);

	/* Keep an existing scene introduction beneath the unchanged HUD. */
	mh_ui_begin(&large, 640, 480);
	large.count = 6;
	memset(large.vertices, 0x35, sizeof(prefix));
	memcpy(prefix, large.vertices, sizeof(prefix));
	CHECK(mh_hud_append(&assets, &state, 640, 480, &large));
	CHECK(large.count == small.count + 6 &&
		memcmp(prefix, large.vertices, sizeof(prefix)) == 0);
	CHECK(memcmp(small.vertices, large.vertices + 6,
		small.count * sizeof(*small.vertices)) == 0);
	large.count = MH_UI_MAX_QUADS * 6 - 6;
	CHECK(!mh_hud_append(&assets, &state, 640, 480, &large) &&
		large.count == MH_UI_MAX_QUADS * 6 - 6);
	CHECK(memcmp(prefix, large.vertices, sizeof(prefix)) == 0);
	CHECK(!mh_hud_append(&assets, &state, 1024, 768, &large) &&
		large.count == MH_UI_MAX_QUADS * 6 - 6);
	state.gear = 0;
	CHECK(mh_hud_draw(&assets, &state, 320, 200, &small));
	state.info_detail = 1;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &small));
	CHECK(small.count == 19 * 6);
	state.info_detail = 0;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &small) &&
		small.count == 0);
	state.start = MH_START_THREE;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &small) &&
		small.count == 6);
	state.info_detail = 3;
	CHECK(!mh_hud_draw(&assets, &state, 640, 480, &small));
	state.info_detail = 2;
	state.revs = 2;
	CHECK(!mh_hud_draw(&assets, &state, 640, 480, &small));
}

static void
test_overlays(void)
{
	static struct mh_field field;
	struct mh_profile profiles[2];
	struct mh_hud_assets assets;
	struct mh_hud_state state;
	struct mh_ui_list list;
	float models[MH_FIELD_MESHES * 16], world[3],
		info[4] = {0, 1000, 0, -90};

	float offset[2] = {0, 0};
	int pixel[2];
	unsigned int i, count;
	static const unsigned long times[8] = {
		0, 199, 200, 399, 400, 599, 600, 800};

	memset(&field, 0, sizeof(field));
	memset(profiles, 0, sizeof(profiles));
	memset(models, 0, sizeof(models));
	field.count = 2;
	field.active_mask = 3;
	field.unavailable_mask = 2;
	field.viewed_entry = 1;
	field.standings.cars[0].position = 2;
	field.standings.cars[1].position = 1;
	field.cars[1].race.wrong_way = 1;
	field.cars[1].ticks = 48;
	field.cars[0].seconds = .4;
	profiles[0].colors[0] = 0x123456;
	profiles[1].colors[0] = 0xabcdef;
	strcpy(profiles[1].name, "Different Full Name");
	strcpy(profiles[1].nick, "AvEnGeR");
	strcpy(profiles[1].team, "zX");
	models[28] = 12;
	models[108] = 34;
	mh_hud_state(&field, 0, &state, NULL);
	mh_hud_map_state(&field, models, profiles, &state);
	CHECK(state.wrong_way && state.clock_ms == 401 &&
		state.marker_count == 2);
	CHECK(state.markers[0].world[0] == 12 &&
		state.markers[1].world[0] == 34);
	CHECK(state.markers[1].active && state.markers[1].viewed &&
		state.markers[1].special && state.markers[1].rank == 1 &&
		state.markers[1].color == 0xabcdef);
	field.cars[0].frame.active = 1;
	field.cars[0].frame.seconds = 123.5;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(state.clock_ms == 123500);
	field.cars[0].frame.active = 0;
	field.active_mask = 1;
	mh_hud_map_state(&field, models, profiles, &state);
	CHECK(state.markers[0].viewed && !state.markers[1].active);
	field.playback = 6;
	mh_hud_state(&field, 0, &state, NULL);

	/* A departed recorded entrant is absent from POS n/N. */
	CHECK(state.cars == 1);
	prepare_map_assets(&assets);
	state.marker_count = 0;
	state.map_mode = 0;
	state.start = MH_START_RACING;
	assets.sprites[MH_HUD_WRONG_WAY].width = 129;
	assets.sprites[MH_HUD_WRONG_WAY].height = 16;
	state.wrong_way = 0;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &list));
	count = list.count;
	state.wrong_way = 1;

	for (i = 0; i < 8; i++) {
		state.clock_ms = times[i];
		CHECK(mh_hud_draw(&assets, &state, 640, 480, &list));
		CHECK(list.count == count + (((times[i] / 200) & 1) ? 0 : 6));

		if (list.count > count)
			CHECK(list.vertices[count].position[0] == 255 &&
				list.vertices[count].position[1] == 10);
	}

	state.wrong_way = 0;
	state.start = MH_START_THREE;
	CHECK(mh_hud_draw(&assets, &state, 640, 480, &list));
	CHECK(list.vertices[list.count - 6].position[0] == 319 &&
		list.vertices[list.count - 6].position[1] == 60);
	memset(world, 0, sizeof(world));
	CHECK(mh_hud_map_project(info, offset, world, 100, 200, pixel) &&
		pixel[0] == 132 && pixel[1] == 240);
	world[1] = 1000;
	CHECK(!mh_hud_map_project(info, offset, world, 100, 200, pixel));
	world[1] = 0;
	world[0] = 1e30f;
	CHECK(!mh_hud_map_project(info, offset, world, 100, 200, pixel));
	CHECK(!mh_hud_map_project(NULL, offset, world, 100, 200, pixel));
	info[1] = 1e-37f;
	world[0] = 1000000;
	CHECK(!mh_hud_map_project(info, offset, world, 100, 200, pixel));
}

static void
test_notices(void)
{
	static struct mh_hud_assets assets;
	static struct mh_ui_list list;
	struct mh_hud_state state;
	unsigned int full, baseline, detail, checkpoint;

	prepare_map_assets(&assets);
	assets.hardware_height = 12;
	memset(&state, 0, sizeof(state));
	state.time_notices = 1;
	state.lap = 1;
	state.split_ms = 12345;
	mh_ui_begin(&list, 641, 480);
	CHECK(mh_hud_notices_draw(&assets, &state, &list));
	CHECK(list.count == 8 * 6 && list.vertices[0].position[0] == 370 &&
		list.vertices[0].position[1] == 34);
	state.lap = 2;
	state.lap_ms = 4999;
	state.last_ms = 73000;
	state.lap_notice = 1;
	mh_ui_begin(&list, 641, 480);
	CHECK(mh_hud_notices_draw(&assets, &state, &list));
	CHECK(list.count == 11 * 6 && list.vertices[0].position[1] == 34 &&
		list.vertices[18].position[1] == 58);
	state.lap_ms = 5000;
	mh_ui_begin(&list, 641, 480);
	CHECK(mh_hud_notices_draw(&assets, &state, &list) &&
		list.count == 8 * 6);
	state.lap = 1;
	state.lap_ms = state.last_ms;
	state.race_notice = 1;
	mh_ui_begin(&list, 641, 480);
	CHECK(mh_hud_notices_draw(&assets, &state, &list) &&
		list.count == 21 * 6);
	CHECK(list.vertices[11 * 6].position[1] == 76 &&
		list.vertices[13 * 6].position[1] == 100);
	state.lap_notice = state.race_notice = 2;
	state.clock_ms = 199;
	mh_ui_begin(&list, 641, 480);
	CHECK(mh_hud_notices_draw(&assets, &state, &list) &&
		list.count == 24 * 6);
	state.clock_ms = 200;
	mh_ui_begin(&list, 641, 480);
	CHECK(mh_hud_notices_draw(&assets, &state, &list) && list.count == 0);
	state.clock_ms = 400;
	state.time_notices = 0;
	CHECK(mh_hud_notices_draw(&assets, &state, &list) && list.count == 0);
	state.info_detail = state.checkpoint_info = 2;
	state.time_notices = 1;
	state.start = MH_START_RACING;

	/*
	 * Gap callers force their own mode to All at finish, after the original
	 * HUD has already consumed the configured checkpoint mode.
	 */
	state.gap_mode = 2;

	for (detail = 0; detail < 3; detail++)
		for (checkpoint = 0; checkpoint < 3; checkpoint++) {
			state.info_detail = detail;
			state.checkpoint_info = checkpoint;
			state.time_notices = 0;
			CHECK(mh_hud_draw(&assets, &state, 641, 480, &list));
			baseline = list.count;
			state.time_notices = 1;
			CHECK(mh_hud_draw(&assets, &state, 641, 480, &list));
			CHECK(list.count ==
				baseline +
					(detail != 0 && checkpoint != 0 ? 24 * 6
									: 0));
		}

	CHECK(mh_hud_draw(&assets, &state, 641, 480, &list));
	full = list.count;

	/* Failure in the late notice block must still roll back append. */
	mh_ui_begin(&list, 641, 480);
	list.count = MH_UI_MAX_QUADS * 6 - full + 6;
	full = list.count;
	CHECK(!mh_hud_append(&assets, &state, 641, 480, &list) &&
		list.count == full);
	mh_ui_begin(&list, 641, 480);
	state.race_notice = 3;
	CHECK(!mh_hud_notices_draw(&assets, &state, &list) && list.count == 0);
	state.race_notice = 2;
	assets.sprites[MH_HUD_BEST].width = 0xffffffffU;
	CHECK(!mh_hud_notices_draw(&assets, &state, &list) && list.count == 0);
}

static void
test_gaps(void)
{
	static struct mh_field field, saved;
	static struct mh_hud_assets assets;
	static struct mh_ui_list list;
	struct mh_profile profiles[2];
	struct mh_hud_state state;
	unsigned int i;

	memset(&field, 0, sizeof(field));
	memset(profiles, 0, sizeof(profiles));
	field.count = 2;
	field.active_mask = 3;

	for (i = 0; i < 2; i++) {
		field.cars[i].race.lap = 1;
		field.cars[i].race.lap_limit = 3;
		field.cars[i].race.split_notice = 2;
		field.cars[i].race.split_hold_ms = 4000;
		field.cars[i].race.now_ms = field.cars[i].race.splits[1][2] =
			10000 + i * 1234;
	}

	strcpy(profiles[0].name, "Test Player");
	strcpy(profiles[1].name, "Other Driver");
	strcpy(profiles[0].nick, "Player");
	strcpy(profiles[1].nick, "El Toro");
	profiles[1].colors[0] = 0xabcdefUL;
	saved = field;
	mh_hud_state(&field, 1, &state, NULL);
	CHECK(mh_hud_gap_state(&field, profiles, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 2 && state.gap_mode == 2);
	CHECK(strcmp(state.gaps[1].name, "El Toro") == 0 &&
		state.gaps[1].milliseconds == 1234 &&
		state.gaps[1].color == 0xabcdefUL);
	CHECK(memcmp(&saved, &field, sizeof(field)) == 0);
	field.viewed_entry = 1;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(mh_hud_gap_state(&field, profiles, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gaps[0].milliseconds == -1234);
	state.gap_mode = 1;
	CHECK(mh_hud_gap_state(&field, profiles, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 1 && state.gaps[0].position == 1);
	prepare_gap_assets(&assets);
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_hud_gaps_draw(&assets, &state, &list) && list.count != 0);
	CHECK(list.vertices[0].position[0] == 4 &&
		list.vertices[0].position[1] == 430);
	memset(profiles[0].nick, 'X', sizeof(profiles[0].nick));
	CHECK(mh_hud_gap_state(&field, profiles, &state, NULL) !=
			MH_ERROR_NONE &&
		strcmp(state.gaps[0].name, "Player") == 0);
	field.cars[1].race.split_notice = 0;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(mh_hud_gap_state(&field, profiles, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 0);
	field.cars[1].race.lap_limit = 0;
	mh_hud_state(&field, 0, &state, NULL);
	CHECK(mh_hud_gap_state(&field, profiles, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 0);
	state.gap_count = MH_RACE_MAX_CARS + 1;
	CHECK(!mh_hud_gaps_draw(&assets, &state, &list));
	state.gap_count = 1;
	memset(state.gaps[0].name, 'X', sizeof(state.gaps[0].name));
	CHECK(!mh_hud_gaps_draw(&assets, &state, &list));
}

static void
test_record_gaps(void)
{
	static struct mh_field field, saved;
	static struct mh_hud_assets assets;
	static struct mh_ui_list list;
	struct mh_hud_state state, before;
	struct mh_race *race;

	memset(&field, 0, sizeof(field));
	field.count = 2;
	field.active_mask = 3;
	field.viewed_entry = 1;
	race = &field.cars[1].race;
	race->lap = 2;
	race->lap_limit = 3;
	race->best_lap = 1;
	race->split_notice = 2;
	race->split_hold_ms = 4000;
	race->splits[1][2] = 3000;
	race->splits[2][0] = 10000;
	race->splits[2][2] = 14000;
	field.records.has_lap = field.records.has_race = 1;
	field.records.lap.splits[2] = 2500;
	field.records.race.rows[2].splits[2] = 15000;
	saved = field;
	mh_hud_state(&field, 1, &state, NULL);
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.record_gaps && state.gap_count == 3);
	CHECK(strcmp(state.gaps[0].name, "Lap") == 0 &&
		state.gaps[0].milliseconds == 1000 &&
		state.gaps[0].color == 0xffffffUL &&
		state.gaps[0].position == 0);
	CHECK(strcmp(state.gaps[1].name, "Lap Ever") == 0 &&
		state.gaps[1].milliseconds == 1500 &&
		state.gaps[1].color == 0xaaaa00UL &&
		state.gaps[1].position == 0);
	CHECK(strcmp(state.gaps[2].name, "Race Ever") == 0 &&
		state.gaps[2].milliseconds == -1000 &&
		state.gaps[2].color == 0xaaaa00UL &&
		state.gaps[2].position == 0);
	CHECK(memcmp(&saved, &field, sizeof(field)) == 0);
	field.time_attack = 1;
	CHECK(mh_hud_gap_state(&field, NULL, &state, NULL) == MH_ERROR_NONE &&
		state.record_gaps && state.gap_count == 3 &&
		strcmp(state.gaps[0].name, "Lap") == 0 &&
		state.gaps[0].milliseconds == 1000);
	prepare_gap_assets(&assets);
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_hud_gaps_draw(&assets, &state, &list) && list.count != 0 &&
		list.vertices[0].position[1] == 430);
	state.gap_mode = 1;
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 3);
	CHECK(mh_hud_gaps_draw(&assets, &state, &list) &&
		list.vertices[0].position[1] == 430);
	field.records.has_lap = field.records.has_race = 0;
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 3 && state.gaps[1].milliseconds == 0 &&
		state.gaps[2].milliseconds == 0);
	state.gap_mode = 0;
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 0 && state.gap_mode == 0);
	race->finished = 1;
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 3 && state.gap_mode == 2);
	CHECK(mh_hud_record_gap_state(&field, 1, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 0 && state.gap_mode == 2);
	race->split_hold_ms = 0;
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 0);
	race->split_hold_ms = 4000;
	race->split_notice = 0;
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 0);
	race->split_notice = 2;
	field.records.has_lap = 2;
	before = state;
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &before, sizeof(state)) == 0);
	field.records.has_lap = 0;
	field.active_mask = 0;
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &before, sizeof(state)) == 0);
	field.active_mask = 3;
	state.timed = 0;
	CHECK(mh_hud_record_gap_state(&field, 0, &state, NULL) ==
			MH_ERROR_NONE &&
		state.gap_count == 0);
}

static void
test_clock(void)
{
	static struct mh_field field;
	struct mh_hud_state state, saved;

	memset(&field, 0, sizeof(field));
	field.count = 2;
	field.active_mask = 3;
	field.cars[0].frame.active = field.cars[1].frame.active = 1;
	field.cars[0].frame.seconds = 1;
	field.cars[1].frame.seconds = 2;
	CHECK(mh_hud_state(&field, 0, &state, NULL) == MH_ERROR_NONE &&
		state.clock_ms == 1000);
	field.viewed_entry = 1;
	CHECK(mh_hud_state(&field, 1, &state, NULL) == MH_ERROR_NONE &&
		state.clock_ms == 1000);
	field.cars[0].frame.seconds = .4;
	CHECK(mh_hud_state(&field, 0, &state, NULL) == MH_ERROR_NONE &&
		state.clock_ms == 400);
	field.cars[0].frame.seconds = 0;
	CHECK(mh_hud_state(&field, 0, &state, NULL) == MH_ERROR_NONE &&
		state.clock_ms == 0);
	field.cars[0].frame.seconds = 4294968;
	CHECK(mh_hud_state(&field, 0, &state, NULL) == MH_ERROR_NONE &&
		state.clock_ms == 704);
	saved = state;
	field.cars[0].frame.seconds = -1;
	CHECK(mh_hud_state(&field, 0, &state, NULL) != MH_ERROR_NONE &&
		!memcmp(&state, &saved, sizeof(state)));
	CHECK(mh_hud_state(NULL, 0, &state, NULL) != MH_ERROR_NONE &&
		!memcmp(&state, &saved, sizeof(state)));
	CHECK(mh_hud_state(&field, 0, NULL, NULL) != MH_ERROR_NONE);
}

static void
test_large_map(void)
{
	static struct mh_hud_assets assets;
	static struct mh_ui_list list;
	struct mh_hud_state state;
	static const unsigned int numbers[] = {MH_HUD_OTHER_MARKER, MH_HUD_BIG,
		MH_HUD_BIG + 1, MH_HUD_BIG + 2, MH_HUD_BIG + 3};

	unsigned int i;

	memset(&state, 0, sizeof(state));
	assets.atlas.image.width = assets.atlas.image.height = 256;
	assets.map.width = 64;
	assets.map.height = 80;
	assets.map_info[1] = 1000;
	assets.map_info[3] = -90;

	for (i = 0; i < MH_HUD_SPRITES; i++) {
		assets.sprites[i].x = i;
		assets.sprites[i].width = assets.sprites[i].height = 1;
	}

	state.map_mode = 2;
	state.marker_count = 5;

	for (i = 0; i < 5; i++) {
		state.markers[i].active = 1;
		state.markers[i].rank = 9 + i;
	}

	mh_ui_begin(&list, 640, 480);
	CHECK(mh_hud_map_draw(&assets, &state, &list));
	CHECK(list.count == 66);

	for (i = 0; i < 5; i++)
		CHECK(list.vertices[(2 + i * 2) * 6].uv[0] ==
			numbers[i] / 256.0f);
}

int
main(void)
{

	test_label();
	test_draw();
	test_state();
	test_layout();
	test_overlays();
	test_large_map();
	test_secrets();
	test_notices();
	test_gaps();
	test_record_gaps();
	test_status();
	test_clock();

	return failures != 0;
}
