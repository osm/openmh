#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_league_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "League drawing line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_menu_layout layout;
static struct mh_menu_league_art art;

int
main(void)
{
	struct mh_menu_league_state menu;
	struct mh_menu_league_info info;
	struct mh_menu_league_details details;
	struct mh_menu_single_item cars[3], tracks[2];
	struct mh_menu_league_animation animation, saved_animation;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	unsigned char pixels[1024], saved_pixels[1024], palette[768],
		saved_palette[768], glyphs[1024];
	unsigned int i, car;
	unsigned long seed, noise;

	memset(&menu, 0, sizeof(menu));
	memset(&info, 0, sizeof(info));
	memset(&details, 0, sizeof(details));
	menu.count = 1;
	info.name = "League";
	info.player = "Human";
	info.stage = 3;

	for (i = 0; i < 8; i++) {
		info.drivers[i].nick = "Racer";
		info.drivers[i].color = 0x123456UL + i;
	}

	details.leagues = &info;
	details.cars = cars;
	details.car_count = 3;
	details.tracks = tracks;
	details.track_count = 2;
	details.profile_count = 26;
	cars[0].name = "First";
	cars[0].division = 101;
	cars[1].name = "Special";
	cars[1].division = 110;
	cars[2].name = "Last";
	cars[2].division = 401;
	tracks[0].name = "Locked";
	tracks[0].division = 400;
	tracks[1].name = "Available";
	tracks[1].division = 100;
	canvas.pixels = pixels;
	canvas.size = canvas.end = sizeof(pixels);
	canvas.begin = 0;
	canvas.pitch = 32;
	memset(&font, 0, sizeof(font));
	memset(glyphs, 255, sizeof(glyphs));
	font.image.rgba = glyphs;
	font.image.width = font.image.height = 16;
	font.height = font.cell_width = 1;

	for (i = 0; i < 256; i++)
		font.advance[i] = 1;

	art.page = MH_MENU_LEAGUE_STANDINGS;
	CHECK(mh_menu_league_animation_init(art.page, &layout, 0, 0, &animation,
		NULL) == MH_ERROR_NONE);
	memset(pixels, 73, sizeof(pixels));
	memset(palette, 51, sizeof(palette));
	memcpy(saved_pixels, pixels, sizeof(pixels));
	memcpy(saved_palette, palette, sizeof(palette));
	saved_animation = animation;
	seed = 123;
	noise = 456;
	car = 2;

	/*
	 * A negative separator length fails after text, car clamp and palette
	 * updates in scratch. None may escape the failed frame.
	 */
	layout.words[16][20][0] = -1;
	CHECK(mh_menu_league_render(&art, &font, &layout, &menu, &details, &car,
		palette, 0, 16, 16, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(car == 2 && seed == 123 && noise == 456 &&
		memcmp(&animation, &saved_animation, sizeof(animation)) == 0);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0 &&
		memcmp(palette, saved_palette, sizeof(palette)) == 0);
	layout.words[16][20][0] = 1;
	CHECK(mh_menu_league_render(&art, &font, &layout, &menu, &details, &car,
		palette, 0, 16, 16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(car == 1 && memcmp(pixels, saved_pixels, sizeof(pixels)) != 0);

	for (i = 0; i < 8; i++)
		CHECK(palette[384 + i * 3] == 0x12 &&
			palette[385 + i * 3] == 0x34 &&
			palette[386 + i * 3] == 0x56 + i);

	CHECK(memcmp(palette, saved_palette, 384) == 0 &&
		memcmp(palette + 408, saved_palette + 408, 360) == 0);

	/*
	 * An interrupted completed season is displayable for recovery; beyond
	 * that boundary still rejects atomically instead of inventing a track.
	 */
	info.race_index = 1;
	CHECK(mh_menu_league_render(&art, &font, &layout, &menu, &details, &car,
		palette, 0, 16, 16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	info.race_index = 2;
	saved_animation = animation;
	memcpy(saved_pixels, pixels, sizeof(pixels));
	memcpy(saved_palette, palette, sizeof(palette));
	seed = 123;
	noise = 456;
	car = 2;
	CHECK(mh_menu_league_render(&art, &font, &layout, &menu, &details, &car,
		palette, 0, 16, 16, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(car == 2 && seed == 123 && noise == 456 &&
		memcmp(&animation, &saved_animation, sizeof(animation)) == 0);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0 &&
		memcmp(palette, saved_palette, sizeof(palette)) == 0);

	/*
	 * The new-draft page does not require an upcoming track, nor mutate the
	 * selected car or standings palette. Missing sprite entries are legal.
	 */
	art.page = MH_MENU_LEAGUE_NEW;
	strcpy((char *)menu.name, "Draft");
	CHECK(mh_menu_league_animation_init(art.page, &layout, 0, 0, &animation,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_league_render(&art, &font, &layout, &menu, &details, &car,
		palette, 0, 16, 16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE &&
		car == 2);
	CHECK(memcmp(palette, saved_palette, sizeof(palette)) == 0);
	puts("League frame rollback, palette scope, car eligibility and unavailable schedule checks passed");

	return 0;
}
