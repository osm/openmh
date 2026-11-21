#include <math.h>

#include "engine/core/text.h"
#include "engine/ui/hud.h"

#define SINE_SAMPLES 4096
#define WORLD_COORDINATE_LIMIT 1000000
#define SCROLL_X_LIMIT 16434

static int
track_matches(const char *track, const char *name)
{
	return track != NULL && mh_ascii_equal(track, name);
}

void
mh_hud_secret_update(struct mh_hud_secrets *secrets, const char *track,
	unsigned int map_mode, int active, const float world[3])
{
	float x, y, z;
	long double dz, distance;
	unsigned int i;

	if (secrets == NULL || map_mode == 0 || !active)
		return;

	secrets->notice = MH_HUD_SECRET_NONE;

	if (world == NULL)
		return;

	for (i = 0; i < 3; i++)
		if (!(world[i] >= -WORLD_COORDINATE_LIMIT &&
			world[i] <= WORLD_COORDINATE_LIMIT))
			return;

	if (track_matches(track, "ruhrstadt")) {
		x = (float)((long double)world[0] - 476);
		dz = (long double)world[2] + 188;
		z = (float)dz;

		/* Retain unrounded Z for one multiplication. */
		distance = (long double)x * x + dz * z;

		if (distance < 400) {
			if (secrets->visits > 200)
				secrets->notice = MH_HUD_SECRET_CLUB;
			else
				secrets->visits++;
		} else
			secrets->visits = 0;
	}

	if (track_matches(track, "atlantika")) {
		x = (float)((long double)world[0] - 159);
		y = (float)((long double)world[1] - 12);
		z = (float)((long double)world[2] + 436);
		distance = (long double)y * y + (long double)x * x +
			(long double)z * z;

		if (distance < 4)
			secrets->notice = MH_HUD_SECRET_SCROLLER;
	}
}

static unsigned int
wrap_phase(unsigned int value)
{
	return value > SINE_SAMPLES ? value - SINE_SAMPLES : value;
}

void
mh_hud_secrets_frame(struct mh_hud_secrets *secrets, const char *track,
	struct mh_hud_state *hud, unsigned int width, unsigned int length)
{
	unsigned int i;

	if (secrets->notice == MH_HUD_SECRET_SCROLLER && length != 0 &&
		!secrets->scroll_started) {
		secrets->scroll_started = 1;
		secrets->scroll_x = (int)width + 50;
		secrets->scroll_offset = 0;
		secrets->phase_y = 1024;
		secrets->phase_x = 0;
	}

	hud->secrets = *secrets;

	if (secrets->notice == MH_HUD_SECRET_CLUB)
		secrets->club_credits = 1;

	if (secrets->notice == MH_HUD_SECRET_SCROLLER) {
		secrets->bounce_phase = wrap_phase(secrets->bounce_phase + 10);

		if (length != 0) {
			secrets->scroll_x--;

			if (secrets->scroll_x < 0) {
				secrets->scroll_x = 8;
				secrets->scroll_offset =
					(secrets->scroll_offset + 1) % length;
				secrets->phase_y += 200;
				secrets->phase_x += 50;
			}

			secrets->phase_y = wrap_phase(secrets->phase_y + 10);
			secrets->phase_x = wrap_phase(secrets->phase_x + 10);
		}
	}

	for (i = 0; i < hud->marker_count && i < MH_RACE_MAX_CARS; i++)
		if (hud->markers[i].viewed) {
			mh_hud_secret_update(secrets, track, hud->map_mode,
				hud->markers[i].active, hud->markers[i].world);
			break;
		}
}

static float
sine_sample(unsigned int index)
{
	/*
	 * 4096 binary32 sine samples with a zero sentinel after the final
	 * sample.
	 */
	if (index >= SINE_SAMPLES)
		return 0;

	return (float)sin(index * 0.001533935546875);
}

static int
draw_glyph(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	unsigned char code, int x, int y, int scale, int shadow, int centred)
{
	const struct mh_ui_region *region;
	static const float white[4] = {1, 1, 1, 1}, black[4] = {0, 0, 0, 1};
	static const float red[4] = {200.0f / 255, 30.0f / 255, 30.0f / 255, 1};

	region = &assets->hardware_glyphs[code];

	if (centred)
		x -= (int)assets->hardware_cell_width / 2;

	if (shadow &&
		!mh_ui_quad_scaled(list, &assets->atlas, region,
			(float)(x + shadow * scale),
			(float)(y + shadow * scale),
			(float)(region->width * scale),
			(float)(region->height * scale), black))
		return 0;

	return mh_ui_quad_scaled(list, &assets->atlas, region, (float)x,
		(float)y, (float)(region->width * scale),
		(float)(region->height * scale), scale == 10 ? red : white);
}

static int
draw_centered_line(const struct mh_hud_assets *assets, struct mh_ui_list *list,
	const char *text, int y)
{
	const unsigned char *p;
	unsigned int width;
	int x;

	width = 0;

	for (p = (const unsigned char *)text; *p != 0; p++)
		width += assets->hardware_advance[*p];

	x = (int)(list->width / 2) - (int)(width / 2);

	for (p = (const unsigned char *)text; *p != 0; p++) {
		if (!draw_glyph(assets, list, *p, x, y, 1, 2, 0))
			return 0;

		x += (int)assets->hardware_advance[*p];
	}

	return 1;
}

int
mh_hud_secrets_draw(const struct mh_hud_assets *assets,
	const struct mh_hud_secrets *secrets, struct mh_ui_list *list)
{
	static const unsigned char title[] = "HCR&D";
	unsigned int i, offset, px, py;
	int x, y, base;

	if (secrets->notice == MH_HUD_SECRET_NONE)
		return 1;

	if (secrets->notice > MH_HUD_SECRET_SCROLLER ||
		secrets->phase_x > SINE_SAMPLES ||
		secrets->phase_y > SINE_SAMPLES ||
		secrets->bounce_phase > SINE_SAMPLES ||
		assets->secret_scroll_length >= sizeof(assets->secret_scroll))
		return 0;

	if (secrets->notice == MH_HUD_SECRET_CLUB)
		return draw_centered_line(assets, list, "WELCOME TO CLUB TBL!",
			(int)list->height / 2) &&
			draw_centered_line(assets, list,
				"Tonights Guest DJ: Randolph Carter!",
				(int)list->height / 2 +
					(int)assets->hardware_height + 2);

	px = secrets->bounce_phase;

	/*
	 * The original unsigned subtraction puts the title offscreen below
	 * 500px.
	 */
	if (list->width >= 500)
		for (i = 0; i < sizeof(title) - 1; i++) {
			x = (int)((list->width - 500) / 2) + 100 * (int)i;
			y = (int)list->height -
				10 * (int)assets->hardware_height -
				(int)(fabs(sine_sample(px)) * 200);

			if (!draw_glyph(assets, list, title[i], x, y, 10, 2, 0))
				return 0;

			px = wrap_phase(px + 200);
		}

	if (assets->secret_scroll_length == 0 || !secrets->scroll_started)
		return 1;

	if (secrets->scroll_offset >= assets->secret_scroll_length ||
		secrets->scroll_x < 0 || secrets->scroll_x > SCROLL_X_LIMIT)
		return 0;

	offset = secrets->scroll_offset;
	px = secrets->phase_x;
	py = secrets->phase_y;
	base = secrets->scroll_x - 50 - (int)assets->hardware_cell_width;

	while (base <
		(int)list->width + 50 + (int)assets->hardware_cell_width) {
		x = base + (int)(sine_sample(px) * 50);
		y = 200 + (int)(sine_sample(py) * 50);

		/*
		 * Fully offscreen letters need not enter the bounded quad
		 * builder.
		 */
		if (x - (int)assets->hardware_cell_width / 2 <
				(int)list->width &&
			x + (int)assets->hardware_cell_width / 2 > 0 &&
			!draw_glyph(assets, list,
				(unsigned char)assets->secret_scroll[offset], x,
				y, 1, 0, 1))
			return 0;

		base += 8;
		offset = (offset + 1) % assets->secret_scroll_length;
		px = wrap_phase(px + 50);
		py = wrap_phase(py + 200);
	}

	return 1;
}
