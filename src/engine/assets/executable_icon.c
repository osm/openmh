#include <string.h>

#include "engine/assets/executable_icon.h"
#include "engine/assets/ico.h"
#include "engine/core/binary.h"

#define RESOURCE_DIRECTORY_FLAG 0x80000000UL
#define PE_SIGNATURE "PE\0\0"

#define RESOURCE_OFFSET_MASK 0x7fffffffUL

enum {
	DOS_HEADER_SIZE = 64,
	DOS_PE_OFFSET = 60,
	PE_HEADER_SIZE = 24,
	PE_SECTION_COUNT = 6,
	PE_OPTIONAL_SIZE = 20,
	PE32_DIRECTORY_COUNT = 92,
	PE_DIRECTORY_SIZE_OFFSET = 4,
	PE_DIRECTORY_BYTES = 8,
	PE32_RESOURCE_DIRECTORY_COUNT = 3,
	SECTION_ADDRESS = 12,
	SECTION_RAW_SIZE = 16,
	SECTION_RAW_OFFSET = 20,
	RESOURCE_NAMED_COUNT = 12,
	RESOURCE_ID_COUNT = 14,
	RESOURCE_ENTRY_VALUE = 4,
	RESOURCE_DATA_LENGTH = 4,
	RESOURCE_TREE_LEVELS = 3,
	PE_SECTION_SIZE = 40,
	PE32_MAGIC = 0x10b,
	PE32_RESOURCE_OFFSET = 112,
	RESOURCE_HEADER_SIZE = 16,
	RESOURCE_ENTRY_SIZE = 8,
	RESOURCE_ICON = 3
};

struct executable_resources {
	const unsigned char	*data, *sections, *root;
	size_t			size, root_size;
	unsigned int		section_count;
};

static const unsigned char *
map_rva(const struct executable_resources *resources, unsigned long address,
	unsigned long length)
{
	const unsigned char *section;
	unsigned long base, raw_size, offset, delta;
	unsigned int i;

	for (i = 0; i < resources->section_count; i++) {
		section = resources->sections + (size_t)i * PE_SECTION_SIZE;
		base = mh_read_u32le(section + SECTION_ADDRESS);
		raw_size = mh_read_u32le(section + SECTION_RAW_SIZE);
		offset = mh_read_u32le(section + SECTION_RAW_OFFSET);

		if (address < base || address - base >= raw_size)
			continue;

		delta = address - base;

		if (length > raw_size - delta || offset > resources->size ||
			delta > resources->size - (size_t)offset ||
			length > resources->size - (size_t)offset -
					(size_t)delta)
			return NULL;

		return resources->data + (size_t)offset + (size_t)delta;
	}

	return NULL;
}

static int
read_resources(const unsigned char *data, size_t size,
	struct executable_resources *resources)
{
	const unsigned char *header, *optional;
	unsigned long offset, address, length;
	unsigned int optional_size;
	size_t sections;

	if (data == NULL || size < DOS_HEADER_SIZE ||
		memcmp(data, "MZ", 2) != 0)
		return 0;

	offset = mh_read_u32le(data + DOS_PE_OFFSET);

	if (offset > size || PE_HEADER_SIZE > size - (size_t)offset)
		return 0;

	header = data + (size_t)offset;

	if (memcmp(header, PE_SIGNATURE, sizeof(PE_SIGNATURE) - 1) != 0)
		return 0;

	optional_size = (unsigned int)mh_read_u16le(header + PE_OPTIONAL_SIZE);
	sections = (size_t)offset + PE_HEADER_SIZE;

	if (optional_size < PE32_RESOURCE_OFFSET + PE_DIRECTORY_BYTES ||
		optional_size > size - sections)
		return 0;

	optional = data + sections;

	if (mh_read_u16le(optional) != PE32_MAGIC ||
		mh_read_u32le(optional + PE32_DIRECTORY_COUNT) <
			PE32_RESOURCE_DIRECTORY_COUNT)
		return 0;

	sections += optional_size;
	resources->data = data;
	resources->size = size;
	resources->sections = data + sections;
	resources->section_count =
		(unsigned int)mh_read_u16le(header + PE_SECTION_COUNT);

	if (resources->section_count > (size - sections) / PE_SECTION_SIZE)
		return 0;

	address = mh_read_u32le(optional + PE32_RESOURCE_OFFSET);
	length = mh_read_u32le(
		optional + PE32_RESOURCE_OFFSET + PE_DIRECTORY_SIZE_OFFSET);
	resources->root = map_rva(resources, address, length);
	resources->root_size = (size_t)length;

	return resources->root != NULL && length >= RESOURCE_HEADER_SIZE;
}

/* Select a numbered type, or the first entry at the ID/language levels. */
static int
resource_entry(const struct executable_resources *resources,
	unsigned long directory, unsigned long type, unsigned long *value)
{
	const unsigned char *header, *entry;
	unsigned long count, i;
	size_t offset;

	if (directory > resources->root_size ||
		RESOURCE_HEADER_SIZE > resources->root_size - (size_t)directory)
		return 0;

	header = resources->root + (size_t)directory;
	count = mh_read_u16le(header + RESOURCE_NAMED_COUNT) +
		mh_read_u16le(header + RESOURCE_ID_COUNT);
	offset = (size_t)directory + RESOURCE_HEADER_SIZE;

	if (count > (resources->root_size - offset) / RESOURCE_ENTRY_SIZE)
		return 0;

	for (i = 0; i < count; i++) {
		entry = resources->root + offset +
			(size_t)i * RESOURCE_ENTRY_SIZE;

		if (type == 0 || mh_read_u32le(entry) == type) {
			*value = mh_read_u32le(entry + RESOURCE_ENTRY_VALUE);
			return 1;
		}
	}

	return 0;
}

enum mh_error_code
mh_executable_icon_decode(const unsigned char *data, size_t size,
	struct mh_image *output, struct mh_error *error_out)
{
	struct executable_resources resources;
	const unsigned char *entry, *bitmap;
	unsigned long offset, length;
	unsigned int level;

	if (!read_resources(data, size, &resources))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid executable resource table");

	offset = 0;

	for (level = 0; level < RESOURCE_TREE_LEVELS; level++) {
		if (!resource_entry(&resources, offset,
			level == 0 ? RESOURCE_ICON : 0, &offset))
			return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
				"executable icon resource not found");

		if ((offset & RESOURCE_DIRECTORY_FLAG) !=
			(level < RESOURCE_TREE_LEVELS - 1
					? RESOURCE_DIRECTORY_FLAG
					: 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid executable icon tree");

		offset &= RESOURCE_OFFSET_MASK;
	}

	if (offset > resources.root_size ||
		RESOURCE_HEADER_SIZE > resources.root_size - (size_t)offset)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated executable icon entry");

	entry = resources.root + (size_t)offset;
	length = mh_read_u32le(entry + RESOURCE_DATA_LENGTH);
	bitmap = map_rva(&resources, mh_read_u32le(entry), length);

	if (bitmap == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid executable icon range");

	return mh_image_ico_decode_dib(
		bitmap, (size_t)length, output, error_out);
}
