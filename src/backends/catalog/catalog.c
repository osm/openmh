#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/catalog/internal.h"
#include "engine/assets/path_catalog.h"
#include "engine/assets/path_directories.h"
#include "engine/core/text.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum {
	CATALOG_EXTENSION_LENGTH = 4,
	CATALOG_NAME_SIZE = 32,
	CATALOG_PATH_SIZE = 1024
};

struct catalog_scan {
	const char	*prefix, *extension;
	unsigned int	limit, count;
	unsigned char	present[MH_CAR_CATALOG_LIMIT];
	struct mh_error	error;
};

struct named_tracks {
	char		names[MH_TRACK_CATALOG_LIMIT][MH_TRACK_PATH_SIZE];
	unsigned int	count;
	struct mh_error	error;
};

static enum mh_directory_action
collect_entry(void *context, const char *name)
{
	struct catalog_scan *scan;
	const char *digits;
	char expected[CATALOG_NAME_SIZE];
	size_t prefix_length, length;
	unsigned int number;

	scan = (struct catalog_scan *)context;
	prefix_length = strlen(scan->prefix);
	length = strlen(name);

	if (length <= prefix_length + CATALOG_EXTENSION_LENGTH ||
		!mh_ascii_equal_n(name, scan->prefix, prefix_length) ||
		!mh_ascii_equal(name + length - CATALOG_EXTENSION_LENGTH,
			scan->extension))
		return MH_DIRECTORY_CONTINUE;

	number = 0;
	digits = name + prefix_length;

	while (digits < name + length - CATALOG_EXTENSION_LENGTH) {
		if (*digits < '0' || *digits > '9')
			return MH_DIRECTORY_CONTINUE;

		/*
		 * Finish recognizing the name before rejecting a stock number.
		 * Named tracks may start with arbitrarily many digits.
		 */
		if (number <= scan->limit)
			number = number * 10 + (unsigned int)(*digits - '0');

		digits++;
	}

	if (number == 0 || number > scan->limit) {
		mh_error_set(&scan->error, MH_ERROR_RANGE,
			"content number outside catalog capacity");
		return MH_DIRECTORY_FAIL;
	}

	if (strcmp(scan->prefix, "car") == 0)
		sprintf(expected, MH_ASSET_CAR_CONFIG_NAME_FORMAT, number);
	else
		sprintf(expected, MH_ASSET_TRACK_CONFIG_NAME_FORMAT, number);

	if (!mh_ascii_equal(name, expected) || scan->present[number - 1]) {
		mh_error_set(&scan->error, MH_ERROR_INVALID,
			"noncanonical or duplicate content number");
		return MH_DIRECTORY_FAIL;
	}

	scan->present[number - 1] = 1;
	scan->count++;

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
catalog_count(const struct mh_resource_source *source, const char *root,
	const char *prefix, const char *extension, unsigned int limit,
	unsigned int *output, struct mh_error *error_out)
{
	struct catalog_scan scan;
	char directory[CATALOG_PATH_SIZE];
	unsigned int i;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing catalog count output");

	memset(&scan, 0, sizeof(scan));
	scan.prefix = prefix;
	scan.extension = extension;
	scan.limit = limit;
	status = mh_asset_path_from(source, root, MH_ASSET_GAME_DIR, directory,
		sizeof(directory), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_list(
		source, directory, collect_entry, &scan, error_out);

	if (scan.error.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &scan.error);

	if (status != MH_ERROR_NONE)
		return status;

	if (scan.count == 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "empty content catalog");

	for (i = 0; i < scan.count; i++)
		if (!scan.present[i])
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"gap in numbered content catalog");

	*output = scan.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_car_catalog_car_count(const struct mh_resource_source *source,
	const char *root, unsigned int *output, struct mh_error *error_out)
{
	return catalog_count(source, root, "car", ".car", MH_CAR_CATALOG_LIMIT,
		output, error_out);
}

static enum mh_directory_action
collect_named_track(void *context, const char *name)
{
	struct named_tracks *tracks;
	const char *digits;
	size_t length;

	tracks = context;
	length = strlen(name);

	if (length <= sizeof(MH_ASSET_TRACK_EXTENSION) - 1 ||
		!mh_ascii_equal(
			name + length - (sizeof(MH_ASSET_TRACK_EXTENSION) - 1),
			MH_ASSET_TRACK_EXTENSION))
		return MH_DIRECTORY_CONTINUE;

	/*
	 * Numbered definitions retain their original identities and validation.
	 */
	if (mh_ascii_equal_n(name, "track", 5)) {
		digits = name + 5;

		while (*digits >= '0' && *digits <= '9')
			digits++;

		if (digits != name + 5 &&
			digits ==
				name + length -
					(sizeof(MH_ASSET_TRACK_EXTENSION) - 1))
			return MH_DIRECTORY_CONTINUE;
	}

	if (tracks->count == MH_TRACK_CATALOG_LIMIT ||
		length >= MH_TRACK_PATH_SIZE - 5) {
		mh_error_set(&tracks->error, MH_ERROR_RANGE,
			"too many or oversized add-on track definitions");
		return MH_DIRECTORY_FAIL;
	}

	strcpy(tracks->names[tracks->count++], name);

	return MH_DIRECTORY_CONTINUE;
}

static int
compare_track_names(const void *a, const void *b)
{
	return mh_ascii_compare(a, b);
}

static enum mh_error_code
named_track_catalog(const struct mh_resource_source *source, const char *root,
	struct named_tracks *tracks, unsigned int *numbered,
	struct mh_error *error_out)
{
	char path[CATALOG_PATH_SIZE];
	unsigned int i;
	enum mh_error_code status;

	memset(tracks, 0, sizeof(*tracks));
	*numbered = 0;
	status = catalog_count(source, root, "track", MH_ASSET_TRACK_EXTENSION,
		MH_TRACK_CATALOG_LIMIT, numbered, error_out);

	if (status != MH_ERROR_NONE && status != MH_ERROR_NOT_FOUND)
		return status;

	status = mh_asset_path_from(
		source, root, MH_ASSET_GAME_DIR, path, sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_list(
			source, path, collect_named_track, tracks, error_out);

	if (tracks->error.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &tracks->error);

	if (status != MH_ERROR_NONE)
		return status;

	if (*numbered + tracks->count > MH_TRACK_CATALOG_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"track catalog exceeds capacity");

	qsort(tracks->names, tracks->count, sizeof(tracks->names[0]),
		compare_track_names);

	for (i = 1; i < tracks->count; i++)
		if (mh_ascii_equal(tracks->names[i - 1], tracks->names[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate add-on track definition");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_track_definition_path(const struct mh_resource_source *source,
	const char *root, unsigned int number, char output[MH_TRACK_PATH_SIZE],
	int *custom, struct mh_error *error_out)
{
	struct named_tracks tracks;
	unsigned int numbered;
	enum mh_error_code status;

	status = named_track_catalog(
		source, root, &tracks, &numbered, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (number == 0 || number > numbered + tracks.count)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"track definition not found");

	*custom = number > numbered;

	if (*custom) {
		strcpy(output, MH_ASSET_GAME_PREFIX);
		strcat(output, tracks.names[number - numbered - 1]);
	} else
		sprintf(output, MH_ASSET_TRACK_CONFIG_FORMAT, number);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_track_catalog_trk_count(const struct mh_resource_source *source,
	const char *root, unsigned int *output, struct mh_error *error_out)
{
	struct named_tracks tracks;
	unsigned int numbered;
	enum mh_error_code status;

	status = named_track_catalog(
		source, root, &tracks, &numbered, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (numbered + tracks.count == 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "empty track catalog");

	*output = numbered + tracks.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
