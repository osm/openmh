#include <stdlib.h>
#include <string.h>

#include "engine/catalog/load.h"
#include "engine/core/text.h"
#include "interfaces/catalog/source_backend.h"

enum mh_error_code
mh_car_catalog_count_from(const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	unsigned int *output, struct mh_error *error_out)
{
	unsigned int count;
	enum mh_error_code status;

	if (source == NULL || source->count_cars == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"car catalog count is unavailable");

	if (resources == NULL || root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car catalog count request");

	status = source->count_cars(
		source->context, resources, root, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count == 0 || count > MH_CAR_CATALOG_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "invalid car catalog count");

	*output = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_car_config_load_from(const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	unsigned int number, struct mh_car *output, struct mh_error *error_out)
{
	if (source == NULL || source->load_car == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"car catalog load is unavailable");

	if (resources == NULL || root == NULL || output == NULL || number < 1 ||
		number > MH_CAR_CATALOG_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car catalog load request");

	return source->load_car(
		source->context, resources, root, number, output, error_out);
}

enum mh_error_code
mh_track_catalog_count_from(const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	unsigned int *output, struct mh_error *error_out)
{
	unsigned int count;
	enum mh_error_code status;

	if (source == NULL || source->count_tracks == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"track catalog count is unavailable");

	if (resources == NULL || root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track catalog count request");

	status = source->count_tracks(
		source->context, resources, root, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (count == 0 || count > MH_TRACK_CATALOG_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid track catalog count");

	*output = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_track_config_load_from(const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	unsigned int number, struct mh_track *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load_track == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"track catalog load is unavailable");

	if (resources == NULL || root == NULL || output == NULL || number < 1 ||
		number > MH_TRACK_CATALOG_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track catalog load request");

	return source->load_track(
		source->context, resources, root, number, output, error_out);
}

enum mh_error_code
mh_car_catalog_load_from(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *source, const char *root,
	struct mh_car_catalog *output, struct mh_error *error_out)
{
	struct mh_car_catalog catalog;
	unsigned int i;
	enum mh_error_code status;

	if (output == NULL || output->entries != NULL || output->count != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected empty car catalog");

	memset(&catalog, 0, sizeof(catalog));
	status = mh_car_catalog_count_from(
		catalog_source, source, root, &catalog.count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	catalog.entries = (struct mh_car *)calloc(
		catalog.count, sizeof(*catalog.entries));

	if (catalog.entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading car catalog");

	for (i = 0; i < catalog.count && status == MH_ERROR_NONE; i++)
		status = mh_car_config_load_from(catalog_source, source, root,
			i + 1, &catalog.entries[i], error_out);

	if (status != MH_ERROR_NONE)
		mh_car_catalog_close(&catalog);
	else
		*output = catalog;

	return status;
}

enum mh_error_code
mh_track_catalog_load_from(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *source, const char *root,
	struct mh_track_catalog *output, struct mh_error *error_out)
{
	struct mh_track_catalog catalog;
	unsigned int i, j;
	enum mh_error_code status;

	if (output == NULL || output->entries != NULL || output->count != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected empty track catalog");

	memset(&catalog, 0, sizeof(catalog));
	status = mh_track_catalog_count_from(
		catalog_source, source, root, &catalog.count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	catalog.entries = (struct mh_track *)calloc(
		catalog.count, sizeof(*catalog.entries));

	if (catalog.entries == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading track catalog");

	for (i = 0; i < catalog.count && status == MH_ERROR_NONE; i++)
		status = mh_track_config_load_from(catalog_source, source, root,
			i + 1, &catalog.entries[i], error_out);

	for (i = 0; status == MH_ERROR_NONE && i < catalog.count; i++)
		for (j = 0; j < i; j++)
			if ((catalog.entries[i].division >=
					MH_TRACK_CUSTOM_DIVISION ||
				catalog.entries[j].division >=
					MH_TRACK_CUSTOM_DIVISION) &&
				(catalog.entries[i].division ==
						catalog.entries[j].division ||
					mh_ascii_equal(catalog.entries[i].name,
						catalog.entries[j].name))) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"conflicting add-on track name or Division");
				break;
			}

	if (status != MH_ERROR_NONE)
		mh_track_catalog_close(&catalog);
	else
		*output = catalog;

	return status;
}
