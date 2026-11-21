#include <stdio.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/core/error.h"
#include "engine/ui/menu_controls_draw.h"
#include "engine/ui/menu_slider.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Controls draw line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_layout layout;
static struct mh_menu_controls_art art, untouched;
static unsigned char bytes[814], rgba[1024];

static void
put_u32le(unsigned int offset, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		bytes[offset + i] = (unsigned char)(value >> (i * 8));
}

int
main(void)
{
	struct mh_sprite_collection *collection;
	struct mh_menu_controls_animation state, saved;
	struct mh_menu_controls_state menu;
	struct mh_control_profile profile;
	struct mh_menu_canvas canvas;
	struct mh_menu_canvas expected;
	struct mh_font font;
	unsigned char screen[8192], before[8192], color;
	unsigned long seed, noise, old_seed, old_noise;
	unsigned int i, scenario, length;
	static const unsigned int phases[] = {MH_MENU_CONTROLS_CAPTURE,
		MH_MENU_CONTROLS_RELEASE, MH_MENU_CONTROLS_FINISH};

	put_u32le(0, 0x80000001UL);
	memcpy(bytes + 4, "dots", 4);
	put_u32le(12, 784);
	put_u32le(784, 1);
	put_u32le(788, 2);
	put_u32le(792, 2);
	put_u32le(796, 16);
	put_u32le(800, 8);
	put_u32le(804, 13);
	memcpy(bytes + 808, "\306\100\0\0\303\303", 6);
	CHECK(mh_sprite_spr_open(bytes, sizeof(bytes), MH_PICTURE_INDEXED,
		&collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_controls_art_read(collection, &art, NULL) ==
		MH_ERROR_NONE);
	CHECK(art.count[1] == 1 && art.count[15] == 0 && art.count[25] == 0);
	memset(&untouched, 0xa5, sizeof(untouched));
	bytes[808] = 0xe8;
	mh_sprite_collection_close(collection);
	CHECK(mh_sprite_spr_open(bytes, sizeof(bytes), MH_PICTURE_INDEXED,
		&collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_controls_art_read(collection, &untouched, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(untouched); i++)
		CHECK(((unsigned char *)&untouched)[i] == 0xa5);

	mh_sprite_collection_close(collection);
	memset(&font, 0, sizeof(font));
	memset(rgba, 255, sizeof(rgba));
	font.height = font.cell_width = 1;
	font.image.width = font.image.height = 16;
	font.image.rgba = rgba;

	for (i = 0; i < 256; i++)
		font.advance[i] = 1;

	memset(&menu, 0, sizeof(menu));
	memset(&profile, 0, sizeof(profile));
	strcpy(profile.layout.name, "Test");
	mh_menu_controls_animation_init(&state);

	for (i = 0; i < 32; i++)
		CHECK(state.sprites.timer[i] == 0 &&
			state.sprites.wobble[i] == (i == 2 || i == 3));

	saved = state;
	seed = old_seed = 123;
	noise = old_noise = 456;
	memset(screen, 23, sizeof(screen));
	memcpy(before, screen, sizeof(screen));
	canvas.pixels = screen;
	canvas.size = sizeof(screen);
	canvas.pitch = 64;
	canvas.begin = 512;
	canvas.end = 3584;

	for (scenario = 0; scenario < 4; scenario++) {
		if (scenario == 0)
			art.count[25] = 9;

		if (scenario == 1)
			memset(profile.layout.name, 'X', 32);

		if (scenario == 2)
			menu.column = 2;

		if (scenario == 3)
			layout.words[8][22][0] = 32769;

		CHECK(mh_menu_controls_render(&art, &font, &layout, &menu,
			&profile, 1, NULL, 0, 0, &state, &seed, &noise,
			&canvas, NULL, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(screen, before, sizeof(screen)) == 0 &&
			memcmp(&state, &saved, sizeof(state)) == 0);
		CHECK(seed == old_seed && noise == old_noise);
		art.count[25] = 0;
		strcpy(profile.layout.name, "Test");
		menu.column = 0;
		layout.words[8][22][0] = 0;
	}

	CHECK(mh_menu_controls_render(&art, &font, &layout, &menu, &profile, 1,
		NULL, 0, 0, &state, &seed, &noise, &canvas, NULL,
		NULL) == MH_ERROR_NONE);
	CHECK(seed != old_seed && memcmp(screen, before, sizeof(screen)) != 0);
	mh_menu_controls_art_free(&art);
	mh_menu_controls_art_free(&art);

	/*
	 * Separate action rows so each prompt's position and color is visible.
	 */
	canvas.pitch = 256;
	canvas.end = sizeof(screen) - canvas.begin;
	layout.words[8][30][MH_MENU_LAYOUT_Y0] = 2;
	layout.words[8][30][MH_MENU_LAYOUT_Y1] = 2;
	menu.column = MH_MENU_CONTROLS_BINDINGS;
	menu.held_key = 28;
	profile.layout.bindings[0] = 28;

	for (scenario = 0; scenario < sizeof(phases) / sizeof(phases[0]);
		scenario++) {
		menu.phase = phases[scenario];
		memset(screen, 23, sizeof(screen));
		CHECK(mh_menu_controls_render(&art, &font, &layout, &menu,
			&profile, 1, NULL, 0, 0, &state, &seed, &noise,
			&canvas, NULL, NULL) == MH_ERROR_NONE);
		length = scenario == 0 ? 18 : scenario == 1 ? 8 : 6;

		for (i = 0; i < canvas.pitch; i++) {
			color = i >= 44 && i < 44 + length ? 2 : 23;

			if (scenario == 1 && i >= 112 && i < 118)
				color = 2;

			CHECK(screen[canvas.begin + 4 * canvas.pitch + i] ==
				color);
		}
	}

	/*
	 * Patched rows use percentages and move the first binding to row four.
	 */
	menu.preferences = art.preferences = 1;
	menu.phase = MH_MENU_CONTROLS_IDLE;
	menu.unavailable = 1U << MH_MENU_CONTROLS_FEEDBACK;
	profile.layout.mouse_speed[0] = 1.25f;
	profile.layout.mouse_speed[1] = 2.5f;
	profile.layout.mouse_speed[2] = 0.75f;
	memset(screen, 23, sizeof(screen));
	memset(before, 23, sizeof(before));
	expected = canvas;
	expected.pixels = before;
	CHECK(mh_menu_controls_render(&art, &font, &layout, &menu, &profile, 1,
		NULL, 0, 0, &state, &seed, &noise, &canvas, NULL,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_CONTROL_MOUSE_AXES; i++) {
		CHECK(mh_menu_slider_draw(&font,
			profile.layout.mouse_speed[i] * 100, 1, 1000, "%",
			150, 0, 44, 7 + i * 2, &expected,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(screen + canvas.begin + (7 + i * 2) * canvas.pitch,
			before + canvas.begin +
				(7 + i * 2) * canvas.pitch,
			canvas.pitch) == 0);
	}

	CHECK(screen[canvas.begin + 5 * canvas.pitch + 44] == 1);
	CHECK(screen[canvas.begin + 13 * canvas.pitch + 44] == 1);
	menu.unavailable |= 1U << MH_MENU_CONTROLS_MOUSE_Z;
	memset(screen, 23, sizeof(screen));
	CHECK(mh_menu_controls_render(&art, &font, &layout, &menu, &profile, 1,
		NULL, 0, 0, &state, &seed, &noise, &canvas, NULL,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < canvas.pitch; i++)
		CHECK(screen[canvas.begin + 11 * canvas.pitch + i] ==
			(i >= 44 && i < 55 ? 1 : 23));

	puts("Controls draw: bounded artwork, retained effects and late-failure pixel/state atomicity passed");
	puts("Controls capture: assignment/release prompts and held-key positions passed");

	return 0;
}
