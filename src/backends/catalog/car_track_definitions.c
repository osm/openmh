#include <stdio.h>
#include <stdlib.h>

#include "backends/catalog/internal.h"
#include "engine/assets/car.h"
#include "engine/assets/path_catalog.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track.h"
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum {
	CONFIG_PATH_SIZE = 1024,
	CONFIG_RELATIVE_SIZE = 64
};

static enum mh_error_code
load_text(const struct mh_resource_source *source, const char *root,
	const char *relative, unsigned char **text, size_t *size,
	struct mh_error *error_out)
{
	struct mh_file file;
	char path[CONFIG_PATH_SIZE];
	enum mh_error_code status;

	status = mh_asset_path_from(
		source, root, relative, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read(source, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_tbl_decode(file.data, file.size, text, size, error_out);
	free(file.data);

	return status;
}

enum mh_error_code
mh_car_config_car_load(const struct mh_resource_source *source,
	const char *root, unsigned int number, struct mh_car *car,
	struct mh_error *error_out)
{
	unsigned char *text;
	size_t text_size;
	char relative[CONFIG_RELATIVE_SIZE];
	enum mh_error_code status;

	if (number < 1 || number > MH_CAR_CATALOG_LIMIT || car == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car catalog entry");

	sprintf(relative, MH_ASSET_CAR_CONFIG_FORMAT, number);
	status =
		load_text(source, root, relative, &text, &text_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_car_config_car_decode(text, text_size, car, error_out);
	free(text);

	return status;
}

enum mh_error_code
mh_track_config_trk_load(const struct mh_resource_source *source,
	const char *root, unsigned int number, struct mh_track *track,
	struct mh_error *error_out)
{
	unsigned char *text;
	size_t text_size;
	char relative[MH_TRACK_PATH_SIZE];
	int custom;
	enum mh_error_code status;

	if (number < 1 || number > MH_TRACK_CATALOG_LIMIT || track == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track catalog entry");

	status = mh_track_definition_path(
		source, root, number, relative, &custom, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status =
		load_text(source, root, relative, &text, &text_size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_track_config_trk_decode(text, text_size, track, error_out);
	free(text);

	if (status == MH_ERROR_NONE && custom &&
		(!track->has_division ||
			track->division < MH_TRACK_CUSTOM_DIVISION))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"named add-on tracks require a unique Division of at least 100000");

	return status;
}
