#include "backends/catalog/demo_provider.h"
#include "backends/catalog/internal.h"
#include "backends/catalog/provider.h"
#include "interfaces/catalog/source_backend.h"

static enum mh_error_code
count_cars(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int *output, struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;
	status = mh_car_catalog_car_count(resources, root, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return mh_car_catalog_count_from(
		mh_catalog_demo_source(), resources, root, output, error_out);
}

static enum mh_error_code
load_car(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int number, struct mh_car *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;
	status = mh_car_config_car_load(
		resources, root, number, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return mh_car_config_load_from(mh_catalog_demo_source(), resources,
		root, number, output, error_out);
}

static enum mh_error_code
count_tracks(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int *output, struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;
	status = mh_track_catalog_trk_count(resources, root, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return mh_track_catalog_count_from(
		mh_catalog_demo_source(), resources, root, output, error_out);
}

static enum mh_error_code
load_track(void *context, const struct mh_resource_source *resources,
	const char *root, unsigned int number, struct mh_track *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;
	status = mh_track_config_trk_load(
		resources, root, number, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return mh_track_config_load_from(mh_catalog_demo_source(), resources,
		root, number, output, error_out);
}

const struct mh_catalog_source *
mh_catalog_fallback_source(void)
{
	static const struct mh_catalog_source source = {
		NULL, count_cars, load_car, count_tracks, load_track};

	return &source;
}
