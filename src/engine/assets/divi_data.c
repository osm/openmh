#include <stdlib.h>

#include "engine/assets/divi.h"
#include "engine/assets/divi_data.h"
#include "interfaces/storage/resource.h"

enum mh_error_code
mh_video_divi_read(const struct mh_resource_source *source, const char *path,
	unsigned int index, struct mh_file *output, struct mh_error *error_out)
{
	unsigned char header[MH_VIDEO_DIVI_HEADER_SIZE];
	struct mh_video_divi_index directory;
	struct mh_file data;
	unsigned long length, size;
	enum mh_error_code status;

	if (source == NULL || path == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid movie archive request");

	status = mh_resource_source_size(source, path, &length, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read_at(
			source, path, 0, header, sizeof(header), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_video_divi_index(
			header, sizeof(header), length, &directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (index >= directory.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid movie archive selection");

	size = directory.entries[index].size;

	if ((unsigned long)(size_t)size != size ||
		size > MH_RESOURCE_READ_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"movie payload exceeds resource limit");

	data.data = malloc((size_t)size);
	data.size = (size_t)size;

	if (data.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate movie payload");

	status = mh_resource_source_read_at(source, path,
		directory.entries[index].offset, data.data, data.size,
		error_out);

	if (status != MH_ERROR_NONE) {
		free(data.data);
		return status;
	}

	*output = data;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
