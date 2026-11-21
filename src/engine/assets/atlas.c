#include <stdlib.h>
#include <string.h>

#include "engine/assets/atlas.h"
#include "engine/catalog/track.h"
#include "engine/core/error.h"
#include "engine/image/collection_load.h"

#define PAGE_LIMIT 4096
#define DIMENSION_LIMIT 16384
#define PIXEL_LIMIT 16777216UL
#define UV_LIMIT 1048576

void
mh_atlas_free(struct mh_atlas *atlas)
{
	size_t i;

	if (atlas == NULL)
		return;

	for (i = 0; i < atlas->count; i++)
		free(atlas->pages[i].rgba);

	free(atlas->pages);
	free(atlas->formats);
	memset(atlas, 0, sizeof(*atlas));
}

static enum mh_error_code
prepare_shared_palette(const struct mh_image_palette *source_palette,
	struct mh_image_palette *shared_palette, const double brightness[3],
	enum mh_texture_format opaque_format, unsigned char background_rgba[4],
	struct mh_error *error_out)
{
	struct mh_image background_image;
	enum mh_error_code status;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (shared_palette->count == 0) {
		*shared_palette = *source_palette;
		memcpy(background_rgba, source_palette->rgb, 3);
		background_rgba[3] = 255;
		background_image.width = background_image.height = 1;
		background_image.rgba = background_rgba;
		status = mh_image_texture_brightness(
			&background_image, brightness, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_image_texture_format(
				&background_image, opaque_format, error_out);
	} else if (shared_palette->count != source_palette->count ||
		memcmp(shared_palette->rgb, source_palette->rgb,
			shared_palette->count * 3) != 0)
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"inconsistent shared P8 atlas palette");

	return status;
}

static void
fill_background(struct mh_image *page, const unsigned char background_rgba[4])
{
	size_t pixel, count;

	count = (size_t)page->width * page->height;

	for (pixel = 0; pixel < count; pixel++)
		memcpy(page->rgba + pixel * 4, background_rgba, 4);
}

enum mh_error_code
mh_atlas_build(const struct mh_image_decoder_provider *decoder,
	const struct mh_atlas_map *remap,
	const struct mh_image_collection *cutout_images,
	const struct mh_image_collection *opaque_images,
	const struct mh_track *track, const struct mh_atlas_options *options,
	struct mh_atlas *output, struct mh_error *error_out)
{
	struct mh_atlas built;
	struct mh_atlas_map_entry entry;
	struct mh_image_decode_options decode;
	struct mh_image image;
	struct mh_image_palette source_palette, shared_palette;
	enum mh_texture_format format, opaque_format;
	unsigned char *page_formats, background_rgba[4];
	unsigned long entry_index;
	size_t page_index, page_pixels;
	int cutout;
	enum mh_error_code status;

	if (options == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing atlas preparation options");

	opaque_format = options->opaque_format;
	decode.alpha = MH_IMAGE_ALPHA_INDEX_ZERO;
	decode.palette = NULL;

	if (remap == NULL || remap->entries == NULL || cutout_images == NULL ||
		opaque_images == NULL || track == NULL || output == NULL ||
		(options->shared_palette != 0 &&
			options->shared_palette != 1) ||
		(options->border != 0 &&
			options->border != MH_ATLAS_MIP_BORDER) ||
		(opaque_format == MH_TEXTURE_PALETTE_RGB &&
			!options->shared_palette) ||
		(opaque_format != MH_TEXTURE_PALETTE_RGB &&
			opaque_format != MH_TEXTURE_RGB565 &&
			opaque_format != MH_TEXTURE_RGB555 &&
			opaque_format != MH_TEXTURE_XRGB8888) ||
		(options->alpha_format != MH_TEXTURE_ARGB1555 &&
			options->alpha_format != MH_TEXTURE_ARGB4444) ||
		remap->pages == 0 || remap->pages > PAGE_LIMIT ||
		remap->width == 0 || remap->width > DIMENSION_LIMIT ||
		remap->height == 0 || remap->height > DIMENSION_LIMIT ||
		remap->width > PIXEL_LIMIT / remap->height / remap->pages)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid atlas page allocation");

	memset(&built, 0, sizeof(built));
	memset(&shared_palette, 0, sizeof(shared_palette));
	memset(&image, 0, sizeof(image));
	memset(background_rgba, 0, sizeof(background_rgba));
	background_rgba[3] = 255;
	built.border = options->border;
	page_pixels = (size_t)remap->width * remap->height;
	built.pages = (struct mh_image *)calloc(
		(size_t)remap->pages, sizeof(*built.pages));
	page_formats = (unsigned char *)calloc((size_t)remap->pages, 1);

	if (built.pages == NULL || page_formats == NULL) {
		free(built.pages);
		free(page_formats);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating atlas pages");
	}

	built.formats = page_formats;
	built.count = (size_t)remap->pages;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (entry_index = 0;
		status == MH_ERROR_NONE && entry_index < remap->count;
		entry_index++) {
		status = mh_atlas_map_entry(
			remap, entry_index, &entry, error_out);

		if (status != MH_ERROR_NONE)
			break;

		cutout = mh_track_texture_cutout(track, entry.name);
		format = cutout ? options->alpha_format : opaque_format;

		if (page_formats[entry.page] != 0 &&
			page_formats[entry.page] != format) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"mixed texture formats on one atlas page");
			break;
		}

		status = mh_image_collection_load(decoder,
			cutout ? cutout_images : opaque_images, entry.name,
			&decode, &image, &source_palette, error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (!cutout && options->shared_palette)
			status = prepare_shared_palette(&source_palette,
				&shared_palette, track->texture_brightness,
				opaque_format, background_rgba, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_image_texture_brightness(
				&image, track->texture_brightness, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_image_texture_format(
				&image, format, error_out);

		if (status == MH_ERROR_NONE && page_formats[entry.page] == 0) {
			built.pages[entry.page].width =
				(unsigned int)remap->width;
			built.pages[entry.page].height =
				(unsigned int)remap->height;
			built.pages[entry.page].rgba =
				(unsigned char *)calloc(page_pixels, 4);

			if (built.pages[entry.page].rgba == NULL)
				status = mh_error_set(error_out,
					MH_ERROR_MEMORY,
					"out of memory filling atlas page");
			else {
				page_formats[entry.page] =
					(unsigned char)format;

				if (!cutout)
					fill_background(
						&built.pages[entry.page],
						background_rgba);
			}
		}

		if (status == MH_ERROR_NONE)
			status = mh_atlas_copy(&entry, &image, options->border,
				&built.pages[entry.page], error_out);

		free(image.rgba);
		memset(&image, 0, sizeof(image));
	}

	for (page_index = 0;
		status == MH_ERROR_NONE && page_index < built.count;
		page_index++)
		if (page_formats[page_index] == 0) {
			page_formats[page_index] = (unsigned char)opaque_format;

			if (options->shared_palette &&
				shared_palette.count == 0) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"empty P8 atlas page has no shared palette");
				break;
			}

			built.pages[page_index].width =
				(unsigned int)remap->width;
			built.pages[page_index].height =
				(unsigned int)remap->height;
			built.pages[page_index].rgba =
				(unsigned char *)malloc(page_pixels * 4);

			if (built.pages[page_index].rgba == NULL)
				status = mh_error_set(error_out,
					MH_ERROR_MEMORY,
					"out of memory filling empty atlas page");
			else
				fill_background(&built.pages[page_index],
					background_rgba);
		}

	if (status != MH_ERROR_NONE)
		mh_atlas_free(&built);
	else
		*output = built;

	return status;
}

static int
valid_rectangle(const struct mh_atlas_map_entry *entry, unsigned int width,
	unsigned int height)
{
	unsigned long placed_width, placed_height;

	if (entry == NULL || width == 0 || width > DIMENSION_LIMIT ||
		height == 0 || height > DIMENSION_LIMIT ||
		entry->transpose > 1 || entry->source_width == 0 ||
		entry->source_width > DIMENSION_LIMIT ||
		entry->source_height == 0 ||
		entry->source_height > DIMENSION_LIMIT ||
		entry->scaled_width == 0 ||
		entry->scaled_width > DIMENSION_LIMIT ||
		entry->scaled_height == 0 ||
		entry->scaled_height > DIMENSION_LIMIT)
		return 0;

	placed_width =
		entry->transpose ? entry->scaled_height : entry->scaled_width;
	placed_height =
		entry->transpose ? entry->scaled_width : entry->scaled_height;

	return entry->x <= width && placed_width <= width - entry->x &&
		entry->y <= height && placed_height <= height - entry->y;
}

enum mh_error_code
mh_atlas_uv(const struct mh_atlas_map_entry *entry, unsigned int width,
	unsigned int height, unsigned int border, const float uv[2],
	float output[2], struct mh_error *error_out)
{
	double u, v;
	volatile float x, y;
	float result[2];

	if (!valid_rectangle(entry, width, height) || uv == NULL ||
		(border != 0 && border != MH_ATLAS_MIP_BORDER) ||
		entry->scaled_width <= 2 * border ||
		entry->scaled_height <= 2 * border || output == NULL ||
		entry->source_width < 2 || entry->source_height < 2 ||
		!(uv[0] >= -UV_LIMIT && uv[0] <= UV_LIMIT) ||
		!(uv[1] >= -UV_LIMIT && uv[1] <= UV_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid atlas coordinates");

	u = uv[entry->transpose ? 1 : 0];
	v = uv[entry->transpose ? 0 : 1];

	/*
	 * Keep the original binary32 page-pixel store before normalization.
	 * Transposition does not exchange either source/scaled dimension pair.
	 */
	x = (float)((u - .5) * (entry->scaled_width - 2 * border - 1) /
			(entry->source_width - 1) +
		.5 + border + entry->x);
	y = (float)((v - .5) * (entry->scaled_height - 2 * border - 1) /
			(entry->source_height - 1) +
		.5 + border + entry->y);
	result[0] = (float)((double)x / width);
	result[1] = (float)((double)y / height);
	output[0] = result[0];
	output[1] = result[1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
copy_resized(const struct mh_atlas_map_entry *entry,
	const struct mh_image *source, struct mh_image *page)
{
	unsigned long x, y, sx, sy, dx, dy;
	size_t source_offset, page_offset;
	float step_x, step_y;

	step_x = (float)((double)source->width / entry->scaled_width);
	step_y = (float)((double)source->height / entry->scaled_height);

	for (y = 0; y < entry->scaled_height; y++) {
		sy = (unsigned long)((y + .5) * (double)step_y);

		for (x = 0; x < entry->scaled_width; x++) {
			sx = (unsigned long)((x + .5) * (double)step_x);
			dx = entry->x + (entry->transpose ? y : x);
			dy = entry->y + (entry->transpose ? x : y);
			source_offset = ((size_t)sy * source->width + sx) * 4;
			page_offset = ((size_t)dy * page->width + dx) * 4;
			memcpy(page->rgba + page_offset,
				source->rgba + source_offset, 4);
		}
	}
}

static void
copy_rectangle(const struct mh_image *source, struct mh_image *page,
	unsigned int x, unsigned int y)
{
	unsigned int row;
	size_t bytes;

	bytes = (size_t)source->width * 4;

	for (row = 0; row < source->height; row++)
		memcpy(page->rgba + ((size_t)(y + row) * page->width + x) * 4,
			source->rgba + (size_t)row * bytes, bytes);
}

static enum mh_error_code
copy_padded(const struct mh_atlas_map_entry *entry,
	const struct mh_image *source, unsigned int border,
	struct mh_image *page, struct mh_error *error_out)
{
	struct mh_image resized, interior;
	struct mh_atlas_map_entry scaled;
	unsigned int i;

	resized.width = (unsigned int)(entry->transpose ? entry->scaled_height
							: entry->scaled_width);
	resized.height =
		(unsigned int)(entry->transpose ? entry->scaled_width
						: entry->scaled_height);
	interior.width = resized.width - 2 * border;
	interior.height = resized.height - 2 * border;
	resized.rgba = (unsigned char *)malloc(
		(size_t)resized.width * resized.height * 4);
	interior.rgba = (unsigned char *)malloc(
		(size_t)interior.width * interior.height * 4);

	if (resized.rgba == NULL || interior.rgba == NULL) {
		free(resized.rgba);
		free(interior.rgba);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing atlas border");
	}

	scaled = *entry;
	scaled.x = scaled.y = 0;
	copy_resized(&scaled, source, &resized);
	scaled.transpose = 0;
	scaled.scaled_width = interior.width;
	scaled.scaled_height = interior.height;
	copy_resized(&scaled, &resized, &interior);

	/* Overlapping strips, then the center. */
	for (i = 0; i <= 2 * border; i++)
		copy_rectangle(&interior, &resized, i, border);

	for (i = 0; i <= 2 * border; i++)
		copy_rectangle(&interior, &resized, border, i);

	copy_rectangle(&interior, &resized, border, border);
	copy_rectangle(
		&resized, page, (unsigned int)entry->x, (unsigned int)entry->y);
	free(interior.rgba);
	free(resized.rgba);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_atlas_copy(const struct mh_atlas_map_entry *entry,
	const struct mh_image *source, unsigned int border,
	struct mh_image *page, struct mh_error *error_out)
{
	if (source == NULL || page == NULL || source->rgba == NULL ||
		page->rgba == NULL || source->rgba == page->rgba ||
		!valid_rectangle(entry, page->width, page->height) ||
		(border != 0 && border != MH_ATLAS_MIP_BORDER) ||
		entry->scaled_width <= 2 * border ||
		entry->scaled_height <= 2 * border ||
		source->width != entry->source_width ||
		source->height != entry->source_height ||
		source->width > PIXEL_LIMIT / source->height ||
		page->width > PIXEL_LIMIT / page->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid atlas image rectangle");

	if (border != 0)
		return copy_padded(entry, source, border, page, error_out);

	copy_resized(entry, source, page);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
