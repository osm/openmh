#include <limits.h>
#include <string.h>

#include "engine/archive/disc.h"
#include "interfaces/storage/resource.h"

enum {
	READ_SECTORS = 16
};

enum mh_error_code
mh_disc_image_init_track(const struct mh_resource_source *source,
	const char *path, enum mh_disc_format format, unsigned long track_bytes,
	struct mh_disc_image *output, struct mh_error *error_out)
{
	struct mh_disc_image image;
	unsigned long size, stride;
	enum mh_error_code status;

	if (output == NULL ||
		(format != MH_DISC_ISO && format != MH_DISC_MODE1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid disc image arguments");

	status = mh_resource_source_size(source, path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (track_bytes > size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"data track exceeds disc image");

	if (track_bytes != 0)
		size = track_bytes;

	stride = format == MH_DISC_ISO ? MH_DISC_BLOCK_BYTES
				: MH_DISC_SECTOR_BYTES;

	if (size == 0 || size % stride != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"disc image has incomplete sectors");

	image.source = source;
	image.path = path;
	image.format = format;
	image.blocks = size / stride;
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_disc_image_init(const struct mh_resource_source *source, const char *path,
	enum mh_disc_format format, struct mh_disc_image *output,
	struct mh_error *error_out)
{
	return mh_disc_image_init_track(
		source, path, format, 0, output, error_out);
}

static int
valid_sector(const unsigned char *sector)
{
	static const unsigned char sync[] = {
		0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};

	return memcmp(sector, sync, sizeof(sync)) == 0 && sector[15] == 1;
}

static enum mh_error_code
read_raw(const struct mh_disc_image *image, unsigned long offset,
	unsigned char *output, size_t size, struct mh_error *error_out)
{
	unsigned char sectors[READ_SECTORS * MH_DISC_SECTOR_BYTES];
	unsigned long block;
	size_t first, count, i, take, needed;
	enum mh_error_code status;

	block = offset / MH_DISC_BLOCK_BYTES;
	first = (size_t)(offset % MH_DISC_BLOCK_BYTES);

	while (size > 0) {
		count = READ_SECTORS;
		needed = size / MH_DISC_BLOCK_BYTES + 1;

		if (count > needed)
			count = needed;

		if (count > image->blocks - block)
			count = (size_t)(image->blocks - block);

		status = mh_resource_source_read_at(image->source, image->path,
			block * MH_DISC_SECTOR_BYTES, sectors,
			count * MH_DISC_SECTOR_BYTES, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < count && size > 0; i++) {
			if (!valid_sector(sectors + i * MH_DISC_SECTOR_BYTES))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid raw Mode 1 sector header");

			take = MH_DISC_BLOCK_BYTES - first;

			if (take > size)
				take = size;

			memcpy(output,
				sectors + i * MH_DISC_SECTOR_BYTES +
					MH_DISC_DATA_OFFSET + first,
				take);
			output += take;
			size -= take;
			first = 0;
		}

		block += (unsigned long)count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_disc_image_read(const struct mh_disc_image *image, unsigned long offset,
	void *output, size_t size, struct mh_error *error_out)
{
	unsigned long length;

	if (image == NULL || output == NULL ||
		image->blocks > ULONG_MAX / MH_DISC_BLOCK_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid disc image read");

	length = image->blocks * MH_DISC_BLOCK_BYTES;

	if (offset > length || size > length - offset)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"disc read exceeds available data blocks");

	if (image->format == MH_DISC_ISO)
		return mh_resource_source_read_at(image->source, image->path,
			offset, output, size, error_out);

	return read_raw(image, offset, output, size, error_out);
}
