#include <stdlib.h>
#include <string.h>

#include "client/application/race_frontend.h"
#include "client/hosts/browser_multiplayer_api.h"
#include "client/hosts/browser_preferences_api.h"
#include "client/hosts/browser_race_api.h"

static struct mh_application *application;
static struct mh_race_request request;
static int enabled;
static char failure[512];
static struct mh_image track_map;

static int
result(enum mh_error_code status, const struct mh_error *error)
{
	size_t length;

	failure[0] = 0;

	if (status == MH_ERROR_NONE)
		return 1;

	length = strlen(error->message);

	if (length >= sizeof(failure))
		length = sizeof(failure) - 1;

	memcpy(failure, error->message, length);
	failure[length] = 0;

	return 0;
}

int
mh_browser_race_enable(void)
{
	if (application != NULL)
		return 0;

	enabled = 1;

	return 1;
}

int
mh_browser_race_attach(struct mh_application *app)
{
	struct mh_error error;
	enum mh_error_code status;

	application = app;
	mh_browser_preferences_attach(app);

	if (!enabled)
		return 1;

	status = mh_application_use_race_frontend(app, &error);

	if (status == MH_ERROR_NONE)
		mh_browser_multiplayer_attach(app, &request);

	return result(status, &error);
}

void
mh_browser_race_detach(void)
{
	mh_browser_multiplayer_detach();
	mh_browser_preferences_attach(NULL);
	free(track_map.rgba);
	memset(&track_map, 0, sizeof(track_map));
	application = NULL;
}

int
mh_browser_race_state(void)
{
	return (int)mh_application_race_state(application);
}

int
mh_browser_race_completed(void)
{
	return mh_application_race_completed(application);
}

unsigned int
mh_browser_race_count(unsigned int kind)
{
	return mh_application_race_count(
		application, (enum mh_frontend_catalog)kind);
}

const char *
mh_browser_race_choice(unsigned int kind, unsigned int index)
{
	int available;

	return mh_application_race_choice(
		application, (enum mh_frontend_catalog)kind, index, &available);
}

int
mh_browser_race_available(unsigned int kind, unsigned int index)
{
	int available;

	return mh_application_race_choice(application,
		(enum mh_frontend_catalog)kind, index,
		&available) != NULL &&
		available;
}

int
mh_browser_race_reset(void)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_race_defaults(application, &request, &error);

	return result(status, &error);
}

const char *
mh_browser_race_control_key(unsigned int profile, unsigned int action)
{
	return mh_application_control_key(application, profile, action);
}

int
mh_browser_race_bind_key(
	unsigned int profile, unsigned int action, const char *key)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_bind_key(
		application, profile, action, key, &error);

	return result(status, &error);
}

int
mh_browser_race_get(const char *key)
{
	return mh_race_request_get(&request, key);
}

int
mh_browser_race_set(const char *key, unsigned int value)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_race_request_set(&request, key, value, &error);

	return result(status, &error);
}

const char *
mh_browser_race_player(void)
{
	return request.name;
}

int
mh_browser_race_name(const char *name)
{
	if (name == NULL || strlen(name) >= sizeof(request.name)) {
		strcpy(failure, "player name must contain 1 to 15 characters");
		return 0;
	}

	strcpy(request.name, name);

	return 1;
}

int
mh_browser_race_set_team(const char *team)
{
	if (team == NULL || strlen(team) >= sizeof(request.team)) {
		strcpy(failure, "team must contain at most 4 characters");
		return 0;
	}

	strcpy(request.team, team);

	return 1;
}

const char *
mh_browser_race_team(void)
{
	return request.team;
}

int
mh_browser_race_start(void)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_start_race(application, &request, &error);

	return result(status, &error);
}

int
mh_browser_race_leave(void)
{
	struct mh_error error;
	enum mh_error_code status;

	status = mh_application_leave_race(application, &error);

	return result(status, &error);
}

const char *
mh_browser_race_error(void)
{
	return failure[0] != 0 ? failure
			: mh_application_race_error(application);
}

unsigned int
mh_browser_race_progress(void)
{
	return mh_application_race_progress(application);
}

unsigned long
mh_browser_race_result(unsigned int index, unsigned int field)
{
	const struct mh_standings *standings;
	const struct mh_race_result *car;

	standings = mh_application_race_results(application);

	if (standings == NULL)
		return 0;

	if (field == 0)
		return standings->count;

	if (index >= standings->count)
		return 0;

	car = &standings->cars[index];

	switch (field) {
	case 1:
		return car->position;
	case 2:
		return car->finished != 0;
	case 3:
		return car->finish_ms;
	case 4:
		return car->best_ms;
	case 5:
		return car->estimated != 0;
	}

	return 0;
}

double
mh_browser_race_car_rating(unsigned int id, unsigned int field)
{
	struct mh_frontend_car_details details;

	if (!mh_application_car_details(application, id, &details) ||
		!details.has_ratings)
		return -1;

	switch (field) {
	case 0:
		return details.speed;
	case 1:
		return details.acceleration;
	case 2:
		return details.handling;
	}

	return -1;
}

const char *
mh_browser_race_track_detail(unsigned int id, unsigned int field)
{
	struct mh_frontend_track_details details;

	if (!mh_application_track_details(application, id, &details))
		return NULL;

	switch (field) {
	case 0:
		return details.location;
	case 1:
		return details.length;
	case 2:
		return details.length_us;
	}

	return NULL;
}

int
mh_browser_race_track_music(unsigned int id)
{
	struct mh_frontend_track_details details;

	return mh_application_track_details(application, id, &details)
		? (int)details.music_track
		: -1;
}

int
mh_browser_race_map_load(unsigned int id)
{
	struct mh_error error;
	enum mh_error_code status;

	free(track_map.rgba);
	memset(&track_map, 0, sizeof(track_map));
	status = mh_application_track_map(application, id, &track_map, &error);

	return result(status, &error);
}

unsigned int
mh_browser_race_map_size(unsigned int dimension)
{
	return dimension == 0 ? track_map.width : track_map.height;
}

const unsigned char *
mh_browser_race_map_pixels(void)
{
	return track_map.rgba;
}

unsigned int
mh_browser_race_resolution_count(unsigned int renderer)
{
	return mh_application_resolution_count(application, renderer);
}

unsigned int
mh_browser_race_resolution(
	unsigned int renderer, unsigned int index, unsigned int dimension)
{
	unsigned int width, height;

	if (!mh_application_resolution(
		application, renderer, index, &width, &height))
		return 0;

	return dimension == 0 ? width : height;
}

int
mh_browser_race_name_color(unsigned int index)
{
	return mh_application_name_color(application, index);
}
