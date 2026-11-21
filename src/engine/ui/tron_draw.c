#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/tron_draw.h"

#define VIEWPORT_LIMIT 16384
#define ATLAS_LIMIT 2048
#define GLYPH_LIMIT 16
#define GLYPH_ADVANCE_LIMIT 17

static int
append_quad(struct mh_ui_list *list, const struct mh_ui_vertex quad[4])
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	unsigned int i;

	if (list->count > MH_UI_MAX_QUADS * 6 - 6)
		return 0;

	for (i = 0; i < 6; i++)
		list->vertices[list->count + i] = quad[corners[i]];

	list->count += 6;

	return 1;
}

static int
region_valid(const struct mh_ui_atlas *atlas, const struct mh_ui_region *region)
{
	return region->x < atlas->image.width &&
		region->width <= atlas->image.width - region->x &&
		region->y < atlas->image.height &&
		region->height <= atlas->image.height - region->y;
}

static int
append_backdrop(const struct mh_hud_assets *assets,
	const struct mh_tron_intro *frame, struct mh_ui_list *list)
{
	struct mh_ui_vertex quad[4];
	const struct mh_ui_region *region;
	unsigned int corner;

	region = &assets->solid;

	if (!region_valid(&assets->atlas, region) || region->width != 1 ||
		region->height != 1 ||
		!(frame->band_top >= 0 &&
			frame->band_top < frame->band_bottom) ||
		!(frame->band_bottom <= list->height) ||
		!(frame->opacity >= 0 && frame->opacity <= 255))
		return 0;

	memset(quad, 0, sizeof(quad));

	for (corner = 0; corner < 4; corner++) {
		quad[corner].position[0] =
			corner == 1 || corner == 2 ? (float)list->width : 0;
		quad[corner].position[1] =
			corner >= 2 ? frame->band_bottom : frame->band_top;
		quad[corner].uv[0] =
			(region->x + .5f) / assets->atlas.image.width;
		quad[corner].uv[1] =
			(region->y + .5f) / assets->atlas.image.height;
		quad[corner].color[3] = corner >= 2 ? frame->opacity / 255 : 0;
	}

	if (!append_quad(list, quad))
		return 0;

	for (corner = 0; corner < 4; corner++) {
		quad[corner].position[1] =
			corner >= 2 ? (float)list->height : frame->band_bottom;
		quad[corner].color[3] = frame->opacity / 255;
	}

	if (!append_quad(list, quad))
		return 0;

	return 1;
}

static int
append_text(const struct mh_hud_assets *assets, unsigned int line, int x, int y,
	struct mh_ui_list *list)
{
	struct mh_ui_vertex quad[4];
	const struct mh_ui_region *region;
	const unsigned char *text;
	unsigned int corner;
	float alpha, logical_x, logical_y;

	text = (const unsigned char *)mh_tron_intro_text(line);

	if (text == NULL || x < -VIEWPORT_LIMIT || x > VIEWPORT_LIMIT ||
		y < -VIEWPORT_LIMIT || y > VIEWPORT_LIMIT)
		return 0;

	for (; *text != 0; text++) {
		region = &assets->hardware_glyphs[*text];

		if (!region_valid(&assets->atlas, region) ||
			region->width > GLYPH_LIMIT || region->height < 1 ||
			region->height > GLYPH_LIMIT ||
			assets->hardware_advance[*text] > GLYPH_ADVANCE_LIMIT)
			return 0;

		for (corner = 0; corner < 4; corner++) {
			logical_x = (float)(x +
				(corner == 1 || corner == 2 ? (int)region->width
							: 0));
			logical_y = (float)(y +
				(corner >= 2 ? (int)region->height - 1 : 0));

			if (mh_tron_intro_corner(list->width, list->height,
				logical_x, logical_y, quad[corner].position,
				&alpha, NULL) != MH_ERROR_NONE)
				return 0;

			/*
			 * Use a .49 texel inset; glyph height is cell-1.
			 * Relocation into the shared atlas is a native
			 * convention.
			 */
			quad[corner].uv[0] =
				(float)((region->x + .49 +
						(corner == 1 || corner == 2
								? region->width
								: 0)) /
					assets->atlas.image.width);
			quad[corner].uv[1] =
				(float)((region->y + .49 +
						(corner >= 2 ? region->height -
									1
							: 0)) /
					assets->atlas.image.height);
			quad[corner].color[0] = quad[corner].color[1] =
				quad[corner].color[2] = 1;
			quad[corner].color[3] = alpha / 255;
		}

		if (!append_quad(list, quad))
			return 0;

		x += (int)assets->hardware_advance[*text];
	}

	return 1;
}

int
mh_tron_intro_draw(const struct mh_hud_assets *assets,
	const struct mh_tron_intro *frame, struct mh_ui_list *list)
{
	unsigned int i, saved_count;

	if (assets == NULL || frame == NULL || list == NULL ||
		list->count > MH_UI_MAX_QUADS * 6 || list->width < 1 ||
		list->width > VIEWPORT_LIMIT || list->height < 1 ||
		list->height > VIEWPORT_LIMIT ||
		assets->atlas.image.width < 1 ||
		assets->atlas.image.width > ATLAS_LIMIT ||
		assets->atlas.image.height < 1 ||
		assets->atlas.image.height > ATLAS_LIMIT ||
		frame->count > MH_TRON_INTRO_VISIBLE_LINES ||
		(frame->backdrop != 0 && frame->backdrop != 1))
		return 0;

	saved_count = list->count;

	if (frame->backdrop && !append_backdrop(assets, frame, list))
		goto failure;

	for (i = 0; i < frame->count; i++)
		if (!append_text(assets, frame->lines[i].line,
			frame->lines[i].x, frame->lines[i].y, list))
			goto failure;

	return 1;

failure:
	list->count = saved_count;

	return 0;
}
