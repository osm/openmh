#include <string.h>

#include "catalog_source.h"

static enum mh_error_code
count_cars(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int *output, struct mh_error *error_out)
{
	struct mh_test_catalog_source *source;

	(void)resources;
	(void)root;
	source = context;
	source->car_counts++;
	*output = source->cars.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
count_tracks(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int *output, struct mh_error *error_out)
{
	struct mh_test_catalog_source *source;

	(void)resources;
	(void)root;
	source = context;
	source->track_counts++;
	*output = source->tracks.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_car(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int number, struct mh_car *output,
	struct mh_error *error_out)
{
	struct mh_test_catalog_source *source;

	(void)resources;
	(void)root;
	source = context;
	source->car_reads++;

	if (number == source->fail_car || number == 0 ||
		number > source->cars.count)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"car absent from decoded fixture");

	*output = source->cars.entries[number - 1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_track(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int number, struct mh_track *output,
	struct mh_error *error_out)
{
	struct mh_test_catalog_source *source;

	(void)resources;
	(void)root;
	source = context;
	source->track_reads++;

	if (number == source->fail_track || number == 0 ||
		number > source->tracks.count)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"track absent from decoded fixture");

	*output = source->tracks.entries[number - 1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_catalog_source_init(struct mh_test_catalog_source *source,
	struct mh_catalog_source *provider)
{
	provider->context = source;

	provider->count_cars = count_cars;
	provider->load_car = load_car;
	provider->count_tracks = count_tracks;
	provider->load_track = load_track;
}

void
mh_test_catalog_source_free(struct mh_test_catalog_source *source)
{
	mh_car_catalog_close(&source->cars);
	mh_track_catalog_close(&source->tracks);
	memset(source, 0, sizeof(*source));
}
