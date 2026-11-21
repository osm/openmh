#include <string.h>

#include "engine/render/powervr_lamp_atlas.h"

#define GLOW_SIZE 128
#define FLARE_SIZE 64
#define SHADOW_ROW 64
#define SHADOW_ROWS 3
#define SHADOW_COLUMN 128
#define SHADOW_LEVELS 16
#define SHADOW_ALPHA_STEP 17
#define PALETTE_COLORS 256
#define COLOR_MAX 255
#define NAME_ROWS 8
#define NAME_CELL_HEIGHT 16
#define NAME_IMAGE_HEIGHT 15
#define NAME_SOURCE_HEIGHT_LIMIT 64

static int
valid_source(const struct mh_indexed_image *image, unsigned int size,
	unsigned int colors)
{
	size_t pixel_index;

	if (image == NULL || image->indices == NULL || image->width != size ||
		image->height != size || image->size != (size_t)size * size)
		return 0;

	for (pixel_index = 0; pixel_index < image->size; pixel_index++)
		if (image->indices[pixel_index] >= colors)
			return 0;

	return 1;
}

static void
copy_alpha(const struct mh_indexed_image *source,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	unsigned int column, int flare, unsigned char *pixels)
{
	const unsigned char *rgb;
	unsigned char *pixel;
	unsigned int x, y, alpha;

	for (y = 0; y < source->height; y++)
		for (x = 0; x < source->width; x++) {
			rgb = palette +
				source->indices[y * source->width + x] * 3;
			alpha = rgb[0] + rgb[1] + rgb[2];

			if (!flare)
				alpha /= 3;

			if (alpha > COLOR_MAX)
				alpha = COLOR_MAX;

			pixel = pixels +
				((size_t)y * MH_POWERVR_LAMP_ATLAS_SIZE +
					column + x) *
					4;
			memset(pixel, COLOR_MAX, 3);
			pixel[3] = (unsigned char)alpha;
		}
}

static void
shadow_ramp(unsigned char *pixels)
{
	unsigned char *row;
	unsigned int y, level;

	for (y = SHADOW_ROW; y < SHADOW_ROW + SHADOW_ROWS; y++) {
		row = pixels +
			((size_t)y * MH_POWERVR_LAMP_ATLAS_SIZE +
				SHADOW_COLUMN) *
				4;
		memset(row, 0, (SHADOW_LEVELS + 3) * 4);

		for (level = 0; level < SHADOW_LEVELS; level++)
			row[(level + 1) * 4 + 3] =
				(unsigned char)(level * SHADOW_ALPHA_STEP);

		/* Append literal RGBA words 000000ff. */
		row[(SHADOW_LEVELS + 1) * 4] = COLOR_MAX;
		row[(SHADOW_LEVELS + 2) * 4] = COLOR_MAX;
	}
}

enum mh_error_code
mh_powervr_lamp_atlas_prepare(const struct mh_indexed_image *glow,
	const struct mh_indexed_image *flare, struct mh_image *output,
	struct mh_error *error_out)
{
	size_t pixel_index, pixels;

	if (glow == NULL || glow->colors == 0 ||
		glow->colors > PALETTE_COLORS ||
		!valid_source(glow, GLOW_SIZE, glow->colors) ||
		!valid_source(flare, FLARE_SIZE, glow->colors) ||
		output == NULL || output->rgba == NULL ||
		output->width != MH_POWERVR_LAMP_ATLAS_SIZE ||
		output->height != MH_POWERVR_LAMP_ATLAS_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR lamp atlas");

	pixels = (size_t)output->width * output->height;

	for (pixel_index = 0; pixel_index < pixels; pixel_index++) {
		memcpy(output->rgba + pixel_index * 4, glow->palette, 3);
		output->rgba[pixel_index * 4 + 3] = 0;
	}

	copy_alpha(glow, glow->palette, 0, 0, output->rgba);
	copy_alpha(flare, glow->palette, GLOW_SIZE, 1, output->rgba);
	shadow_ramp(output->rgba);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_powervr_lamp_atlas_names(const struct mh_name_images *names,
	const unsigned char palette[MH_IMAGE_PALETTE_BYTES],
	struct mh_image *atlas, struct mh_name_label labels[MH_NAME_LABELS_MAX],
	struct mh_error *error_out)
{
	const struct mh_indexed_image *image;
	unsigned char *pixel;
	unsigned int slot, x, y, left, top, height, ink;

	if (names == NULL || palette == NULL || atlas == NULL ||
		labels == NULL || names->count < 1 ||
		names->count > MH_NAME_LABELS_MAX ||
		names->active_mask >= (1U << names->count) ||
		atlas->rgba == NULL ||
		atlas->width != MH_POWERVR_LAMP_ATLAS_SIZE ||
		atlas->height != MH_POWERVR_LAMP_ATLAS_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR name atlas");

	for (slot = 0; slot < names->count; slot++) {
		if (!(names->active_mask & (1U << slot)))
			continue;

		image = &names->images[slot];

		if (image->indices == NULL || image->width < 2 ||
			image->width > MH_POWERVR_NAME_CELL_WIDTH ||
			image->height < 2 ||
			image->height > NAME_SOURCE_HEIGHT_LIMIT ||
			image->size != (size_t)image->width * image->height)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid PowerVR name image");
	}

	for (y = GLOW_SIZE; y < atlas->height; y++)
		for (x = 0; x < atlas->width; x++) {
			pixel = atlas->rgba +
				((size_t)y * atlas->width + x) * 4;
			memcpy(pixel, palette, 3);
			pixel[3] = 0;
		}

	for (slot = 0; slot < MH_NAME_LABELS_MAX; slot++) {
		memset(labels[slot].uv, 0, sizeof(labels[slot].uv));

		if (slot >= names->count ||
			!(names->active_mask & (1U << slot)))
			continue;

		image = &names->images[slot];
		left = (slot / NAME_ROWS) * MH_POWERVR_NAME_CELL_WIDTH;
		top = GLOW_SIZE + (slot % NAME_ROWS) * NAME_CELL_HEIGHT;
		height = image->height < NAME_IMAGE_HEIGHT ? image->height
							: NAME_IMAGE_HEIGHT;
		labels[slot].uv[0] = (left + 1) / (float)atlas->width;
		labels[slot].uv[1] = (top + 1) / (float)atlas->height;
		labels[slot].uv[2] =
			(left + image->width - 1) / (float)atlas->width;
		labels[slot].uv[3] = (top + height - 1) / (float)atlas->height;

		/* Stamp nonzero indices as palette entry 255. */
		for (y = 0; y < height; y++)
			for (x = 0; x < image->width; x++) {
				ink = image->indices[y * image->width + x] != 0;
				pixel = atlas->rgba +
					((size_t)(top + y) * atlas->width +
						left + x) *
						4;
				memcpy(pixel,
					palette + (ink ? COLOR_MAX * 3 : 0), 3);
				pixel[3] = ink ? COLOR_MAX : 0;
			}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
