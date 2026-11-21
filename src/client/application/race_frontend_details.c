#include "client/application/race_frontend_internal.h"
#include "engine/image/track_map.h"
#include "engine/settings/course_music.h"

const char *
mh_application_control_key(const struct mh_application *app,
	unsigned int profile, unsigned int action)
{
	unsigned long code;

	if (profile >= mh_application_race_count(app, MH_FRONTEND_CONTROLS) ||
		action >= MH_CONTROL_FILE_BINDINGS)
		return NULL;

	code = app->race_frontend->controls.profiles[profile]
		.layout.bindings[action];

	return code < 256 ? mh_control_key_name((unsigned int)code) : NULL;
}

static double
rating(double value, double minimum, double maximum, int inverse)
{
	double normalized;

	/* Same bounds and acceleration direction as the original car menu. */
	normalized = (value - minimum) / (maximum - minimum);

	if (inverse)
		normalized = 1 - normalized;

	return normalized < 0 ? 0 : normalized > 1 ? 1 : normalized;
}

int
mh_application_car_details(const struct mh_application *app, unsigned int id,
	struct mh_frontend_car_details *output)
{
	const struct mh_car *car;

	if (output == NULL || id == 0 ||
		id > mh_application_race_count(app, MH_FRONTEND_CARS))
		return 0;

	car = &app->race_frontend->cars.entries[id - 1];
	output->has_ratings = car->has_statistics;
	output->speed = rating(car->top_speed, 250, 320, 0);
	output->acceleration = rating(car->acceleration, 2.2f, 3.2f, 1);
	output->handling = rating(car->handling, 3.5f, 8.5f, 0);

	return 1;
}

int
mh_application_track_details(const struct mh_application *app, unsigned int id,
	struct mh_frontend_track_details *output)
{
	const struct mh_track *track;

	if (output == NULL || id == 0 ||
		id > mh_application_race_count(app, MH_FRONTEND_TRACKS))
		return 0;

	track = &app->race_frontend->tracks.entries[id - 1];
	output->location = track->location;
	output->length = track->length;
	output->length_us = track->length_us;
	output->music_track = mh_settings_track_music(app->preferences, track);

	return 1;
}

enum mh_error_code
mh_application_track_map(const struct mh_application *app, unsigned int id,
	struct mh_image *output, struct mh_error *error_out)
{
	if (id == 0 || id > mh_application_race_count(app, MH_FRONTEND_TRACKS))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unknown track");

	return mh_track_map_load_from(app->session_options.image_decoder,
		app->resources, app->asset_root,
		&app->race_frontend->tracks.entries[id - 1], output, error_out);
}

static const struct mh_menu_graphics_device *
resolution_device(const struct mh_application *app, unsigned int renderer)
{
	const struct mh_menu_graphics_catalog *catalog;

	if (app == NULL || app->race_frontend == NULL)
		return NULL;

	catalog = mh_graphics_catalog_choices(app->race_frontend->graphics);

	return catalog != NULL && renderer < catalog->count
		? &catalog->devices[renderer]
		: NULL;
}

unsigned int
mh_application_resolution_count(
	const struct mh_application *app, unsigned int renderer)
{
	const struct mh_menu_graphics_device *device;

	device = resolution_device(app, renderer);

	return device != NULL ? device->count : 0;
}

int
mh_application_resolution(const struct mh_application *app,
	unsigned int renderer, unsigned int index, unsigned int *width,
	unsigned int *height)
{
	const struct mh_menu_graphics_device *device;

	device = resolution_device(app, renderer);

	if (device == NULL || index >= device->count || width == NULL ||
		height == NULL)
		return 0;

	*width = (unsigned int)device->modes[index].width;
	*height = (unsigned int)device->modes[index].height;

	return 1;
}
