#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "engine/archive/zip_deflate.h"
#include "engine/archive/zip_layout.h"
#include "engine/archive/zip_write.h"
#include "engine/core/binary.h"
#include "engine/core/text.h"

#define ZIP_LIMIT MH_U32_MAX
#define ZIP_ENTRIES (MH_U16_MAX - 1U)
#define COPY_BUFFER_BYTES 65536

struct zip_member {
	char		*name;
	unsigned long	offset, size, packed, crc;
	unsigned int	method;
	int		directory;
};

struct mh_zip_writer {
	struct mh_data_stream	*stream;
	struct zip_member	*members;
	size_t			count, capacity;
	int			finished, failed;
};

static enum mh_error_code
append(struct mh_zip_writer *writer, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	size_t position;

	position = mh_data_stream_size(writer->stream);

	if (position >= ZIP_LIMIT || size >= ZIP_LIMIT - position)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"package exceeds classic ZIP size limit");

	return mh_data_stream_append(writer->stream, bytes, size, error_out);
}

static void
local_header(const struct zip_member *member,
	unsigned char header[MH_ZIP_LOCAL_BYTES])
{
	memset(header, 0, MH_ZIP_LOCAL_BYTES);

	mh_write_u32le(header, MH_ZIP_LOCAL_SIGNATURE);
	mh_write_u16le(header + MH_ZIP_LOCAL_VERSION, MH_ZIP_VERSION_20);
	mh_write_u16le(header + MH_ZIP_LOCAL_METHOD, member->method);

	/* Fixed 1980-01-01, independent of host time. */
	mh_write_u16le(header + MH_ZIP_LOCAL_DATE, MH_ZIP_FIXED_DATE);
	mh_write_u32le(header + MH_ZIP_LOCAL_CRC, member->crc);
	mh_write_u32le(header + MH_ZIP_LOCAL_PACKED_SIZE, member->packed);
	mh_write_u32le(header + MH_ZIP_LOCAL_SIZE, member->size);
	mh_write_u16le(header + MH_ZIP_LOCAL_NAME_SIZE,
		(unsigned long)strlen(member->name));
}

static enum mh_error_code
write_stored(struct mh_zip_writer *writer, struct zip_member *member,
	const struct mh_resource_source *source, const char *path,
	struct mh_error *error_out)
{
	unsigned char bytes[COPY_BUFFER_BYTES];
	unsigned long offset;
	size_t chunk;
	enum mh_error_code status;

	member->crc = crc32(0, Z_NULL, 0);

	for (offset = 0; offset < member->size; offset += chunk) {
		chunk = member->size - offset > sizeof(bytes)
			? sizeof(bytes)
			: (size_t)(member->size - offset);

		status = mh_resource_source_read_at(
			source, path, offset, bytes, chunk, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		member->crc = crc32(member->crc, bytes, (uInt)chunk);
		status = append(writer, bytes, chunk, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_member(struct zip_member *member,
	const struct mh_resource_source *source, const char *path,
	enum mh_zip_compression compression, struct mh_file *compressed,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (member->directory)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status =
		mh_resource_source_size(source, path, &member->size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (member->size >= ZIP_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"package member exceeds ZIP size limit");

	member->packed = member->size;

	if (compression == MH_ZIP_STORE)
		return MH_ERROR_NONE;

	status = mh_zip_deflate(source, path, member->size, compressed,
		&member->crc, error_out);

	if (status == MH_ERROR_NONE && compressed->data != NULL) {
		member->packed = (unsigned long)compressed->size;
		member->method = MH_ZIP_DEFLATED;
	}

	return status;
}

static enum mh_error_code
write_member(struct mh_zip_writer *writer, struct zip_member *member,
	const struct mh_resource_source *source, const char *path,
	enum mh_zip_compression compression, struct mh_error *error_out)
{
	unsigned char header[MH_ZIP_LOCAL_BYTES];
	struct mh_file compressed;
	enum mh_error_code status;

	compressed.data = NULL;
	compressed.size = 0;
	member->offset = (unsigned long)mh_data_stream_size(writer->stream);
	status = prepare_member(
		member, source, path, compression, &compressed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	local_header(member, header);
	status = append(writer, header, sizeof(header), error_out);

	if (status == MH_ERROR_NONE)
		status = append(
			writer, member->name, strlen(member->name), error_out);

	if (status == MH_ERROR_NONE) {
		if (member->method == MH_ZIP_DEFLATED)
			status = append(writer, compressed.data,
				compressed.size, error_out);
		else
			status = write_stored(
				writer, member, source, path, error_out);
	}

	free(compressed.data);

	if (status != MH_ERROR_NONE)
		return status;

	local_header(member, header);

	return mh_data_stream_write_at(writer->stream, member->offset, header,
		sizeof(header), error_out);
}

static int
valid_name(const char *name)
{
	const char *start, *scan;
	size_t size;

	if (name == NULL || strlen(name) >= 1023)
		return 0;

	start = name;

	for (scan = name;; scan++) {
		if (*scan == '\\' || *scan == ':' ||
			(*scan != 0 && (unsigned char)*scan < 32))
			return 0;

		if (*scan != '/' && *scan != 0)
			continue;

		size = (size_t)(scan - start);

		if (size == 0 || size > 255 || (size == 1 && start[0] == '.') ||
			(size == 2 && start[0] == '.' && start[1] == '.'))
			return 0;

		if (*scan == 0)
			return 1;

		start = scan + 1;
	}
}

enum mh_error_code
mh_zip_writer_open(struct mh_data_stream *stream, struct mh_zip_writer **output,
	struct mh_error *error_out)
{
	struct mh_zip_writer *writer;

	if (stream == NULL || output == NULL ||
		mh_data_stream_size(stream) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ZIP writer requires an empty private stream");

	writer = calloc(1, sizeof(*writer));

	if (writer == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating ZIP writer");

	writer->stream = stream;
	*output = writer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_zip_writer_add(struct mh_zip_writer *writer,
	const struct mh_resource_source *source, const char *path,
	const char *name, int directory, enum mh_zip_compression compression,
	struct mh_error *error_out)
{
	struct zip_member *member, *next;
	size_t length, capacity, i;
	enum mh_error_code status;

	if (writer == NULL || writer->failed || writer->finished ||
		!valid_name(name) || (directory != 0 && directory != 1) ||
		(compression != MH_ZIP_STORE &&
			compression != MH_ZIP_COMPRESS) ||
		(!directory && (source == NULL || path == NULL)))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid ZIP member");

	if (writer->count == ZIP_ENTRIES)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many ZIP members");

	length = strlen(name);

	for (i = 0; i < writer->count; i++) {
		size_t previous;

		previous = strlen(writer->members[i].name) -
			writer->members[i].directory;

		if (previous == length &&
			mh_ascii_equal_n(writer->members[i].name, name, length))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate ZIP member");
	}

	if (writer->count == writer->capacity) {
		capacity = writer->capacity == 0 ? 64 : writer->capacity * 2;
		next = realloc(writer->members, capacity * sizeof(*next));

		if (next == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory indexing ZIP output");

		writer->members = next;
		writer->capacity = capacity;
	}

	member = &writer->members[writer->count];
	memset(member, 0, sizeof(*member));
	member->name = malloc(length + 2);

	if (member->name == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory naming ZIP member");

	memcpy(member->name, name, length);

	if (directory)
		member->name[length++] = '/';

	member->name[length] = 0;
	member->directory = directory;
	writer->count++;
	status = write_member(
		writer, member, source, path, compression, error_out);
	writer->failed = status != MH_ERROR_NONE;

	return status;
}

static enum mh_error_code
write_central(struct mh_zip_writer *writer, const struct zip_member *member,
	struct mh_error *error_out)
{
	unsigned char header[MH_ZIP_CENTRAL_BYTES], local[MH_ZIP_LOCAL_BYTES];
	enum mh_error_code status;

	memset(header, 0, sizeof(header));
	local_header(member, local);
	mh_write_u32le(header, MH_ZIP_CENTRAL_SIGNATURE);
	mh_write_u16le(
		header + MH_ZIP_CENTRAL_CREATOR_VERSION, MH_ZIP_VERSION_20);
	memcpy(header + MH_ZIP_CENTRAL_VERSION, local + MH_ZIP_LOCAL_VERSION,
		MH_ZIP_LOCAL_BYTES - MH_ZIP_LOCAL_VERSION);

	if (member->directory)
		mh_write_u32le(header + MH_ZIP_CENTRAL_EXTERNAL_ATTRIBUTES,
			MH_ZIP_DOS_DIRECTORY);

	mh_write_u32le(header + MH_ZIP_CENTRAL_LOCAL_OFFSET, member->offset);
	status = append(writer, header, sizeof(header), error_out);

	if (status == MH_ERROR_NONE)
		status = append(
			writer, member->name, strlen(member->name), error_out);

	return status;
}

enum mh_error_code
mh_zip_writer_finish(struct mh_zip_writer *writer, struct mh_error *error_out)
{
	unsigned char end[MH_ZIP_END_BYTES];
	unsigned long central;
	size_t i;
	enum mh_error_code status;

	if (writer == NULL || writer->failed || writer->finished)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "ZIP writer is not open");

	central = (unsigned long)mh_data_stream_size(writer->stream);
	status = MH_ERROR_NONE;

	for (i = 0; status == MH_ERROR_NONE && i < writer->count; i++)
		status = write_central(writer, &writer->members[i], error_out);

	if (status == MH_ERROR_NONE) {
		memset(end, 0, sizeof(end));
		mh_write_u32le(end, MH_ZIP_END_SIGNATURE);
		mh_write_u16le(end + MH_ZIP_END_DISK_ENTRIES,
			(unsigned long)writer->count);
		mh_write_u16le(
			end + MH_ZIP_END_ENTRIES, (unsigned long)writer->count);
		mh_write_u32le(end + MH_ZIP_END_CENTRAL_SIZE,
			(unsigned long)mh_data_stream_size(writer->stream) -
				central);
		mh_write_u32le(end + MH_ZIP_END_CENTRAL_OFFSET, central);
		status = append(writer, end, sizeof(end), error_out);
	}

	writer->finished = 1;
	writer->failed = status != MH_ERROR_NONE;

	return status;
}

void
mh_zip_writer_close(struct mh_zip_writer *writer)
{
	size_t i;

	if (writer == NULL)
		return;

	for (i = 0; i < writer->count; i++)
		free(writer->members[i].name);

	free(writer->members);
	free(writer);
}
