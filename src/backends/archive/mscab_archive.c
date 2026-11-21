#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "backends/archive/mscab_provider.h"
#include "engine/archive/path.h"
#include "engine/core/binary.h"
#include "engine/core/text.h"
#include "interfaces/archive/archive_backend.h"

#define CAB_SIGNATURE "MSCF"
#define CAB_BLOCK_SIGNATURE "CK"

enum {
	CAB_LENGTH = 8,
	CAB_FILE_TABLE = 16,
	CAB_VERSION_MINOR = 24,
	CAB_VERSION_MAJOR = 25,
	CAB_FOLDER_COUNT = 26,
	CAB_FILE_COUNT = 28,
	CAB_FLAGS = 30,
	CAB_NUMBER = 34,
	CAB_FOLDER_BLOCK_COUNT = 4,
	CAB_FOLDER_COMPRESSION = 6,
	CAB_FILE_OFFSET = 4,
	CAB_FILE_FOLDER = 8,
	CAB_BLOCK_PACKED = 4,
	CAB_BLOCK_EXPANDED = 6,
	CAB_BLOCK_SIGNATURE_BYTES = sizeof(CAB_BLOCK_SIGNATURE) - 1,
	CAB_HEADER_SIZE = 36,
	CAB_FOLDER_SIZE = 8,
	CAB_FILE_SIZE = 16,
	CAB_BLOCK_SIZE = 8,
	CAB_WINDOW_SIZE = 32768,
	CAB_MSZIP = 1
};

#define CAB_EXPANDED_LIMIT ((size_t)128 * 1024 * 1024)

struct cab_member {
	struct mh_archive_entry	entry;
	size_t			offset;
};

struct cabinet {
	struct cab_member	*members;
	size_t			count, expanded;
	unsigned char		*data;
};

static void
close_cabinet(void *context)
{
	struct cabinet *cabinet;

	cabinet = context;
	free(cabinet->members);
	free(cabinet->data);
	free(cabinet);
}

static size_t
member_count(const void *context)
{
	const struct cabinet *cabinet;

	cabinet = context;

	return cabinet->count;
}

static const struct mh_archive_entry *
member_entry(const void *context, size_t index)
{
	const struct cabinet *cabinet;

	cabinet = context;

	return &cabinet->members[index].entry;
}

static enum mh_error_code
read_member(void *context, size_t index, struct mh_file *output,
	struct mh_error *error_out)
{
	struct cabinet *cabinet;
	struct mh_file file;

	cabinet = context;
	file.size = cabinet->members[index].entry.size;
	file.data = malloc(file.size == 0 ? 1 : file.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading MS cabinet member");

	memcpy(file.data, cabinet->data + cabinet->members[index].offset,
		file.size);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
member_path(const unsigned char *name, size_t length, char *output,
	struct mh_error *error_out)
{
	char path[MH_ARCHIVE_PATH_SIZE], *slash;
	size_t i;

	/* This SFX uses a leading backslash for cabinet-root relative names. */
	if (length != 0 && name[0] == '\\') {
		name++;
		length--;
	}

	if (length == 0 || length >= sizeof(path))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid MS cabinet filename length");

	memcpy(path, name, length);
	path[length] = 0;

	for (i = 0; i < length; i++) {
		if (path[i] == '\\')
			path[i] = '/';
	}

	slash = strrchr(path, '/');

	if (slash == NULL)
		return mh_archive_path("", path, output, error_out);

	*slash = 0;

	return mh_archive_path(path, slash + 1, output, error_out);
}

static enum mh_error_code
read_directory(struct cabinet *cabinet, const unsigned char *bytes,
	size_t table, size_t end, struct mh_error *error_out)
{
	struct cab_member *member;
	const unsigned char *zero;
	unsigned long length, offset;
	size_t i, j, names, member_end;
	enum mh_error_code status;

	cabinet->members = calloc(cabinet->count, sizeof(*cabinet->members));

	if (cabinet->members == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading MS cabinet directory");

	for (i = 0; i < cabinet->count; i++) {
		if (table > end || end - table < CAB_FILE_SIZE + 1 ||
			mh_read_u16le(bytes + table + CAB_FILE_FOLDER) != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated or split MS cabinet file");

		length = mh_read_u32le(bytes + table);
		offset = mh_read_u32le(bytes + table + CAB_FILE_OFFSET);

		if (offset > CAB_EXPANDED_LIMIT ||
			length > CAB_EXPANDED_LIMIT - offset)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"MS cabinet expanded data exceeds limit");

		table += CAB_FILE_SIZE;
		zero = memchr(bytes + table, 0, end - table);

		if (zero == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated MS cabinet filename");

		names = (size_t)(zero - bytes - table);
		member = &cabinet->members[i];
		status = member_path(
			bytes + table, names, member->entry.path, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (j = 0; j < i; j++) {
			if (mh_ascii_equal(member->entry.path,
				cabinet->members[j].entry.path))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate MS cabinet filename");
		}

		member->offset = (size_t)offset;
		member->entry.size = (size_t)length;
		member_end = member->offset + member->entry.size;

		if (member_end > cabinet->expanded)
			cabinet->expanded = member_end;

		table += names + 1;
	}

	if (table != end)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unexpected MS cabinet directory padding");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
inflate_block(const unsigned char *bytes, size_t packed, unsigned char *data,
	size_t offset, size_t expanded, struct mh_error *error_out)
{
	z_stream stream;
	size_t history;
	int result, valid;

	if (packed < CAB_BLOCK_SIGNATURE_BYTES ||
		memcmp(bytes, CAB_BLOCK_SIGNATURE, CAB_BLOCK_SIGNATURE_BYTES) !=
			0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing MSZIP block signature");

	memset(&stream, 0, sizeof(stream));
	result = inflateInit2(&stream, -MAX_WBITS);

	if (result != Z_OK)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot initialize MSZIP decoder");

	history = offset < CAB_WINDOW_SIZE ? offset : CAB_WINDOW_SIZE;

	if (history != 0)
		result = inflateSetDictionary(
			&stream, data + offset - history, (uInt)history);

	if (result == Z_OK) {
		stream.next_in = (Bytef *)bytes + CAB_BLOCK_SIGNATURE_BYTES;
		stream.avail_in = (uInt)(packed - CAB_BLOCK_SIGNATURE_BYTES);
		stream.next_out = data + offset;
		stream.avail_out = (uInt)expanded;
		result = inflate(&stream, Z_FINISH);
	}

	valid = result == Z_STREAM_END &&
		stream.total_in == packed - CAB_BLOCK_SIGNATURE_BYTES &&
		stream.total_out == expanded;
	inflateEnd(&stream);

	if (!valid)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MSZIP block decode or length mismatch");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* CAB checksums XOR little-endian words, with the tail in high-to-low order. */
static unsigned long
block_checksum(const unsigned char *bytes, size_t size)
{
	unsigned long sum, tail;

	sum = 0;

	while (size >= 4) {
		sum ^= mh_read_u32le(bytes);
		bytes += 4;
		size -= 4;
	}

	tail = 0;

	while (size != 0) {
		tail = (tail << 8) | *bytes++;
		size--;
	}

	return sum ^ tail;
}

static enum mh_error_code
read_blocks(struct cabinet *cabinet, const unsigned char *bytes, size_t size,
	size_t position, unsigned int blocks, struct mh_error *error_out)
{
	unsigned long checksum;
	size_t offset, packed, expanded;
	unsigned int i;
	enum mh_error_code status;

	cabinet->data = malloc(cabinet->expanded == 0 ? 1 : cabinet->expanded);

	if (cabinet->data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory expanding MS cabinet");

	offset = 0;

	for (i = 0; i < blocks; i++) {
		if (position > size || size - position < CAB_BLOCK_SIZE)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated MS cabinet block header");

		checksum = mh_read_u32le(bytes + position);
		packed = mh_read_u16le(bytes + position + CAB_BLOCK_PACKED);
		expanded = mh_read_u16le(bytes + position + CAB_BLOCK_EXPANDED);
		position += CAB_BLOCK_SIZE;

		if (packed > size - position || expanded == 0 ||
			expanded > CAB_WINDOW_SIZE ||
			expanded > cabinet->expanded - offset)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid MS cabinet block length");

		if (checksum != 0 &&
			checksum !=
				(block_checksum(bytes + position, packed) ^
					mh_read_u32le(bytes + position -
						CAB_BLOCK_SIZE +
						CAB_BLOCK_PACKED)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"MS cabinet block checksum mismatch");

		status = inflate_block(bytes + position, packed, cabinet->data,
			offset, expanded, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		offset += expanded;
		position += packed;
	}

	if (position != size || offset != cabinet->expanded)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"MS cabinet data length mismatch");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mscab_open(const void *data, size_t size, struct mh_archive **output,
	struct mh_error *error_out)
{
	static const struct mh_archive_ops ops = {
		member_count, member_entry, read_member, close_cabinet};

	const unsigned char *bytes;
	struct cabinet *cabinet;
	unsigned long start, table;
	unsigned int count, blocks;
	enum mh_error_code status;

	bytes = data;

	if (bytes == NULL || output == NULL ||
		size < CAB_HEADER_SIZE + CAB_FOLDER_SIZE ||
		memcmp(bytes, CAB_SIGNATURE, sizeof(CAB_SIGNATURE) - 1) != 0 ||
		mh_read_u32le(bytes + CAB_LENGTH) != size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid MS cabinet header");

	/* Version 1.3, no reserve areas or continuation cabinets. */
	if (bytes[CAB_VERSION_MINOR] != 3 || bytes[CAB_VERSION_MAJOR] != 1 ||
		mh_read_u16le(bytes + CAB_FOLDER_COUNT) != 1 ||
		mh_read_u16le(bytes + CAB_FLAGS) != 0 ||
		mh_read_u16le(bytes + CAB_NUMBER) != 0 ||
		mh_read_u16le(bytes + CAB_HEADER_SIZE +
			CAB_FOLDER_COMPRESSION) != CAB_MSZIP)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"expected a single-folder MSZIP cabinet");

	count = (unsigned int)mh_read_u16le(bytes + CAB_FILE_COUNT);
	table = mh_read_u32le(bytes + CAB_FILE_TABLE);
	start = mh_read_u32le(bytes + CAB_HEADER_SIZE);
	blocks = (unsigned int)mh_read_u16le(
		bytes + CAB_HEADER_SIZE + CAB_FOLDER_BLOCK_COUNT);

	if (count == 0 || count > MH_ARCHIVE_MEMBER_LIMIT || blocks == 0 ||
		table != CAB_HEADER_SIZE + CAB_FOLDER_SIZE || start < table ||
		start > size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid MS cabinet directory bounds");

	cabinet = calloc(1, sizeof(*cabinet));

	if (cabinet == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening MS cabinet");

	cabinet->count = count;
	status = read_directory(
		cabinet, bytes, (size_t)table, (size_t)start, error_out);

	if (status == MH_ERROR_NONE)
		status = read_blocks(
			cabinet, bytes, size, (size_t)start, blocks, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_archive_create(&ops, cabinet, output, error_out);

	if (status != MH_ERROR_NONE)
		close_cabinet(cabinet);

	return status;
}
