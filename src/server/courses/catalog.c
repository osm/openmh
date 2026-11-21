#include "engine/core/text.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/catalog/source.h"
#include "server/courses/courses.h"

static enum mh_error_code
discover_courses(const struct mh_catalog_source *catalog,
	const struct mh_resource_source *resources, const char *root,
	struct mh_server_config *config, struct mh_error *error_out)
{
	struct mh_track track;
	struct mh_server_track *entry;
	unsigned int count, i, j;
	enum mh_error_code status;

	status = mh_track_catalog_count_from(
		catalog, resources, root, &count, error_out);

	if (status == MH_ERROR_NOT_FOUND)
		return mh_error_set(error_out, status, "no game courses found");

	if (status != MH_ERROR_NONE)
		return status;

	if (count > MH_SERVER_TRACKS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"game has too many courses for the server");

	for (i = 0; i < count; i++) {
		status = mh_track_config_load_from(
			catalog, resources, root, i + 1, &track, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (track.name[0] == '\0' || track.spline_file[0] == '\0')
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"server course requires a name and route");

		for (j = 0; j < i; j++)
			if (mh_ascii_equal(config->tracks[j].name, track.name))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate server course name");

		entry = &config->tracks[i];
		status = mh_text_copy(entry->name, sizeof(entry->name),
			track.name, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_text_copy(entry->path, sizeof(entry->path),
				track.spline_file, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		config->track_count++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_courses_configure(const struct mh_catalog_source *catalog,
	const struct mh_resource_source *resources, const char *root,
	struct mh_server_config *config, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (config == NULL || config->track_count > MH_SERVER_TRACKS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid server course configuration");

	if (config->track_count == 0) {
		status = discover_courses(
			catalog, resources, root, config, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (config->start_track[0] == '\0')
		return mh_text_copy(config->start_track,
			sizeof(config->start_track), config->tracks[0].name,
			error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_driver_catalog_load(const struct mh_catalog_source *catalog,
	const struct mh_horn_source *horn_source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_server_config *config, struct mh_error *error_out)
{
	struct mh_horn_catalog horns;
	struct mh_car car;
	unsigned int count, i;
	enum mh_error_code status;

	count = 0;
	status = mh_car_catalog_count_from(
		catalog, resources, root, &count, error_out);

	if (status != MH_ERROR_NONE && status != MH_ERROR_NOT_FOUND)
		return status;

	if (count > MH_CAR_CATALOG_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many server cars");

	for (i = 0; i < count; i++) {
		status = mh_car_config_load_from(
			catalog, resources, root, i + 1, &car, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_text_copy(config->cars[i],
				sizeof(config->cars[i]), car.name, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	config->car_count = count;

	status = mh_horn_catalog_load_from(
		horn_source, resources, root, &horns, error_out);

	if (status == MH_ERROR_NOT_FOUND) {
		config->horn_count = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status != MH_ERROR_NONE)
		return status;

	if (horns.count > MH_HORNS)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many server horns");

	for (i = 0; i < horns.count; i++) {
		status =
			mh_text_copy(config->horns[i], sizeof(config->horns[i]),
				horns.horns[i].name, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	config->horn_count = horns.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
