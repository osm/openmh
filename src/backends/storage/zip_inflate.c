#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "backends/storage/zip_internal.h"

static enum mh_error_code
expand(const struct mh_zip_resources *zip, const struct mh_zip_entry *entry,
	z_stream *stream, struct mh_error *error_out)
{
	unsigned char input[65536];
	unsigned long offset;
	size_t chunk;
	int result;
	enum mh_error_code status;

	offset = 0;

	for (;;) {
		if (stream->avail_in == 0 && offset < entry->packed) {
			chunk = entry->packed - offset > sizeof(input)
				? sizeof(input)
				: (size_t)(entry->packed - offset);

			status = mh_resource_source_read_at(zip->source,
				zip->path, entry->offset + offset, input, chunk,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			stream->next_in = input;
			stream->avail_in = (uInt)chunk;
			offset += chunk;
		}

		result = inflate(stream, Z_NO_FLUSH);

		if (result == Z_STREAM_END)
			break;

		if (result == Z_MEM_ERROR)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory expanding ZIP entry");

		if (result != Z_OK || stream->avail_out == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid deflated ZIP entry");
	}

	if (stream->total_in != entry->packed ||
		stream->total_out != entry->size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ZIP entry compressed or expanded size mismatch");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_zip_inflate(const struct mh_zip_resources *zip,
	const struct mh_zip_entry *entry, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_file file;

	z_stream stream;
	int result;
	enum mh_error_code status;

	/*
	 * An extra byte detects expansion beyond the declared size, including
	 * an invalid nonempty stream advertised as an empty file.
	 */
	file.size = (size_t)entry->size;

	if (entry->size > MH_RESOURCE_READ_LIMIT || entry->size >= UINT_MAX ||
		file.size != entry->size || file.size == (size_t)-1)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"expanded ZIP entry exceeds resource limit");

	file.data = malloc(file.size + 1);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory expanding ZIP entry");

	memset(&stream, 0, sizeof(stream));
	result = inflateInit2(&stream, -MAX_WBITS);

	if (result != Z_OK) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot initialize ZIP decompression");
	}

	stream.next_out = file.data;
	stream.avail_out = (uInt)file.size + 1;
	status = expand(zip, entry, &stream, error_out);
	inflateEnd(&stream);

	if (status == MH_ERROR_NONE &&
		crc32(crc32(0, Z_NULL, 0), file.data, (uInt)file.size) !=
			entry->crc)
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"package resource CRC mismatch");

	if (status != MH_ERROR_NONE) {
		free(file.data);
		return status;
	}

	*output = file;

	return status;
}
