#include <stdlib.h>
#include <string.h>

#include "engine/assets/remap.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

#define ENTRY_LIMIT 65536
#define PAGE_LIMIT 4096
#define DIMENSION_LIMIT 16384
#define SCALE_LIMIT 16

enum {
	HEADER_BYTES = 28,
	ENTRY_BYTES = 49,
	NAME_BYTES = 13,
	HEADER_ENTRY_COUNT = 0,
	HEADER_PAGES = 4,
	HEADER_CUTOUT_PAGES = 8,
	HEADER_WIDTH = 12,
	HEADER_HEIGHT = 16,
	HEADER_SCALE = 20,
	ENTRY_PAGE = 13,
	ENTRY_SAVED_HANDLE = 17,
	ENTRY_X = 21,
	ENTRY_Y = 25,
	ENTRY_TRANSPOSE = 29,
	ENTRY_SOURCE_WIDTH = 33,
	ENTRY_SOURCE_HEIGHT = 37,
	ENTRY_SCALED_WIDTH = 41,
	ENTRY_SCALED_HEIGHT = 45
};

static void
read_entry(const unsigned char *data, struct mh_atlas_map_entry *entry)
{
	memset(entry, 0, sizeof(*entry));
	memcpy(entry->name, data, NAME_BYTES);
	entry->page = mh_read_u32le(data + ENTRY_PAGE);
	entry->saved_handle = mh_read_u32le(data + ENTRY_SAVED_HANDLE);
	entry->x = mh_read_u32le(data + ENTRY_X);
	entry->y = mh_read_u32le(data + ENTRY_Y);
	entry->transpose = mh_read_u32le(data + ENTRY_TRANSPOSE);
	entry->source_width = mh_read_u32le(data + ENTRY_SOURCE_WIDTH);
	entry->source_height = mh_read_u32le(data + ENTRY_SOURCE_HEIGHT);
	entry->scaled_width = mh_read_u32le(data + ENTRY_SCALED_WIDTH);
	entry->scaled_height = mh_read_u32le(data + ENTRY_SCALED_HEIGHT);
}

enum mh_error_code
mh_atlas_remap_decode(const unsigned char *data, size_t size,
	struct mh_atlas_map *output, struct mh_error *error_out)
{
	struct mh_atlas_map remap;
	unsigned long i;
	double value;
	enum mh_error_code status;

	if (data == NULL || output == NULL || size < HEADER_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short remap header");

	memset(&remap, 0, sizeof(remap));
	remap.count = mh_read_u32le(data + HEADER_ENTRY_COUNT);
	remap.pages = mh_read_u32le(data + HEADER_PAGES);
	remap.cutout_pages = mh_read_u32le(data + HEADER_CUTOUT_PAGES);
	remap.width = mh_read_u32le(data + HEADER_WIDTH);
	remap.height = mh_read_u32le(data + HEADER_HEIGHT);

	if (remap.count == 0 || remap.count > ENTRY_LIMIT ||
		remap.count != (size - HEADER_BYTES) / ENTRY_BYTES ||
		(size - HEADER_BYTES) % ENTRY_BYTES != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remap record count");

	if (remap.pages == 0 || remap.pages > PAGE_LIMIT ||
		remap.cutout_pages > remap.pages || remap.width == 0 ||
		remap.width > DIMENSION_LIMIT || remap.height == 0 ||
		remap.height > DIMENSION_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remap atlas header");

	for (i = 0; i < 2; i++) {
		if (!mh_read_f32le(data + HEADER_SCALE + i * 4, &value) ||
			!(value > 0 && value <= SCALE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid remap scale");

		remap.scale[i] = (float)value;
	}

	if (sizeof(*remap.entries) > (size_t)-1 / remap.count)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"atlas map allocation overflow");

	remap.entries = malloc((size_t)remap.count * sizeof(*remap.entries));

	if (remap.entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding atlas map");

	for (i = 0; i < remap.count; i++) {
		read_entry(data + HEADER_BYTES + (size_t)i * ENTRY_BYTES,
			&remap.entries[i]);
		status = mh_atlas_map_entry(
			&remap, i, &remap.entries[i], error_out);

		if (status != MH_ERROR_NONE) {
			mh_atlas_map_free(&remap);
			return status;
		}
	}

	*output = remap;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
