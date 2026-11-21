#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "engine/archive/zip_deflate.h"

/* Keep packaging memory bounded independently of the total archive size. */
#define ZIP_COMPRESSION_LIMIT (32UL * 1024UL * 1024UL)

static enum mh_error_code
compress_entry(const struct mh_resource_source *source, const char *path,
	unsigned long size, z_stream *stream, unsigned long *checksum,
	struct mh_error *error_out)
{
	unsigned char bytes[65536];
	unsigned long offset;
	size_t chunk;
	int result, flush;
	enum mh_error_code status;

	offset = 0;
	*checksum = crc32(0, Z_NULL, 0);

	for (;;) {
		if (stream->avail_in == 0 && offset < size) {
			chunk = size - offset > sizeof(bytes)
				? sizeof(bytes)
				: (size_t)(size - offset);

			status = mh_resource_source_read_at(
				source, path, offset, bytes, chunk, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			*checksum = crc32(*checksum, bytes, (uInt)chunk);
			stream->next_in = bytes;
			stream->avail_in = (uInt)chunk;
			offset += chunk;
		}

		flush = offset == size ? Z_FINISH : Z_NO_FLUSH;
		result = deflate(stream, flush);

		if (result == Z_STREAM_END || stream->avail_out == 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (result != Z_OK)
			return mh_error_set(error_out, MH_ERROR_BACKEND,
				"cannot compress ZIP entry");
	}
}

enum mh_error_code
mh_zip_deflate(const struct mh_resource_source *source, const char *path,
	unsigned long size, struct mh_file *output, unsigned long *checksum,
	struct mh_error *error_out)
{
	struct mh_file file;

	z_stream stream;
	unsigned long crc;
	int result;
	enum mh_error_code status;

	file.data = NULL;
	file.size = 0;

	if (size == 0 || size > ZIP_COMPRESSION_LIMIT) {
		*output = file;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	file.data = malloc((size_t)size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory compressing ZIP entry");

	memset(&stream, 0, sizeof(stream));
	result = deflateInit2(
		&stream, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);

	if (result != Z_OK) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot initialize ZIP compression");
	}

	stream.next_out = file.data;
	stream.avail_out = (uInt)size;
	status = compress_entry(source, path, size, &stream, &crc, error_out);
	file.size = (size_t)stream.total_out;
	deflateEnd(&stream);

	if (status != MH_ERROR_NONE || file.size >= size) {
		free(file.data);
		file.data = NULL;
		file.size = 0;
	}

	if (status != MH_ERROR_NONE)
		return status;

	*output = file;

	if (file.data != NULL)
		*checksum = crc;

	return status;
}
