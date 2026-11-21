#include <string.h>

#include "engine/core/limits.h"
#include "engine/ui/draw.h"

#define COORDINATE_LIMIT 16384

void
mh_ui_begin(struct mh_ui_list *list, unsigned int width, unsigned int height)
{
	list->count = 0;
	list->text_count = 0;
	list->text_size = 0;
	list->width = width;
	list->height = height;
}

enum mh_error_code
mh_ui_word_text(struct mh_ui_list *list, const struct mh_font_program *font,
	const unsigned char *text, size_t length, int x, int y,
	unsigned int foreground, enum mh_ui_text_align align,
	struct mh_error *error_out)
{
	struct mh_ui_word_text *command;
	size_t i;

	if (list == NULL || font == NULL || font->bytes == NULL ||
		(length != 0 && text == NULL) || length > MH_UI_TEXT_LIMIT ||
		list->width == 0 || list->height == 0 ||
		list->width > COORDINATE_LIMIT ||
		list->height > COORDINATE_LIMIT ||
		list->count > MH_UI_MAX_QUADS * 6 || list->count % 6 != 0 ||
		list->text_count > MH_UI_MAX_TEXTS ||
		list->text_size > MH_UI_TEXT_BYTES || x < -COORDINATE_LIMIT ||
		x > COORDINATE_LIMIT || y < -COORDINATE_LIMIT ||
		y > COORDINATE_LIMIT || foreground > MH_U16_MAX ||
		align < MH_UI_TEXT_LEFT || align > MH_UI_TEXT_CENTER)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid UI word text");

	for (i = 0; i < length; i++)
		if (text[i] == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"embedded NUL in UI word text");

	if (length == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (list->text_count == MH_UI_MAX_TEXTS ||
		length > MH_UI_TEXT_BYTES - list->text_size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"UI word text capacity exceeded");

	if (list->text_count != 0 &&
		list->texts[list->text_count - 1].before > list->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid UI word text order");

	command = &list->texts[list->text_count];
	command->font = font;
	command->before = list->count;
	command->offset = list->text_size;
	command->length = (unsigned int)length;
	command->foreground = foreground;
	command->x = x;
	command->y = y;
	command->align = align;
	memmove(list->text + list->text_size, text, length);
	list->text_size += (unsigned int)length;
	list->text_count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_ui_quad(struct mh_ui_list *list, const struct mh_ui_atlas *atlas,
	const struct mh_ui_region *region, float x, float y,
	const float color[4])
{
	if (region == NULL)
		return 0;

	return mh_ui_quad_scaled(list, atlas, region, x, y,
		(float)region->width, (float)region->height, color);
}

int
mh_ui_quad_scaled(struct mh_ui_list *list, const struct mh_ui_atlas *atlas,
	const struct mh_ui_region *region, float x, float y, float width,
	float height, const float color[4])
{
	float left, top, right, bottom, u[2], v[2], px[2], py[2];
	unsigned int i;
	struct mh_ui_vertex *vertex;
	static const unsigned int corners[6][2] = {
		{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};

	if (list == NULL || atlas == NULL || region == NULL || color == NULL ||
		list->width == 0 || list->height == 0 ||
		list->width > COORDINATE_LIMIT ||
		list->height > COORDINATE_LIMIT ||
		!(x >= -COORDINATE_LIMIT && x <= COORDINATE_LIMIT) ||
		!(y >= -COORDINATE_LIMIT && y <= COORDINATE_LIMIT) ||
		!(width >= 0 && width <= COORDINATE_LIMIT) ||
		!(height >= 0 && height <= COORDINATE_LIMIT) ||
		region->x > atlas->image.width ||
		region->width > atlas->image.width - region->x ||
		region->y > atlas->image.height ||
		region->height > atlas->image.height - region->y ||
		atlas->image.width == 0 || atlas->image.height == 0)
		return 0;

	for (i = 0; i < 4; i++)
		if (!(color[i] >= 0 && color[i] <= 1))
			return 0;

	if (region->width == 0 || region->height == 0 || width == 0 ||
		height == 0)
		return 1;

	left = x < 0 ? 0 : x;
	top = y < 0 ? 0 : y;
	right = x + width;
	bottom = y + height;

	if (right > list->width)
		right = (float)list->width;

	if (bottom > list->height)
		bottom = (float)list->height;

	if (left >= right || top >= bottom)
		return 1;

	if (list->count > MH_UI_MAX_QUADS * 6 - 6)
		return 0;

	u[0] = (region->x + (left - x) * (region->width / width)) /
		atlas->image.width;
	u[1] = (region->x + (right - x) * (region->width / width)) /
		atlas->image.width;
	v[0] = (region->y + (top - y) * (region->height / height)) /
		atlas->image.height;
	v[1] = (region->y + (bottom - y) * (region->height / height)) /
		atlas->image.height;
	px[0] = left;
	px[1] = right;
	py[0] = top;
	py[1] = bottom;

	for (i = 0; i < 6; i++) {
		vertex = &list->vertices[list->count + i];
		vertex->position[0] = px[corners[i][0]];
		vertex->position[1] = py[corners[i][1]];
		vertex->uv[0] = u[corners[i][0]];
		vertex->uv[1] = v[corners[i][1]];
		memcpy(vertex->color, color, sizeof(vertex->color));
	}

	list->count += 6;

	return 1;
}
