#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_text.h"
#include "backends/render/software_ui.h"
#include "engine/core/binary.h"

/* Prepared software loading art can span the full 2160-pixel viewport. */
#define ATLAS_LIMIT 4096
#define COORDINATE_LIMIT 16384
#define QUAD_VERTICES 6
#define COLOR_MAX 255
#define BLEND_ROUND 127

void
mh_software_ui_free(struct mh_software_ui *ui)
{
	free(ui->atlas.rgba);
	memset(ui, 0, sizeof(*ui));
}

enum mh_error_code
mh_software_ui_upload(struct mh_software_ui *ui,
	const struct mh_ui_atlas *atlas, struct mh_error *error_out)
{
	struct mh_image image;
	size_t size;

	if (ui == NULL || atlas == NULL || ui->atlas.rgba != NULL ||
		atlas->image.rgba == NULL || atlas->image.width == 0 ||
		atlas->image.height == 0 || atlas->image.width > ATLAS_LIMIT ||
		atlas->image.height > ATLAS_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or already uploaded software UI atlas");

	image = atlas->image;
	size = (size_t)image.width * image.height * 4;
	image.rgba = malloc(size);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying software UI atlas");

	memcpy(image.rgba, atlas->image.rgba, size);
	ui->atlas = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_ui_check(const struct mh_software_ui *ui,
	const struct mh_software_surface *surface,
	const struct mh_ui_list *list, struct mh_error *error_out)
{
	const struct mh_ui_vertex *vertex;
	const struct mh_ui_word_text *command;
	unsigned int i, channel, previous_vertex;
	enum mh_error_code status;

	if (list == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (ui == NULL || ui->atlas.rgba == NULL || surface == NULL ||
		surface->pixels == NULL || surface->width == 0 ||
		surface->height == 0 || surface->width > COORDINATE_LIMIT ||
		surface->height > COORDINATE_LIMIT ||
		surface->pitch < (size_t)surface->width * 2 ||
		surface->height > surface->size / surface->pitch ||
		surface->bits[0] != 5 || surface->bits[2] != 5 ||
		(surface->bits[1] != 5 && surface->bits[1] != 6) ||
		surface->shift[0] != surface->bits[1] + 5 ||
		surface->shift[1] != 5 || surface->shift[2] != 0 ||
		list->width != surface->width ||
		list->height != surface->height ||
		list->count > MH_UI_MAX_QUADS * QUAD_VERTICES ||
		list->count % QUAD_VERTICES != 0 ||
		list->text_count > MH_UI_MAX_TEXTS ||
		list->text_size > MH_UI_TEXT_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software UI frame or uninitialized overlay");

	for (i = 0; i < list->count; i++) {
		vertex = &list->vertices[i];

		for (channel = 0; channel < 2; channel++)
			if (!(vertex->position[channel] >= -COORDINATE_LIMIT &&
				vertex->position[channel] <=
					COORDINATE_LIMIT) ||
				!(vertex->uv[channel] >= 0 &&
					vertex->uv[channel] <= 1))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software UI position or texture coordinate");

		for (channel = 0; channel < 4; channel++)
			if (!(vertex->color[channel] >= 0 &&
				vertex->color[channel] <= 1))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software UI color");
	}

	previous_vertex = 0;

	for (i = 0; i < list->text_count; i++) {
		command = &list->texts[i];

		if (command->before < previous_vertex ||
			command->before > list->count ||
			command->before % QUAD_VERTICES != 0 ||
			command->offset > list->text_size ||
			command->length > list->text_size - command->offset ||
			command->align < MH_UI_TEXT_LEFT ||
			command->align > MH_UI_TEXT_CENTER)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software UI text order or bounds");

		status = mh_software_text_check(surface, command->font,
			list->text + command->offset, command->length,
			command->x, command->y, command->foreground,
			command->align, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		previous_vertex = command->before;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static double
edge_value(const struct mh_ui_vertex *a, const struct mh_ui_vertex *b, double x,
	double y)
{
	return ((double)b->position[0] - a->position[0]) *
		(y - a->position[1]) -
		((double)b->position[1] - a->position[1]) *
		(x - a->position[0]);
}

static int
edge_inclusive(const struct mh_ui_vertex *a, const struct mh_ui_vertex *b)
{
	return b->position[1] < a->position[1] ||
		(b->position[1] == a->position[1] &&
			b->position[0] > a->position[0]);
}

static double
clamp_unit(double value)
{
	return value < 0 ? 0 : value > 1 ? 1 : value;
}

static void
draw_pixel(const struct mh_software_ui *ui,
	const struct mh_software_surface *surface,
	const struct mh_ui_vertex *v[3], const double weights[3],
	unsigned int x, unsigned int y)
{
	const unsigned char *source;
	unsigned char *destination;
	double uv[2], color[4];
	unsigned int i, channel, texel_x, texel_y, alpha, foreground,
		background;
	unsigned int mask, packed, result;

	for (channel = 0; channel < 2; channel++) {
		uv[channel] = 0;

		for (i = 0; i < 3; i++)
			uv[channel] += weights[i] * v[i]->uv[channel];
	}

	for (channel = 0; channel < 4; channel++) {
		color[channel] = 0;

		for (i = 0; i < 3; i++)
			color[channel] += weights[i] * v[i]->color[channel];

		color[channel] = clamp_unit(color[channel]);
	}

	texel_x = (unsigned int)(clamp_unit(uv[0]) * ui->atlas.width);
	texel_y = (unsigned int)(clamp_unit(uv[1]) * ui->atlas.height);

	if (texel_x == ui->atlas.width)
		texel_x--;

	if (texel_y == ui->atlas.height)
		texel_y--;

	source = ui->atlas.rgba +
		((size_t)texel_y * ui->atlas.width + texel_x) * 4;
	alpha = (unsigned int)(source[3] * color[3] + .5);

	if (alpha == 0)
		return;

	destination =
		surface->pixels + (size_t)y * surface->pitch + (size_t)x * 2;
	packed = mh_read_u16le(destination);
	result = 0;

	for (channel = 0; channel < 3; channel++) {
		mask = (1U << surface->bits[channel]) - 1;
		foreground =
			(unsigned int)(source[channel] * color[channel] + .5);
		background = (packed >> surface->shift[channel]) & mask;
		background = (background * COLOR_MAX + mask / 2) / mask;
		foreground =
			(foreground * alpha + background * (COLOR_MAX - alpha) +
				BLEND_ROUND) /
			COLOR_MAX;
		result |= (foreground >> (8 - surface->bits[channel]))
			<< surface->shift[channel];
	}

	destination[0] = (unsigned char)result;
	destination[1] = (unsigned char)(result >> 8);
}

static void
draw_triangle(const struct mh_software_ui *ui,
	const struct mh_software_surface *surface,
	const struct mh_ui_vertex vertices[3])
{
	const struct mh_ui_vertex *v[3], *swap;
	double area, weights[3], low[2], high[2], limit;
	unsigned int i, axis, begin[2], end[2], x, y;
	int included[3];

	v[0] = &vertices[0];
	v[1] = &vertices[1];
	v[2] = &vertices[2];
	area = edge_value(v[0], v[1], v[2]->position[0], v[2]->position[1]);

	if (area == 0)
		return;

	if (area < 0) {
		swap = v[1];
		v[1] = v[2];
		v[2] = swap;
		area = -area;
	}

	for (axis = 0; axis < 2; axis++) {
		low[axis] = high[axis] = v[0]->position[axis];

		for (i = 1; i < 3; i++) {
			if (v[i]->position[axis] < low[axis])
				low[axis] = v[i]->position[axis];

			if (v[i]->position[axis] > high[axis])
				high[axis] = v[i]->position[axis];
		}

		limit = axis == 0 ? surface->width : surface->height;
		low[axis] = low[axis] < 0 ? 0 : low[axis];
		high[axis] = high[axis] > limit ? limit : high[axis];

		if (low[axis] >= high[axis])
			return;

		begin[axis] = (unsigned int)ceil(low[axis] - .5);
		end[axis] = (unsigned int)ceil(high[axis] - .5);
	}

	for (i = 0; i < 3; i++)
		included[i] = edge_inclusive(v[(i + 1) % 3], v[(i + 2) % 3]);

	for (y = begin[1]; y < end[1]; y++)
		for (x = begin[0]; x < end[0]; x++) {
			for (i = 0; i < 3; i++) {
				weights[i] = edge_value(v[(i + 1) % 3],
					v[(i + 2) % 3], x + .5, y + .5);

				if (weights[i] < 0 ||
					(weights[i] == 0 && !included[i]))
					break;

				weights[i] /= area;
			}

			if (i == 3)
				draw_pixel(ui, surface, v, weights, x, y);
		}
}

void
mh_software_ui_draw(const struct mh_software_ui *ui,
	const struct mh_software_surface *surface,
	const struct mh_ui_list *list)
{
	const struct mh_ui_word_text *command;
	unsigned int i, vertex;

	if (list == NULL)
		return;

	vertex = 0;

	for (i = 0; i < list->text_count; i++) {
		command = &list->texts[i];

		while (vertex < command->before) {
			draw_triangle(ui, surface, list->vertices + vertex);
			vertex += 3;
		}

		/*
		 * The complete immutable list was checked before frame
		 * preparation.
		 */
		(void)mh_software_text_draw(surface, command->font,
			list->text + command->offset, command->length,
			command->x, command->y, command->foreground,
			command->align, NULL);
	}

	while (vertex < list->count) {
		draw_triangle(ui, surface, list->vertices + vertex);
		vertex += 3;
	}
}
