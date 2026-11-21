#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/retro.h"

#define FONT_CELL_LIMIT 16
#define FONT_ATLAS_LIMIT 2048

static void
append_polygon(struct mh_retro_draw *draw,
	const struct mh_retro_draw_vertex *quad, unsigned int count)
{
	static const unsigned int corners[MH_RETRO_QUAD_VERTICES] = {
		0, 1, 2, 0, 2, 3};

	unsigned int i, vertex_count;

	vertex_count = count == 3 ? 3U : (unsigned int)MH_RETRO_QUAD_VERTICES;

	for (i = 0; i < vertex_count; i++)
		draw->vertices[draw->count++] = quad[corners[i]];
}

static enum mh_error_code
measure_story_line(const struct mh_hud_assets *hud, const unsigned char *text,
	unsigned int *line_width, struct mh_error *error_out)
{
	const struct mh_ui_region *region;
	unsigned int character, width;

	if (memchr(text, 0, MH_RETRO_LINE_BYTES) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated retro story line");

	width = 0;

	for (character = 0; text[character] != 0; character++) {
		region = &hud->hardware_glyphs[text[character]];

		if (region->x >= hud->atlas.image.width ||
			region->y >= hud->atlas.image.height ||
			region->width > hud->atlas.image.width - region->x ||
			region->height > hud->atlas.image.height - region->y ||
			region->width > FONT_CELL_LIMIT ||
			region->height != hud->hardware_height ||
			hud->hardware_advance[text[character]] !=
				region->width + 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retro glyph region");

		width += hud->hardware_advance[text[character]];
	}

	*line_width = width;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_story(struct mh_retro_draw *output, const struct mh_retro_assets *assets,
	const struct mh_hud_assets *hud, unsigned int width,
	unsigned int height, const struct mh_retro_frame *frame,
	struct mh_error *error_out)
{
	struct mh_retro_vertex glyph[4];
	struct mh_retro_draw_vertex quad[4];
	const struct mh_ui_region *region;
	const unsigned char *text;
	enum mh_error_code status;
	unsigned int line, character, corner, line_width;
	int x;
	float xy[4][2];

	memset(quad, 0, sizeof(quad));
	line_width = 0;

	for (line = 0; line < MH_RETRO_VISIBLE_LINES; line++) {
		text = assets->text[frame->lines[line]];

		status = measure_story_line(hud, text, &line_width, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		x = -(int)(line_width >> 1);

		for (character = 0; text[character] != 0; character++) {
			region = &hud->hardware_glyphs[text[character]];

			for (corner = 0; corner < 4; corner++) {
				xy[corner][0] = (float)(x +
					(corner == 1 || corner == 2
							? (int)region->width
							: 0));
				xy[corner][1] = corner >= 2
					? (float)(region->height - 1)
					: 0;

				/*
				 * Subtract .5 in Glide's 256-unit texture space
				 * before weighting by the constant W.
				 */
				quad[corner].uv[0] =
					(float)((region->x + .49 -
							hud->hardware_cell_width /
								32.0 +
							(corner == 1 || corner == 2
									? region->width
									: 0)) /
						hud->atlas.image.width);
				quad[corner].uv[1] =
					(float)((region->y + .49 -
							hud->hardware_height /
								32.0 +
							(corner >= 2 ? region->height -
										1
								: 0)) /
						hud->atlas.image.height);
			}

			status = mh_retro_glyph(width, height,
				hud->hardware_height, frame->center,
				frame->scroll[line], frame->wave,
				(const float (*)[2])xy, glyph, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			for (corner = 0; corner < 4; corner++)
				quad[corner].vertex = glyph[corner];

			append_polygon(output, quad, 4);
			x += (int)hud->hardware_advance[text[character]];
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_meshes(struct mh_retro_draw *output,
	const struct mh_retro_assets *assets, unsigned int width,
	unsigned int height, float time, struct mh_error *error_out)
{
	struct mh_retro_polygon polygons[MH_RETRO_MAX_FACES];
	struct mh_retro_draw_vertex quad[4];
	enum mh_error_code status;
	unsigned int i, j, k, count;

	memset(quad, 0, sizeof(quad));

	for (i = 0; i < MH_RETRO_INSTANCE_COUNT; i++) {
		status = mh_retro_polygons(assets, i, width, height, time,
			polygons, &count, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (j = 0; j < count; j++) {
			for (k = 0; k < polygons[j].count; k++)
				quad[k].vertex = polygons[j].vertices[k];

			append_polygon(output, quad, polygons[j].count);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_retro_draw(const struct mh_retro_assets *assets,
	const struct mh_hud_assets *hud, unsigned int width,
	unsigned int height, float dt, const struct mh_retro_state *state,
	struct mh_retro_draw *output, struct mh_error *error_out)
{
	struct mh_retro_frame frame;
	struct mh_retro_vertex backdrop[MH_RETRO_BACKDROP_VERTICES];
	enum mh_error_code status;
	unsigned int i;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing retro draw output");

	output->count = 0;

	if (assets == NULL || hud == NULL || hud->hardware_height < 1 ||
		hud->hardware_height > FONT_CELL_LIMIT ||
		hud->hardware_cell_width < 1 ||
		hud->hardware_cell_width > FONT_CELL_LIMIT ||
		hud->atlas.image.width < 1 ||
		hud->atlas.image.width > FONT_ATLAS_LIMIT ||
		hud->atlas.image.height < 1 ||
		hud->atlas.image.height > FONT_ATLAS_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid retro font");

	status = mh_retro_frame(dt, state, &frame, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_retro_backdrop(
			width, height, frame.center, backdrop, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_RETRO_BACKDROP_VERTICES; i++) {
		output->vertices[i].vertex = backdrop[i];
		output->vertices[i].uv[0] = output->vertices[i].uv[1] = 0;
	}

	output->count = MH_RETRO_BACKDROP_VERTICES;

	status = append_story(
		output, assets, hud, width, height, &frame, error_out);

	if (status != MH_ERROR_NONE)
		goto failure;

	output->text_end = output->count;
	status = append_meshes(
		output, assets, width, height, state->time, error_out);

	if (status != MH_ERROR_NONE)
		goto failure;

	output->width = width;
	output->height = height;
	output->next = frame.next;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

failure:
	output->count = 0;

	return status;
}
