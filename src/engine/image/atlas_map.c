#include <stdlib.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/image/atlas_map.h"

#define DIMENSION_LIMIT 16384

void
mh_atlas_map_free(struct mh_atlas_map *map)
{
	if (map == NULL)
		return;

	free(map->entries);
	memset(map, 0, sizeof(*map));
}

static enum mh_error_code
check_entry(const struct mh_atlas_map *remap,
	const struct mh_atlas_map_entry *entry, struct mh_error *error_out)
{
	unsigned long placed_width, placed_height;
	unsigned int i;
	unsigned char byte;

	for (i = 0; i < sizeof(entry->name) && entry->name[i] != 0; i++) {
		byte = (unsigned char)entry->name[i];

		if (byte < 32 || byte > 126 || byte == '/' || byte == '\\' ||
			byte == ':')
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid remap texture name");
	}

	if (i == 0 || i == sizeof(entry->name))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated remap texture name");

	if (entry->page >= remap->pages || entry->transpose > 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remap atlas selection");

	if (entry->source_width == 0 || entry->source_width > DIMENSION_LIMIT ||
		entry->source_height == 0 ||
		entry->source_height > DIMENSION_LIMIT ||
		entry->scaled_width == 0 || entry->scaled_height == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remap dimensions");

	placed_width =
		entry->transpose ? entry->scaled_height : entry->scaled_width;
	placed_height =
		entry->transpose ? entry->scaled_width : entry->scaled_height;

	if (entry->x > remap->width || placed_width > remap->width - entry->x ||
		entry->y > remap->height ||
		placed_height > remap->height - entry->y)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"remap rectangle exceeds atlas");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_atlas_map_entry(const struct mh_atlas_map *remap, unsigned long index,
	struct mh_atlas_map_entry *output, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (remap == NULL || remap->entries == NULL || output == NULL ||
		index >= remap->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remap entry request");

	status = check_entry(remap, &remap->entries[index], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = remap->entries[index];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_atlas_map_find(const struct mh_atlas_map *remap, const char *name,
	struct mh_atlas_map_entry *output, struct mh_error *error_out)
{
	struct mh_atlas_map_entry entry;
	enum mh_error_code status;
	unsigned long n;

	if (remap == NULL || remap->entries == NULL || name == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid texture remap lookup");

	for (n = 0; n < remap->count; n++) {
		status = mh_atlas_map_entry(remap, n, &entry, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (mh_ascii_equal(name, entry.name))
			break;
	}

	if (n == remap->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"texture missing from track remap");

	*output = entry;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_atlas_map_group(const struct mh_atlas_map *remap, const char *name,
	struct mh_texture_groups *groups, unsigned int *index,
	struct mh_error *error_out)
{
	struct mh_atlas_map_entry entry;
	enum mh_error_code status;
	unsigned int i;

	if (groups == NULL || index == NULL ||
		groups->count > MH_TEXTURE_GROUPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid texture registration");

	status = mh_atlas_map_find(remap, name, &entry, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < groups->count && groups->pages[i] != entry.page; i++) {
	}

	if (i == MH_TEXTURE_GROUPS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"too many registered texture groups");

	if (i == groups->count) {
		groups->pages[i] = entry.page;
		groups->count++;
	}

	*index = i;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
