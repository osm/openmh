#include <stdlib.h>
#include <string.h>

#include "backends/archive/unshield_provider.h"
#include "backends/archive/unshield_zip.h"
#include "engine/archive/path.h"
#include "engine/archive/zip_layout.h"
#include "engine/core/binary.h"
#include "engine/core/text.h"
#include "interfaces/archive/archive_backend.h"

enum {
	SFX_NAME_OFFSET = 4,
	SFX_PACKED_SIZE_OFFSET = 8,
	SFX_RECORD_HEADER = 12,
	SFX_MEMBER_LIMIT = 64
};

struct sfx_member {
	struct mh_archive_entry	entry;
	const unsigned char	*data;
	size_t			packed;
};

struct sfx_archive {
	struct sfx_member	members[SFX_MEMBER_LIMIT];
	size_t			count;
	int			compressed;
};

/*
 * Decode each filename byte by rotating left, then XORing the repeating key.
 */
static enum mh_error_code
decode_name(const unsigned char *bytes, size_t size,
	char output[MH_ARCHIVE_PATH_SIZE], struct mh_error *error_out)
{
	static const unsigned char key[] = {
		0x95, 0x6b, 0xd3, 0xb5, 0x49, 0x9d, 0x1f, 0xa0};

	char path[MH_ARCHIVE_PATH_SIZE], *name, *parent;
	unsigned int byte, shift;
	size_t i;

	if (size == 0 || size >= sizeof(path))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SFX filename length");

	for (i = 0; i < size; i++) {
		shift = (unsigned int)((i + 1) % 8);
		byte = bytes[i];
		byte = ((byte << shift) | (byte >> (8 - shift))) & 255U;
		byte ^= key[i % sizeof(key)];

		if (byte < 32 || byte >= 127)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid SFX filename encoding");

		path[i] = (char)byte;
	}

	path[size] = 0;
	name = strrchr(path, '\\');

	if (name == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing SFX source directory");

	*name++ = 0;
	parent = strrchr(path, '\\');

	if (parent == NULL || strcmp(parent + 1, "disk1") != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected SFX disk1 source directory");

	return mh_archive_path("disk1", name, output, error_out);
}

static enum mh_error_code
read_record(struct sfx_archive *archive, const unsigned char *bytes,
	size_t size, size_t *consumed, struct mh_error *error_out)
{
	struct sfx_member *member;
	unsigned long names, packed;
	size_t start, i;
	enum mh_error_code status;

	if (size < SFX_RECORD_HEADER || archive->count == SFX_MEMBER_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated or oversized SFX member table");

	names = mh_read_u32le(bytes);

	if (names > size - SFX_RECORD_HEADER)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated SFX filename");

	member = &archive->members[archive->count];
	status = decode_name(bytes + SFX_NAME_OFFSET, (size_t)names,
		member->entry.path, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < archive->count; i++)
		if (mh_ascii_equal(
			member->entry.path, archive->members[i].entry.path))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate SFX filename");

	/* The four bytes following the name are retained source metadata. */
	packed = mh_read_u32le(bytes + SFX_PACKED_SIZE_OFFSET + names);
	start = SFX_RECORD_HEADER + (size_t)names;

	if (packed > size - start)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated SFX member data");

	member->data = bytes + start;
	member->packed = (size_t)packed;
	member->entry.size = member->packed;

	if (archive->compressed) {
		if (packed < MH_ZIP_LOCAL_BYTES ||
			mh_read_u32le(member->data) != MH_ZIP_LOCAL_SIGNATURE)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing SFX ZIP member header");

		member->entry.size =
			mh_read_u32le(member->data + MH_ZIP_LOCAL_SIZE);
	}

	*consumed = start + member->packed;
	archive->count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static size_t
member_count(const void *context)
{
	const struct sfx_archive *archive;

	archive = context;

	return archive->count;
}

static const struct mh_archive_entry *
member_entry(const void *context, size_t index)
{
	const struct sfx_archive *archive;

	archive = context;

	return &archive->members[index].entry;
}

static enum mh_error_code
read_member(void *context, size_t index, struct mh_file *output,
	struct mh_error *error_out)
{
	const struct sfx_archive *archive;
	const struct sfx_member *member;
	struct mh_file file;

	archive = context;
	member = &archive->members[index];

	if (archive->compressed)
		return mh_unshield_zip_member(member->data, member->packed,
			strrchr(member->entry.path, '/') + 1, output,
			error_out);

	file.size = member->packed;
	file.data = malloc(file.size == 0 ? 1 : file.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading SFX member");

	memcpy(file.data, member->data, file.size);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_archive(void *context)
{
	free(context);
}

enum mh_error_code
mh_unshield_sfx_open(const void *bytes, size_t size, int compressed,
	struct mh_archive **output, struct mh_error *error_out)
{
	static const struct mh_archive_ops ops = {
		member_count, member_entry, read_member, close_archive};

	struct sfx_archive *archive;
	const unsigned char *scan;
	size_t consumed;
	enum mh_error_code status;

	if (bytes == NULL || output == NULL || size == 0 ||
		(compressed != 0 && compressed != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SFX archive input");

	archive = calloc(1, sizeof(*archive));

	if (archive == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening SFX archive");

	archive->compressed = compressed;
	scan = bytes;
	consumed = 0;
	status = MH_ERROR_NONE;

	while (size != 0 && status == MH_ERROR_NONE) {
		status = read_record(archive, scan, size, &consumed, error_out);

		if (status == MH_ERROR_NONE) {
			scan += consumed;
			size -= consumed;
		}
	}

	if (status == MH_ERROR_NONE)
		status = mh_archive_create(&ops, archive, output, error_out);

	if (status != MH_ERROR_NONE)
		free(archive);

	return status;
}
