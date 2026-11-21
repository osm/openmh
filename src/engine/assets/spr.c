#include <stdlib.h>
#include <string.h>

#include "engine/assets/spr.h"
#include "engine/assets/spr_format.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

static enum mh_error_code
read_sprite(const unsigned char *data, struct mh_spr_entry *entry,
	struct mh_error *error_out)
{
	size_t size, offset, end, minimum_frame_offset, row, minimum_row_offset;
	unsigned long value;
	unsigned int frame, y;

	data += entry->offset;
	size = entry->end - entry->offset;

	if (size < SPR_FRAME_TABLE_OFFSET + SPR_OFFSET_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short SPR sprite header");

	value = mh_read_u32le(data);

	if (value == 0 || value > SPR_FRAME_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid SPR frame count");

	entry->frames = (unsigned int)value;
	value = mh_read_u32le(data + SPR_WIDTH_OFFSET);

	if (value == 0 || value > SPR_DIMENSION_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid SPR width");

	entry->width = (unsigned int)value;
	value = mh_read_u32le(data + SPR_HEIGHT_OFFSET);

	if (value == 0 || value > SPR_DIMENSION_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid SPR height");

	entry->height = (unsigned int)value;

	if (SPR_FRAME_TABLE_OFFSET + entry->frames * SPR_OFFSET_BYTES > size)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short SPR frame table");

	minimum_frame_offset =
		SPR_FRAME_TABLE_OFFSET + entry->frames * SPR_OFFSET_BYTES;

	for (frame = 0; frame < entry->frames; frame++) {
		value = mh_read_u32le(data + SPR_FRAME_TABLE_OFFSET +
			frame * SPR_OFFSET_BYTES);

		if (value < minimum_frame_offset || value >= size)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid SPR frame offset");

		offset = (size_t)value;
		end = size;

		if (frame + 1 < entry->frames) {
			value = mh_read_u32le(data + SPR_FRAME_TABLE_OFFSET +
				SPR_OFFSET_BYTES + frame * SPR_OFFSET_BYTES);

			if (value <= offset || value > size)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"unordered SPR frames");

			end = (size_t)value;
		}

		if ((size_t)entry->height * SPR_OFFSET_BYTES > end - offset)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"short SPR row table");

		minimum_row_offset = (size_t)entry->height * SPR_OFFSET_BYTES;

		for (y = 0; y < entry->height; y++) {
			value = mh_read_u32le(
				data + offset + y * SPR_OFFSET_BYTES);

			if (value < minimum_row_offset || value >= end - offset)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid SPR row offset");

			row = (size_t)value;
			minimum_row_offset = row + 1;
		}

		minimum_frame_offset = end;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sprite_spr_read(const unsigned char *data, size_t size,
	struct mh_spr *output, struct mh_error *error_out)
{
	struct mh_spr archive;
	struct mh_spr_entry *entry;
	unsigned long value;
	size_t minimum;
	unsigned int i, j, k;
	enum mh_error_code status;

	if (data == NULL || output == NULL || size < SPR_HEADER_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short SPR archive");

	memset(&archive, 0, sizeof(archive));
	value = mh_read_u32le(data) & (unsigned long)MH_I32_MAX;

	if (value == 0 || value > SPR_ENTRY_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SPR sprite count");

	archive.count = (unsigned int)value;
	archive.palette_offset =
		SPR_HEADER_BYTES + (size_t)archive.count * SPR_ENTRY_BYTES;
	minimum = archive.palette_offset + SPR_PALETTE_BYTES;

	if (minimum > size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"short SPR directory or palette");

	archive.entries = (struct mh_spr_entry *)calloc(
		archive.count, sizeof(*archive.entries));

	if (archive.entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading SPR");

	archive.data = data;
	archive.size = size;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < archive.count; i++) {
		entry = &archive.entries[i];
		memcpy(entry->name,
			data + SPR_HEADER_BYTES + (size_t)i * SPR_ENTRY_BYTES,
			MH_SPR_NAME_BYTES);

		for (j = 0; j < MH_SPR_NAME_BYTES && entry->name[j] != 0; j++)
			if ((unsigned char)entry->name[j] < 32 ||
				(unsigned char)entry->name[j] > 126)
				break;

		if (j == 0 || (j < MH_SPR_NAME_BYTES && entry->name[j] != 0)) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid SPR name");
			break;
		}

		for (k = j; k < MH_SPR_NAME_BYTES; k++)
			if (entry->name[k] != 0)
				break;

		if (k != MH_SPR_NAME_BYTES) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid SPR name padding");
			break;
		}

		value = mh_read_u32le(data + SPR_HEADER_BYTES +
			MH_SPR_NAME_BYTES + (size_t)i * SPR_ENTRY_BYTES);

		if (value < minimum || value >= size) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid SPR sprite offset");
			break;
		}

		entry->offset = (size_t)value;
		entry->end = size;

		if (i + 1 < archive.count) {
			value = mh_read_u32le(data + SPR_HEADER_BYTES +
				MH_SPR_NAME_BYTES + SPR_ENTRY_BYTES +
				(size_t)i * SPR_ENTRY_BYTES);

			if (value <= entry->offset || value > size) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"unordered SPR sprites");
				break;
			}

			entry->end = (size_t)value;
		}

		status = read_sprite(data, entry, error_out);

		if (status != MH_ERROR_NONE)
			break;

		minimum = entry->end;
	}

	if (status != MH_ERROR_NONE) {
		mh_sprite_spr_free(&archive);
		return status;
	}

	*output = archive;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_sprite_spr_free(struct mh_spr *archive)
{
	if (archive != NULL) {
		free(archive->entries);
		memset(archive, 0, sizeof(*archive));
	}
}

const struct mh_spr_entry *
mh_sprite_spr_find(const struct mh_spr *archive, const char *name)
{
	char key[MH_SPR_NAME_BYTES];
	unsigned int i, length;

	if (archive == NULL || archive->entries == NULL || name == NULL)
		return NULL;

	for (length = 0; length <= MH_SPR_NAME_BYTES && name[length] != 0;
		length++) {
	}

	if (length == 0 || length > MH_SPR_NAME_BYTES)
		return NULL;

	memset(key, 0, sizeof(key));
	memcpy(key, name, length);

	for (i = 0; i < archive->count; i++)
		if (memcmp(key, archive->entries[i].name, MH_SPR_NAME_BYTES) ==
			0)
			return &archive->entries[i];

	return NULL;
}
