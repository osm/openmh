#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/atlas.h"

/* Prepared software loading art can span the full 2160-pixel viewport. */
#define ATLAS_SIZE_LIMIT 4096

enum mh_error_code
mh_ui_atlas_init(unsigned int width, unsigned int height,
	struct mh_ui_atlas *atlas, struct mh_error *error_out)
{
	struct mh_ui_atlas result;

	if (atlas == NULL || width < 3 || height < 3 ||
		width > ATLAS_SIZE_LIMIT || height > ATLAS_SIZE_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid UI atlas size");

	memset(&result, 0, sizeof(result));
	result.image.width = width;
	result.image.height = height;
	result.x = result.y = 1;
	result.image.rgba = (unsigned char *)calloc((size_t)width * height, 4);

	if (result.image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating UI atlas");

	*atlas = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ui_atlas_add(struct mh_ui_atlas *atlas, const struct mh_image *image,
	struct mh_ui_region *region, struct mh_error *error_out)
{
	unsigned int x, y, row, row_height;

	if (atlas == NULL || image == NULL || region == NULL ||
		atlas->image.rgba == NULL || image->rgba == NULL ||
		image->width == 0 || image->height == 0 ||
		atlas->image.width < 3 || atlas->image.height < 3 ||
		image->width > atlas->image.width - 2 ||
		image->height > atlas->image.height - 2)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid UI atlas image");

	x = atlas->x;
	y = atlas->y;
	row_height = atlas->row_height;

	if (x >= atlas->image.width ||
		image->width + 1 > atlas->image.width - x) {
		x = 1;
		y += row_height + 2;
		row_height = 0;
	}

	if (y >= atlas->image.height ||
		image->height + 1 > atlas->image.height - y)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "UI atlas is full");

	for (row = 0; row < image->height; row++)
		memcpy(atlas->image.rgba +
				((size_t)(y + row) * atlas->image.width + x) *
					4,
			image->rgba + (size_t)row * image->width * 4,
			(size_t)image->width * 4);

	region->x = x;
	region->y = y;
	region->width = image->width;
	region->height = image->height;
	atlas->x = x + image->width + 2;
	atlas->y = y;
	atlas->row_height =
		image->height > row_height ? image->height : row_height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
