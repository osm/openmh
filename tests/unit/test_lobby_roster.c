#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/font.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/lobby_roster.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "lobby roster line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
statistics_view(void)
{
	struct mh_lobby_roster roster;
	struct mh_lobby_roster_view view, saved_view;
	struct mh_net_statistics statistics;
	unsigned char palette[768];
	unsigned int i, advance[256];

	memset(&roster, 0, sizeof(roster));
	memset(&statistics, 0, sizeof(statistics));
	memset(palette, 0, sizeof(palette));
	roster.layout = MH_LOBBY_ROSTER_31;
	roster.count = 13;
	roster.player = 12;
	roster.statistics = &statistics;
	roster.system = MH_UNITS_IMPERIAL;
	roster.players[12].lobby = 1;

	for (i = 0; i < 256; i++)
		advance[i] = 6;

	statistics.rows[0].position = 3;
	strcpy(statistics.rows[0].name, "Third");
	statistics.rows[0].best_lap_ms = 61009;
	statistics.rows[0].race_ms = 123456;
	statistics.rows[0].top_speed_kmh = 300;
	statistics.rows[2].position = 1;
	memset(statistics.rows[2].name, 'A', MH_NET_NAME_BYTES);
	CHECK(mh_lobby_roster_view(&roster, advance, palette, &view, NULL) ==
		MH_ERROR_NONE);
	CHECK(view.count == 19);
	CHECK(!strcmp((char *)view.lines[9].text, "3.") &&
		view.lines[9].y == 165);
	CHECK(!strcmp((char *)view.lines[11].text, "1:01:00") &&
		view.lines[11].x == 220);
	CHECK(!strcmp((char *)view.lines[12].text, "2:03:45") &&
		view.lines[12].x == 365);
	CHECK(!strcmp((char *)view.lines[13].text, "186 mph") &&
		view.lines[13].x == 487);
	CHECK(!strcmp((char *)view.lines[14].text, "1.") &&
		view.lines[14].y == 177);
	CHECK(strlen((char *)view.lines[15].text) == MH_NET_NAME_BYTES);
	saved_view = view;
	roster.system = 2;
	CHECK(mh_lobby_roster_view(&roster, advance, palette, &view, NULL) ==
		MH_ERROR_INVALID);
	CHECK(!memcmp(&view, &saved_view, sizeof(view)));

	return 0;
}

static int
patched_row_spacing(void)
{
	struct mh_lobby_roster roster;
	struct mh_lobby_roster_view view;
	unsigned int advance[256], count, i, rows, previous;
	unsigned char palette[768];

	memset(&roster, 0, sizeof(roster));
	memset(palette, 0, sizeof(palette));

	for (i = 0; i < 256; i++)
		advance[i] = 6;

	roster.layout = MH_LOBBY_ROSTER_31;
	roster.count = 13;
	roster.player = 1;

	for (count = 2; count <= 12; count++) {
		for (i = 1; i < roster.count; i++) {
			roster.players[i].lobby = i <= count;
			strcpy(roster.players[i].name, "Driver");
		}

		CHECK(mh_lobby_roster_view(&roster, advance, palette, &view,
			NULL) == MH_ERROR_NONE);
		rows = previous = 0;

		for (i = 0; i < view.count; i++) {
			if (view.lines[i].x != 75)
				continue;

			if (rows)
				CHECK(view.lines[i].y - previous >=
					(count <= 10 ? 12U : 10U));

			previous = view.lines[i].y;
			rows++;
		}

		CHECK(rows == count && previous + 10 <= 285);
	}

	return 0;
}

int
main(void)
{
	struct mh_lobby_roster roster, saved_roster;
	struct mh_lobby_roster_view view, saved_view;
	struct mh_font font;
	struct mh_menu_canvas canvas;
	unsigned char palette[768], glyphs[1024];
	unsigned int i, advance[256];

	CHECK(statistics_view() == 0);
	CHECK(patched_row_spacing() == 0);

	memset(&roster, 0, sizeof(roster));
	memset(palette, 0, sizeof(palette));
	roster.count = 8;
	roster.laps = 3;
	strcpy(roster.session, "Motorhead");
	strcpy(roster.track, "RedRock");
	palette[3] = 255;
	palette[723] = 224;
	palette[724] = 104;
	palette[725] = 32;

	for (i = 0; i < 256; i++)
		advance[i] = 10;

	for (i = 0; i < 8; i++) {
		roster.players[i].racing = roster.players[i].lobby =
			roster.players[i].recording = 1;
		roster.players[i].position = i + 1;
		roster.players[i].color = 0xff0000UL;
		roster.players[i].points = 10;
		strcpy(roster.players[i].name, "ABCDEFGHIJKLMNO");
		strcpy(roster.players[i].car, "123456789012345");
		strcpy(roster.players[i].team, i % 2 ? "dice" : "DICE");
	}

	saved_roster = roster;
	CHECK(mh_lobby_roster_view(&roster, advance, palette, &view, NULL) ==
			MH_ERROR_NONE &&
		view.count == 177);
	CHECK(!memcmp(&roster, &saved_roster, sizeof(roster)) &&
		view.team_points[0] == 80);
	CHECK(!strcmp((char *)view.lines[9].text, "1") &&
		view.lines[9].x == 30 && view.lines[9].y == 170);
	CHECK(!strcmp((char *)view.lines[11].text, "R") &&
		view.lines[11].y == 175);
	CHECK(!strcmp((char *)view.lines[12].text, "ABCDEFGHIJKLMN"));
	CHECK(!strcmp((char *)view.lines[13].text, "12345678"));
	CHECK(!strcmp((char *)view.lines[97].text, "L") &&
		view.lines[97].y == 276);
	saved_view = view;
	advance[65] = 65536;
	CHECK(mh_lobby_roster_view(&roster, advance, palette, &view, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&view, &saved_view, sizeof(view)));
	advance[65] = 10;
	roster.password_mode = 1;
	CHECK(mh_lobby_roster_view(&roster, advance, palette, &view, NULL) ==
			MH_ERROR_NONE &&
		view.count == 168);
	roster.password_mode = 0;
	roster.players[0].position = 2;
	roster.players[1].position = 2;
	CHECK(mh_lobby_roster_view(&roster, advance, palette, &view, NULL) ==
			MH_ERROR_NONE &&
		view.count == 166);

	/* First matching slot wins a rank tie. */
	CHECK(!strcmp((char *)view.lines[9].text, "2"));

	memset(&font, 0, sizeof(font));
	memset(glyphs, 255, sizeof(glyphs));
	font.image.rgba = glyphs;
	font.image.width = font.image.height = 16;
	font.height = font.cell_width = 1;

	for (i = 0; i < 256; i++)
		font.advance[i] = 1;

	canvas.size = 256000;
	canvas.begin = 0;
	canvas.end = 256000;
	canvas.pitch = 640;
	canvas.pixels = (unsigned char *)calloc(canvas.size, 1);
	CHECK(canvas.pixels != NULL);
	CHECK(mh_lobby_roster_draw(&roster, &font, palette, &canvas, NULL) ==
		MH_ERROR_NONE);
	CHECK(canvas.pixels[170 * 640 + 30] == 1 &&
		canvas.pixels[28 * 640 + 170] == 241);
	memset(canvas.pixels, 0xa5, canvas.size);
	memset(roster.players[7].name, 'X', 17);
	CHECK(mh_lobby_roster_draw(&roster, &font, palette, &canvas, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < canvas.size; i++)
		CHECK(canvas.pixels[i] == 0xa5);

	free(canvas.pixels);
	puts("lobby roster: ok");

	return 0;
}
