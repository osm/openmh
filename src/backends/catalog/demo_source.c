#include <stdio.h>
#include <stdlib.h>

#include "backends/catalog/demo_provider.h"
#include "engine/assets/demo_car.h"
#include "engine/assets/demo_track.h"
#include "engine/storage/asset_path.h"
#include "interfaces/catalog/source_backend.h"
#include "interfaces/storage/resource.h"

enum {
	CATALOG_PATH_BYTES = 1024,
	CATALOG_RECORD_BYTES = 0x20f0 + 4
};

static enum mh_error_code
read_record(const struct mh_resource_source *resources, const char *root,
	const struct mh_demo_asset_layout *layout, unsigned int number,
	struct mh_file *output, struct mh_error *error_out)
{
	char relative[64], path[CATALOG_PATH_BYTES];
	enum mh_error_code status;

	sprintf(relative, layout->catalog_pattern, number);
	status = mh_asset_path_from(
		resources, root, relative, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_resource_source_read_bounded(
		resources, path, CATALOG_RECORD_BYTES, output, error_out);
}

static enum mh_error_code
count_cars(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int *output, struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	enum mh_error_code status;

	(void)context;
	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status == MH_ERROR_NONE)
		*output = layout->cars;

	return status;
}

static enum mh_error_code
count_tracks(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int *output, struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	enum mh_error_code status;

	(void)context;
	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status == MH_ERROR_NONE)
		*output = layout->tracks;

	return status;
}

static enum mh_error_code
load_car(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int number, struct mh_car *output,
	struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (number == 0 || number > layout->cars)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "no such demo car");

	status = read_record(resources, root, layout, layout->tracks + number,
		&file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_car_demo_decode(&file, output, error_out);
	free(file.data);

	return status;
}

static enum mh_error_code
load_track(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int number, struct mh_track *output,
	struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (number == 0 || number > layout->tracks)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "no such demo track");

	status = read_record(resources, root, layout, number, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_track_demo_decode(&file, layout->track, output, error_out);
	free(file.data);

	return status;
}

const struct mh_catalog_source *
mh_catalog_demo_source(void)
{
	static const struct mh_catalog_source source = {
		NULL, count_cars, load_car, count_tracks, load_track};

	return &source;
}
