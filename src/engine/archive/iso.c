#include <stdlib.h>
#include <string.h>

#include "engine/archive/iso.h"
#include "engine/archive/path.h"
#include "engine/core/binary.h"
#include "engine/core/text.h"
#include "interfaces/archive/archive_backend.h"

#define DESCRIPTOR_SIGNATURE "CD001"

enum {
	DESCRIPTOR_SIGNATURE_OFFSET = 1,
	DESCRIPTOR_VERSION = 6,
	DESCRIPTOR_PRIMARY = 1,
	DESCRIPTOR_SUPPLEMENTARY = 2,
	DESCRIPTOR_TERMINATOR = 255,
	DESCRIPTOR_VOLUME_LE = 80,
	DESCRIPTOR_VOLUME_BE = 84,
	DESCRIPTOR_ESCAPES = 88,
	DESCRIPTOR_BLOCK_SIZE_LE = 128,
	DESCRIPTOR_BLOCK_SIZE_BE = 130,
	RECORD_EXTENDED_ATTRIBUTES = 1,
	RECORD_EXTENT_LE = 2,
	RECORD_EXTENT_BE = 6,
	RECORD_SIZE_LE = 10,
	RECORD_SIZE_BE = 14,
	RECORD_FLAGS = 25,
	RECORD_FILE_UNIT = 26,
	RECORD_INTERLEAVE_GAP = 27,
	RECORD_VOLUME_LE = 28,
	RECORD_VOLUME_BE = 30,
	RECORD_NAME_SIZE = 32,
	RECORD_NAME = 33,
	RECORD_DIRECTORY = 2,
	RECORD_MULTI_EXTENT = 128,
	DESCRIPTOR_FIRST_BLOCK = 16,
	DESCRIPTOR_LIMIT = 32,
	ROOT_RECORD_OFFSET = 156,
	DIRECTORY_RECORD_MINIMUM = 34,
	DIRECTORY_DEPTH_LIMIT = 16,
	DIRECTORY_BYTE_LIMIT = 1024 * 1024
};

struct iso_member {
	struct mh_archive_entry	entry;
	unsigned long		offset;
};

struct iso_archive {
	struct mh_disc_image	image;
	char			*path;
	struct iso_member	*members;
	size_t			count, capacity, records;
	int			joliet;
};

static unsigned long
read_big_word(const unsigned char *bytes)
{
	return ((unsigned long)bytes[0] << 24) |
		((unsigned long)bytes[1] << 16) |
		((unsigned long)bytes[2] << 8) | bytes[3];
}

static int
read_extent(const struct iso_archive *iso, const unsigned char *record,
	unsigned long *block, unsigned long *length)
{
	unsigned long extent, size;

	extent = mh_read_u32le(record + RECORD_EXTENT_LE);
	size = mh_read_u32le(record + RECORD_SIZE_LE);

	if (record[0] < DIRECTORY_RECORD_MINIMUM ||
		record[RECORD_EXTENDED_ATTRIBUTES] != 0 ||
		extent != read_big_word(record + RECORD_EXTENT_BE) ||
		size != read_big_word(record + RECORD_SIZE_BE) ||
		record[RECORD_FILE_UNIT] != 0 ||
		record[RECORD_INTERLEAVE_GAP] != 0 ||
		mh_read_u16le(record + RECORD_VOLUME_LE) != 1 ||
		record[RECORD_VOLUME_BE] != 0 ||
		record[RECORD_VOLUME_BE + 1] != 1 ||
		extent > iso->image.blocks ||
		size > (iso->image.blocks - extent) * MH_DISC_BLOCK_BYTES)
		return 0;

	*block = extent;
	*length = size;

	return 1;
}

static enum mh_error_code
select_root(struct iso_archive *iso,
	unsigned char root[DIRECTORY_RECORD_MINIMUM],
	struct mh_error *error_out)
{
	unsigned char descriptor[MH_DISC_BLOCK_BYTES];
	unsigned long sector, volume, selected_volume;
	int found, terminated, select;
	enum mh_error_code status;

	found = 0;
	terminated = 0;
	selected_volume = 0;

	for (sector = DESCRIPTOR_FIRST_BLOCK;
		sector < DESCRIPTOR_FIRST_BLOCK + DESCRIPTOR_LIMIT; sector++) {
		status = mh_disc_image_read(&iso->image,
			sector * MH_DISC_BLOCK_BYTES, descriptor,
			sizeof(descriptor), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (memcmp(descriptor + DESCRIPTOR_SIGNATURE_OFFSET,
			DESCRIPTOR_SIGNATURE,
			sizeof(DESCRIPTOR_SIGNATURE) - 1) != 0 ||
			descriptor[DESCRIPTOR_VERSION] != 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid ISO volume descriptor");

		if (descriptor[0] == DESCRIPTOR_TERMINATOR) {
			terminated = 1;
			break;
		}

		select = descriptor[0] == DESCRIPTOR_PRIMARY && !found;

		if (descriptor[0] == DESCRIPTOR_SUPPLEMENTARY &&
			descriptor[DESCRIPTOR_ESCAPES] == '%' &&
			descriptor[DESCRIPTOR_ESCAPES + 1] == '/' &&
			(descriptor[DESCRIPTOR_ESCAPES + 2] == '@' ||
				descriptor[DESCRIPTOR_ESCAPES + 2] == 'C' ||
				descriptor[DESCRIPTOR_ESCAPES + 2] == 'E')) {
			select = 1;
			iso->joliet = 1;
		}

		if (!select)
			continue;

		volume = mh_read_u32le(descriptor + DESCRIPTOR_VOLUME_LE);

		if (volume == 0 || volume > iso->image.blocks ||
			volume !=
				read_big_word(
					descriptor + DESCRIPTOR_VOLUME_BE) ||
			mh_read_u16le(descriptor + DESCRIPTOR_BLOCK_SIZE_LE) !=
				MH_DISC_BLOCK_BYTES ||
			descriptor[DESCRIPTOR_BLOCK_SIZE_BE] !=
				(MH_DISC_BLOCK_BYTES >> 8) ||
			descriptor[DESCRIPTOR_BLOCK_SIZE_BE + 1] !=
				(MH_DISC_BLOCK_BYTES & 255) ||
			descriptor[ROOT_RECORD_OFFSET] !=
				DIRECTORY_RECORD_MINIMUM)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported ISO volume size or root record");

		memcpy(root, descriptor + ROOT_RECORD_OFFSET,
			DIRECTORY_RECORD_MINIMUM);
		selected_volume = volume;
		found = 1;
	}

	if (!found || !terminated)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing ISO root or descriptor terminator");

	iso->image.blocks = selected_volume;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
entry_name(const struct iso_archive *iso, const unsigned char *record,
	char name[256], struct mh_error *error_out)
{
	size_t length, step, i, count;

	length = record[RECORD_NAME_SIZE];
	step = iso->joliet ? 2 : 1;

	if (length == 0 || length % step != 0 ||
		length > record[0] - (unsigned int)RECORD_NAME)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ISO entry name length");

	count = 0;

	for (i = 0; i < length; i += step) {
		if (iso->joliet && record[RECORD_NAME + i] != 0)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"ISO name is outside the supplied disc character set");

		name[count++] = (char)record[RECORD_NAME + i + step - 1];

		if (name[count - 1] == '\0')
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"NUL in ISO filename");
	}

	if (count >= 2 && name[count - 2] == ';' && name[count - 1] == '1')
		count -= 2;

	name[count] = '\0';

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
add_file(struct iso_archive *iso, const char *path, unsigned long block,
	unsigned long size, struct mh_error *error_out)
{
	struct iso_member *members, *member;
	size_t capacity, i;

	if (size != (size_t)size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ISO member exceeds native size range");

	for (i = 0; i < iso->count; i++) {
		if (mh_ascii_equal(iso->members[i].entry.path, path))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate ISO member path");
	}

	if (iso->count == iso->capacity) {
		capacity = iso->capacity == 0 ? 128 : iso->capacity * 2;

		if (capacity > MH_ARCHIVE_MEMBER_LIMIT)
			capacity = MH_ARCHIVE_MEMBER_LIMIT;

		if (capacity <= iso->capacity ||
			capacity > (size_t)-1 / sizeof(*members))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"ISO member limit exceeded");

		members = realloc(iso->members, capacity * sizeof(*members));

		if (members == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory reading ISO files");

		iso->members = members;
		iso->capacity = capacity;
	}

	member = &iso->members[iso->count++];
	strcpy(member->entry.path, path);
	member->entry.size = (size_t)size;
	member->offset = block * MH_DISC_BLOCK_BYTES;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code	read_directory(struct iso_archive *,
	const char *, unsigned long, unsigned long, unsigned int,
	struct mh_error *);

static enum mh_error_code
read_record(struct iso_archive *iso, const char *parent,
	const unsigned char *record, unsigned int depth,
	struct mh_error *error_out)
{
	char name[256], path[MH_ARCHIVE_PATH_SIZE];
	unsigned long block, length;
	enum mh_error_code status;

	if (++iso->records > MH_ARCHIVE_MEMBER_LIMIT ||
		!read_extent(iso, record, &block, &length) ||
		(record[RECORD_FLAGS] & RECORD_MULTI_EXTENT) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or unsupported ISO directory record");

	if (record[RECORD_NAME_SIZE] == 1 && record[RECORD_NAME] <= 1)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = entry_name(iso, record, name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_archive_path(parent, name, path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((record[RECORD_FLAGS] & RECORD_DIRECTORY) != 0)
		return read_directory(
			iso, path, block, length, depth + 1, error_out);

	return add_file(iso, path, block, length, error_out);
}

static enum mh_error_code
read_directory(struct iso_archive *iso, const char *parent, unsigned long block,
	unsigned long length, unsigned int depth, struct mh_error *error_out)
{
	unsigned char *bytes;
	size_t offset, record_size, remaining;
	enum mh_error_code status;

	if (depth >= DIRECTORY_DEPTH_LIMIT || length == 0 ||
		length > DIRECTORY_BYTE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ISO directory limit exceeded");

	bytes = malloc((size_t)length);

	if (bytes == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading ISO directory");

	status = mh_disc_image_read(&iso->image, block * MH_DISC_BLOCK_BYTES,
		bytes, (size_t)length, error_out);
	offset = 0;

	while (status == MH_ERROR_NONE && offset < length) {
		record_size = bytes[offset];
		remaining = MH_DISC_BLOCK_BYTES - offset % MH_DISC_BLOCK_BYTES;

		if (record_size == 0) {
			offset += remaining;
			continue;
		}

		if (record_size < DIRECTORY_RECORD_MINIMUM ||
			record_size > length - offset ||
			record_size > remaining) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated ISO directory record");
			break;
		}

		status = read_record(
			iso, parent, bytes + offset, depth, error_out);
		offset += record_size;
	}

	free(bytes);

	return status;
}

static size_t
member_count(const void *context)
{
	const struct iso_archive *iso;

	iso = context;

	return iso->count;
}

static const struct mh_archive_entry *
member_entry(const void *context, size_t index)
{
	const struct iso_archive *iso;

	iso = context;

	return &iso->members[index].entry;
}

static enum mh_error_code
read_member(void *context, size_t index, struct mh_file *output,
	struct mh_error *error_out)
{
	struct iso_archive *iso;
	struct mh_file file;
	enum mh_error_code status;

	iso = context;
	file.size = iso->members[index].entry.size;
	file.data = malloc(file.size == 0 ? 1 : file.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading ISO member");

	status = mh_disc_image_read(&iso->image, iso->members[index].offset,
		file.data, file.size, error_out);

	if (status != MH_ERROR_NONE) {
		free(file.data);
		return status;
	}

	*output = file;

	return status;
}

static void
close_iso(void *context)
{
	struct iso_archive *iso;

	iso = context;
	free(iso->members);
	free(iso->path);
	free(iso);
}

enum mh_error_code
mh_iso_open_track(const struct mh_resource_source *source, const char *path,
	enum mh_disc_format format, unsigned long track_bytes,
	struct mh_archive **output, struct mh_error *error_out)
{
	static const struct mh_archive_ops ops = {
		member_count, member_entry, read_member, close_iso};

	struct iso_archive *iso;
	unsigned char root[DIRECTORY_RECORD_MINIMUM];
	unsigned long block, length;
	enum mh_error_code status;

	if (path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ISO open arguments");

	iso = calloc(1, sizeof(*iso));

	if (iso == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening ISO");

	iso->path = malloc(strlen(path) + 1);

	if (iso->path == NULL) {
		close_iso(iso);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying ISO path");
	}

	strcpy(iso->path, path);
	status = mh_disc_image_init_track(
		source, iso->path, format, track_bytes, &iso->image, error_out);

	if (status == MH_ERROR_NONE)
		status = select_root(iso, root, error_out);

	if (status == MH_ERROR_NONE &&
		(!read_extent(iso, root, &block, &length) ||
			(root[25] & 2) == 0))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ISO root directory");

	if (status == MH_ERROR_NONE)
		status = read_directory(iso, "", block, length, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_archive_create(&ops, iso, output, error_out);

	if (status != MH_ERROR_NONE)
		close_iso(iso);

	return status;
}

enum mh_error_code
mh_iso_open(const struct mh_resource_source *source, const char *path,
	enum mh_disc_format format, struct mh_archive **output,
	struct mh_error *error_out)
{
	return mh_iso_open_track(source, path, format, 0, output, error_out);
}
