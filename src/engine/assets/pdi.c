#include <string.h>

#include "engine/assets/pdi.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/text.h"

enum {
	HEADER_BYTES = 32,
	HEADER_CAPACITY = 4,
	HEADER_ENTRY_COUNT = 8,
	HEADER_BITMAP_OFFSET = 12,
	HEADER_DIRECTORY_OFFSET = 16,
	HEADER_PAYLOAD_OFFSET = 20,
	HEADER_PAYLOAD_SIZE = 24,
	ENTRY_BYTES = 52,
	ENTRY_PAYLOAD_OFFSET = 12,
	ENTRY_SIZE_OFFSET = 16,
	ENTRY_NAME_OFFSET = 20,
	NAME_BYTES = 32
};

static int
slot_occupied(const struct mh_pdi *archive, unsigned long slot)
{
	return (archive->data[archive->bitmap_offset + (size_t)(slot / 8)] &
		(1U << (slot % 8))) != 0;
}

int
mh_pdi_entry(const struct mh_pdi *archive, unsigned long slot,
	struct mh_pdi_entry *entry)
{
	const unsigned char *record;

	if (slot >= archive->capacity || !slot_occupied(archive, slot)) {
		return 0;
	}

	record = archive->data + archive->directory_offset +
		(size_t)slot * ENTRY_BYTES;
	entry->name = (const char *)record + ENTRY_NAME_OFFSET;
	entry->data = archive->data + archive->payload_offset +
		(size_t)mh_read_u32le(record + ENTRY_PAYLOAD_OFFSET);
	entry->size = (size_t)mh_read_u32le(record + ENTRY_SIZE_OFFSET);

	return 1;
}

int
mh_pdi_find(const struct mh_pdi *archive, const char *name,
	struct mh_pdi_entry *entry)
{
	unsigned long slot;
	struct mh_pdi_entry candidate;

	for (slot = 0; slot < archive->capacity; slot++) {
		if (mh_pdi_entry(archive, slot, &candidate) &&
			mh_ascii_equal(name, candidate.name)) {
			*entry = candidate;
			return 1;
		}
	}

	return 0;
}

enum mh_error_code
mh_pdi_parse(const unsigned char *data, size_t size, struct mh_pdi *archive,
	struct mh_error *error_out)
{
	struct mh_pdi parsed;
	unsigned long capacity, entry_count, occupied_count;
	unsigned long bitmap_offset, directory_offset, payload_offset;
	unsigned long payload_size;
	unsigned long slot;
	unsigned long offset;
	unsigned long length;
	const unsigned char *record;

	if (data == NULL || archive == NULL || size < HEADER_BYTES ||
		memcmp(data, "PDI1", 4) != 0) {
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"missing or unsupported PDI header");
	}

	capacity = mh_read_u32le(data + HEADER_CAPACITY);
	entry_count = mh_read_u32le(data + HEADER_ENTRY_COUNT);
	bitmap_offset = mh_read_u32le(data + HEADER_BITMAP_OFFSET);
	directory_offset = mh_read_u32le(data + HEADER_DIRECTORY_OFFSET);
	payload_offset = mh_read_u32le(data + HEADER_PAYLOAD_OFFSET);
	payload_size = mh_read_u32le(data + HEADER_PAYLOAD_SIZE);

	if (entry_count > capacity || bitmap_offset < HEADER_BYTES ||
		bitmap_offset > size ||
		capacity / 8 + (capacity % 8 != 0) > size - bitmap_offset ||
		directory_offset < HEADER_BYTES || directory_offset > size ||
		capacity > (size - directory_offset) / ENTRY_BYTES ||
		payload_offset < HEADER_BYTES || payload_offset > size ||
		payload_size > size - payload_offset) {
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"PDI table or payload extends outside the file");
	}

	parsed.data = data;
	parsed.capacity = capacity;
	parsed.count = entry_count;
	parsed.bitmap_offset = (size_t)bitmap_offset;
	parsed.directory_offset = (size_t)directory_offset;
	parsed.payload_offset = (size_t)payload_offset;
	occupied_count = 0;

	for (slot = 0; slot < parsed.capacity; slot++) {
		if (!slot_occupied(&parsed, slot)) {
			continue;
		}

		occupied_count++;
		record = data + parsed.directory_offset +
			(size_t)slot * ENTRY_BYTES;
		offset = mh_read_u32le(record + ENTRY_PAYLOAD_OFFSET);
		length = mh_read_u32le(record + ENTRY_SIZE_OFFSET);

		if (offset > payload_size || length > payload_size - offset) {
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"PDI entry extends outside the payload");
		}

		if (record[ENTRY_NAME_OFFSET] == 0 ||
			memchr(record + ENTRY_NAME_OFFSET, 0, NAME_BYTES) ==
				NULL) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"PDI entry name is empty or unterminated");
		}
	}

	if (occupied_count != parsed.count) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"PDI count does not match its occupied slots");
	}

	*archive = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
