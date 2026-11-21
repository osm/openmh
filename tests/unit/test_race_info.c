#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/input/control_type.h"
#include "engine/ui/hud.h"
#include "engine/ui/race_info.h"
#include "engine/ui/software_hud.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static struct mh_hud_assets assets;
static struct mh_ui_list list;

static void
prepare_font(unsigned int height)
{
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	assets.atlas.image.width = assets.atlas.image.height = 8192;
	assets.hardware_height = height;
	assets.solid.x = assets.solid.y = 8191;
	assets.solid.width = assets.solid.height = 1;

	for (i = 0; i < 256; i++) {
		assets.hardware_glyphs[i].x = i * 32;
		assets.hardware_glyphs[i].width = i % 7 + 3;
		assets.hardware_glyphs[i].height = height;
		assets.hardware_advance[i] = i % 7 + 4;
	}
}

static void
check_separator_span(unsigned int width)
{
	const struct mh_ui_vertex *vertex;
	unsigned int i, count;
	float left, right;

	left = (float)width;
	right = 0;
	count = 0;

	for (i = 0; i < list.count; i++) {
		vertex = &list.vertices[i];

		if (vertex->uv[0] < .999f || vertex->uv[1] < .999f ||
			vertex->color[0] != 1)
			continue;

		if (vertex->position[0] < left)
			left = vertex->position[0];

		if (vertex->position[0] > right)
			right = vertex->position[0];

		CHECK(vertex->position[0] >= 0 && vertex->position[0] <= width);
		count++;
	}

	CHECK(count != 0);
	CHECK(left <= width / 9 - 12);
	CHECK(right >= 6 * (width / 7) + 12);
}

static void
check_resolutions(void)
{
	static const unsigned int widths[] = {
		320, 640, 1024, 1920, 3840, MH_HUD_VIEW_LIMIT};

	static struct mh_software_hud_assets software;
	static unsigned char font_byte;
	struct mh_race_info info;
	unsigned int i, width;
	int previous_failures;

	memset(&info, 0, sizeof(info));
	strcpy(info.track, "Atlantika");
	prepare_font(12);
	memset(&software, 0, sizeof(software));
	software.atlas = assets.atlas;
	software.solid = assets.solid;
	software.font.height = 12;
	software.font.bytes = &font_byte;
	software.green_bits = 6;

	for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
		previous_failures = failures;
		width = widths[i];
		software.resolution_shift = width < 640;
		mh_ui_begin(&list, width, width * 3 / 4);
		CHECK(mh_race_info_draw(&assets, &info, &list));
		check_separator_span(width);
		mh_ui_begin(&list, width, width * 3 / 4);
		CHECK(mh_software_hud_info(&software, &info, &list, NULL) ==
			MH_ERROR_NONE);
		check_separator_span(width);

		/* A failed separator must also roll back preceding text. */
		mh_ui_begin(&list, width, width * 3 / 4);
		list.count = MH_UI_MAX_QUADS * 6 - 6;
		CHECK(mh_software_hud_info(&software, &info, &list, NULL) !=
			MH_ERROR_NONE);
		CHECK(list.count == MH_UI_MAX_QUADS * 6 - 6 &&
			list.text_count == 0 && list.text_size == 0);

		if (failures != previous_failures)
			fprintf(stderr, "Scoreboard width: %u\n", width);
	}
}

static void
check_snapshot(void)
{
	static struct mh_field field;
	struct mh_track track;
	struct mh_profile profiles[8];
	struct mh_race_info info, saved;
	unsigned long types[8];
	unsigned int i;

	memset(&field, 0, sizeof(field));
	memset(&track, 0, sizeof(track));
	memset(profiles, 0, sizeof(profiles));
	strcpy(track.name, "RedRock");
	field.count = field.standings.count = 8;
	field.active_mask = 255;
	field.cars[0].track = &track;

	for (i = 0; i < 8; i++) {
		field.standings.order[i] = 7 - i;
		field.standings.cars[7 - i].position = i + 1;
		field.cars[i].config.options.manual = i & 1;
		sprintf(profiles[i].nick, "Driver%u", i);
		sprintf(profiles[i].team, "Team%u", i);
		profiles[i].colors[0] = 0x123450UL + i;
		types[i] = i;
	}

	CHECK(mh_race_info_state(&field, profiles, types, &info, NULL) ==
		MH_ERROR_NONE);
	CHECK(info.count == 8 && !strcmp(info.track, "RedRock"));

	for (i = 0; i < 8; i++) {
		CHECK(info.rows[i].entry == 7 - i &&
			info.rows[i].position == i + 1);
		CHECK(!strcmp(info.rows[i].name, profiles[7 - i].nick));
		CHECK(!strcmp(info.rows[i].team, profiles[7 - i].team));
		CHECK(info.rows[i].automatic == (i & 1) &&
			info.rows[i].control_type == 7 - i);
		CHECK(info.rows[i].color == 0x123457UL - i &&
			info.rows[i].ping == 0);
	}

	field.active_mask = 0x81;
	field.unavailable_mask = 0x81;
	CHECK(mh_race_info_state(&field, profiles, types, &info, NULL) ==
		MH_ERROR_NONE);
	CHECK(info.count == 2 && info.rows[0].entry == 7 &&
		info.rows[1].entry == 0);
	saved = info;
	field.standings.order[7] = 7;
	CHECK(mh_race_info_state(&field, profiles, types, &info, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&info, &saved, sizeof(info)));
	field.standings.order[7] = 0;
	memset(profiles[0].team, 'X', sizeof(profiles[0].team));
	CHECK(mh_race_info_state(&field, profiles, types, &info, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&info, &saved, sizeof(info)));
	info = saved;
	prepare_font(12);
	mh_ui_begin(&list, 640, 480);
	CHECK(mh_race_info_draw(&assets, &info, &list) && list.count != 0);
	list.count = MH_UI_MAX_QUADS * 6 - 6;
	CHECK(!mh_race_info_draw(&assets, &info, &list) &&
		list.count == MH_UI_MAX_QUADS * 6 - 6);
	mh_ui_begin(&list, 640, 480);
	memset(info.track, 'X', sizeof(info.track));
	CHECK(!mh_race_info_draw(&assets, &info, &list) && list.count == 0);
}

int
main(void)
{

	check_snapshot();
	check_resolutions();

	return failures ? 1 : 0;
}
