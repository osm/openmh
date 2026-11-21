#include <stdio.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/core/error.h"
#include "engine/ui/hud.h"
#include "engine/ui/menu_credits.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Credits line %d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_credits_art art;
static struct mh_menu_layout layout;
static unsigned char pixels[614400], saved_pixels[614400], glyphs[1024],
	sprite_bytes[814];

static void
write_word(unsigned int offset, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		sprite_bytes[offset + i] = (unsigned char)(value >> (i * 8));
}

int
main(void)
{
	struct mh_sprite_collection *collection;
	struct mh_menu_credits_art rejected_art;
	struct mh_menu_credits_animation animation, saved_animation;
	struct mh_menu_credits_input input;
	struct mh_menu_output output, saved_output;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	struct mh_hud_state hud;
	struct mh_hud_secrets secrets;
	unsigned long seed, noise, saved_seed, saved_noise;
	unsigned int i;
	size_t origin;

	memset(&font, 0, sizeof(font));
	memset(glyphs, 255, sizeof(glyphs));
	memset(&input, 0, sizeof(input));
	memset(&hud, 0, sizeof(hud));
	memset(&secrets, 0, sizeof(secrets));

	for (i = 0; i < 256; i++)
		font.advance[i] = 1;

	font.image.rgba = glyphs;
	font.image.width = font.image.height = 16;
	font.cell_width = font.height = 1;
	canvas.pixels = pixels;
	canvas.size = sizeof(pixels);
	canvas.begin = 115360;
	canvas.end = 499360;
	canvas.pitch = 960;
	origin = canvas.begin;
	write_word(0, 0x80000001UL);
	memcpy(sprite_bytes + 4, "dots", 4);
	write_word(12, 784);
	write_word(784, 1);
	write_word(788, 2);
	write_word(792, 2);
	write_word(796, 16);
	write_word(800, 8);
	write_word(804, 13);
	memcpy(sprite_bytes + 808, "\306\100\0\0\303\303", 6);
	CHECK(mh_sprite_spr_open(sprite_bytes, sizeof(sprite_bytes),
		MH_PICTURE_INDEXED, &collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_credits_art_read(collection, &art, NULL) ==
			MH_ERROR_NONE &&
		art.images[1].pixels != NULL && art.images[3].pixels == NULL);
	memset(&rejected_art, 0xa5, sizeof(rejected_art));
	sprite_bytes[808] = 0xe8;
	mh_sprite_collection_close(collection);
	CHECK(mh_sprite_spr_open(sprite_bytes, sizeof(sprite_bytes),
		MH_PICTURE_INDEXED, &collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_credits_art_read(collection, &rejected_art, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(rejected_art); i++)
		CHECK(((unsigned char *)&rejected_art)[i] == 0xa5);

	mh_sprite_collection_close(collection);

	layout.words[18][24][0] = layout.words[18][24][1] = 320;
	layout.words[18][24][2] = layout.words[18][24][3] = 40;
	layout.words[18][25][2] = layout.words[18][25][3] = 80;
	mh_menu_credits_animation_init(&animation);

	for (i = 0; i < 32; i++)
		CHECK(animation.sprites.timer[i] == 0 &&
			animation.sprites.wobble[i] == (i == 2 || i == 3));

	seed = 1;
	noise = 741118;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_credits_render(&art, &font, &layout, 0, &input, 0,
		&animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 40 * 960 + 313] == 99 &&
		pixels[origin + 40 * 960 + 314] == 3);
	CHECK(pixels[origin + 60 * 960 + 304] == 1 &&
		pixels[origin + 100 * 960 + 316] == 3);
	CHECK(pixels[origin + 220 * 960 + 312] == 1);

	/*
	 * The temporary shortcut leaves the independent visit latch untouched.
	 */
	input.control = input.left_alt = input.right_alt = 1;
	CHECK(mh_menu_credits_alternate(secrets.club_credits, &input));
	input.right_alt = 0;
	CHECK(!mh_menu_credits_alternate(secrets.club_credits, &input));
	hud.map_mode = 1;
	hud.marker_count = 1;
	hud.markers[0].viewed = hud.markers[0].active = 1;
	hud.markers[0].world[0] = 476;
	hud.markers[0].world[2] = -188;

	for (i = 0; i < 202; i++)
		mh_hud_secrets_frame(&secrets, "ruhrstadt", &hud, 640, 0);

	CHECK(secrets.notice == 1 && !secrets.club_credits);
	mh_hud_secrets_frame(&secrets, "ruhrstadt", &hud, 640, 0);
	CHECK(secrets.club_credits == 1 &&
		mh_menu_credits_alternate(secrets.club_credits, &input));
	secrets.notice = 0;
	secrets.scroll_started = 0; /* The race-entry resets. */
	mh_menu_credits_animation_init(&animation);
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_credits_render(&art, &font, &layout, secrets.club_credits,
		&input, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 40 * 960 + 314] == 99 &&
		pixels[origin + 40 * 960 + 318] == 3);

	/*
	 * A bad late sprite must not publish partial drawing or consumed RNG.
	 */
	saved_animation = animation;
	saved_seed = seed;
	saved_noise = noise;
	memcpy(saved_pixels, pixels, sizeof(pixels));
	art.images[3] = art.images[1];
	art.images[3].height = 1;
	CHECK(mh_menu_credits_render(&art, &font, &layout, 0, &input, 0,
		&animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0 &&
		memcmp(&saved_animation, &animation, sizeof(saved_animation)) ==
			0);
	CHECK(seed == saved_seed && noise == saved_noise);
	memset(&art.images[3], 0, sizeof(art.images[3]));
	CHECK(mh_menu_credits_render(&art, &font, &layout, 0, &input, 257,
		&animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);
	input.escape = input.enter = 1;
	CHECK(mh_menu_credits_update(&input, &output, NULL) == MH_ERROR_NONE &&
		output.count == 4);
	CHECK(output.events[0].kind == MH_MENU_SOUND &&
		output.events[0].value == 1);
	CHECK(output.events[1].kind == MH_MENU_PAGE &&
		output.events[1].value == MH_MENU_MAIN);
	CHECK(output.events[2].kind == MH_MENU_SOUND &&
		output.events[2].value == 11);
	CHECK(output.events[3].kind == MH_MENU_PAGE &&
		output.events[3].value == MH_MENU_MAIN);
	saved_output = output;
	CHECK(mh_menu_credits_update(NULL, &output, NULL) != MH_ERROR_NONE &&
		memcmp(&saved_output, &output, sizeof(output)) == 0);
	mh_menu_credits_art_free(&art);
	mh_menu_credits_art_free(&art);
	puts("Credits artwork bounds, centred text, Club TBL HUD handoff, input ordering and failure atomicity passed");

	return 0;
}
