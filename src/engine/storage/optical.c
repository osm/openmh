#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "interfaces/storage/optical_backend.h"
#include "interfaces/storage/resource_backend.h"

struct mh_optical_disc {
	const struct mh_optical_ops	*ops;
	void				*context;
	struct mh_optical_track		tracks[MH_OPTICAL_TRACK_LIMIT];
	size_t				count;
	struct mh_resource_source	resources;

	mh_optical_progress		progress;
	void				*observer;
	unsigned char			cache[MH_OPTICAL_READ_SECTORS * MH_OPTICAL_AUDIO_BYTES];
	size_t				cache_track;
	unsigned long			cache_sector;
	unsigned int			cache_count;
};

static unsigned long
sector_bytes(const struct mh_optical_track *track)
{
	return track->kind == MH_OPTICAL_DATA ? MH_OPTICAL_DATA_BYTES
					: MH_OPTICAL_AUDIO_BYTES;
}

static enum mh_error_code
find_track(const struct mh_optical_disc *disc, const char *path, size_t *index,
	struct mh_error *error_out)
{
	unsigned int number;
	size_t i;

	if (strlen(path) != 7 || memcmp(path, "track", 5) != 0 ||
		path[5] < '0' || path[5] > '9' || path[6] < '0' ||
		path[6] > '9')
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"unknown optical disc track");

	number = (unsigned int)(path[5] - '0') * 10 +
		(unsigned int)(path[6] - '0');

	for (i = 0; i < disc->count; i++) {
		if (disc->tracks[i].number == number) {
			*index = i;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(
		error_out, MH_ERROR_NOT_FOUND, "optical disc track is absent");
}

static enum mh_error_code
track_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	struct mh_optical_disc *disc;
	size_t index;
	enum mh_error_code status;

	disc = context;
	index = 0;
	status = find_track(disc, path, &index, error_out);

	if (status == MH_ERROR_NONE)
		*output = disc->tracks[index].sectors *
			sector_bytes(&disc->tracks[index]);

	return status;
}

static enum mh_error_code
fill_cache(struct mh_optical_disc *disc, size_t index, unsigned long sector,
	size_t needed, struct mh_error *error_out)
{
	const struct mh_optical_track *track;
	unsigned int count;
	enum mh_error_code status;

	if (disc->cache_count != 0 && disc->cache_track == index &&
		sector >= disc->cache_sector &&
		sector - disc->cache_sector < disc->cache_count)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	track = &disc->tracks[index];
	disc->cache_count = 0;

	if (disc->progress != NULL) {
		status = disc->progress(disc->observer, track->number, sector,
			track->sectors, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	count = MH_OPTICAL_READ_SECTORS;

	if (needed < count)
		count = (unsigned int)needed;

	if (track->sectors - sector < count)
		count = (unsigned int)(track->sectors - sector);

	status = disc->ops->read(
		disc->context, index, sector, count, disc->cache, error_out);

	if (status == MH_ERROR_NONE) {
		disc->cache_track = index;
		disc->cache_sector = sector;
		disc->cache_count = count;
	}

	return status;
}

static enum mh_error_code
read_range(void *context, const char *path, unsigned long offset, void *output,
	size_t size, struct mh_error *error_out)
{
	struct mh_optical_disc *disc;
	unsigned char *bytes;
	unsigned long length, stride, sector;
	size_t index, first, take, needed;
	enum mh_error_code status;

	disc = context;
	index = 0;
	status = find_track(disc, path, &index, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	stride = sector_bytes(&disc->tracks[index]);
	length = disc->tracks[index].sectors * stride;

	if (offset > length || size > length - offset)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"optical read exceeds track boundary");

	bytes = output;

	while (size > 0) {
		sector = offset / stride;
		needed = size / stride +
			(size % stride + offset % stride + stride - 1) / stride;
		status = fill_cache(disc, index, sector, needed, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		first = (size_t)(offset - disc->cache_sector * stride);
		take = (size_t)(disc->cache_count * stride) - first;

		if (take > size)
			take = size;

		memcpy(bytes, disc->cache + first, take);
		bytes += take;
		size -= take;
		offset += (unsigned long)take;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_track(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_file file;
	unsigned long size;
	enum mh_error_code status;

	status = track_size(context, path, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (size > limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"optical track exceeds resource read limit");

	file.size = (size_t)size;
	file.data = malloc(file.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading optical track");

	status = read_range(context, path, 0, file.data, file.size, error_out);

	if (status != MH_ERROR_NONE) {
		free(file.data);
		return status;
	}

	*output = file;

	return status;
}

static int
valid_tracks(const struct mh_optical_track *tracks, size_t count)
{
	size_t i;

	if (tracks == NULL || count == 0 || count > MH_OPTICAL_TRACK_LIMIT)
		return 0;

	for (i = 0; i < count; i++) {
		if (tracks[i].number == 0 ||
			tracks[i].number > MH_OPTICAL_TRACK_LIMIT ||
			(i != 0 &&
				tracks[i].number != tracks[i - 1].number + 1) ||
			(tracks[i].kind != MH_OPTICAL_DATA &&
				tracks[i].kind != MH_OPTICAL_AUDIO) ||
			tracks[i].sectors == 0 ||
			tracks[i].sectors >
				ULONG_MAX / sector_bytes(&tracks[i]))
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_optical_disc_create(const struct mh_optical_ops *ops, void *context,
	const struct mh_optical_track *tracks, size_t count,
	struct mh_optical_disc **output, struct mh_error *error_out)
{
	static const struct mh_resource_ops resources = {
		read_track, track_size, NULL, read_range, NULL};

	struct mh_optical_disc *disc;

	if (ops == NULL || ops->read == NULL || ops->close == NULL ||
		output == NULL || !valid_tracks(tracks, count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid optical disc provider or track table");

	disc = calloc(1, sizeof(*disc));

	if (disc == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening optical disc");

	disc->ops = ops;
	disc->context = context;
	disc->count = count;
	memcpy(disc->tracks, tracks, count * sizeof(*tracks));
	mh_resource_source_init(&disc->resources, &resources, disc, NULL);
	*output = disc;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_optical_disc_close(struct mh_optical_disc *disc)
{
	if (disc == NULL)
		return;

	disc->ops->close(disc->context);
	free(disc);
}

size_t
mh_optical_disc_count(const struct mh_optical_disc *disc)
{
	return disc == NULL ? 0 : disc->count;
}

const struct mh_optical_track *
mh_optical_disc_track(const struct mh_optical_disc *disc, size_t index)
{
	return disc == NULL || index >= disc->count ? NULL
						: &disc->tracks[index];
}

const struct mh_resource_source *
mh_optical_disc_resources(struct mh_optical_disc *disc)
{
	return disc == NULL ? NULL : &disc->resources;
}

void
mh_optical_disc_set_progress(struct mh_optical_disc *disc,
	mh_optical_progress progress, void *context)
{
	if (disc == NULL)
		return;

	disc->progress = progress;
	disc->observer = context;
}
