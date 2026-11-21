#include <stdio.h>
#include <stdlib.h>

#include "backends/storage/filesystem_file_read.h"
#include "backends/storage/filesystem_stream.h"
#include "engine/core/error.h"

#define FILE_READ_MAX_BYTES (256UL * 1024UL * 1024UL)

enum mh_error_code
mh_file_read(const char *path, struct mh_file *file, struct mh_error *error_out)
{
	FILE *stream;
	unsigned long length;
	unsigned char *data;
	size_t received;
	int close_status;
	enum mh_error_code status;

	status = mh_filesystem_open_read(path, &stream, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_filesystem_stream_size(stream, &length, error_out);

	if (status != MH_ERROR_NONE) {
		fclose(stream);
		return status;
	}

	if (length > FILE_READ_MAX_BYTES || length != (size_t)length) {
		fclose(stream);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid or oversized file");
	}

	data = (unsigned char *)malloc(length == 0 ? 1 : (size_t)length);

	if (data == NULL) {
		fclose(stream);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading file");
	}

	received = fread(data, 1, (size_t)length, stream);
	close_status = fclose(stream);

	if (received != (size_t)length || close_status != 0) {
		free(data);
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot read complete file");
	}

	file->data = data;
	file->size = (size_t)length;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
