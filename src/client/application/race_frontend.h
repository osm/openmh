#ifndef MH_CLIENT_RACE_FRONTEND_H
#define MH_CLIENT_RACE_FRONTEND_H

#include "engine/game/standings.h"
#include "engine/settings/config.h"
#include "interfaces/image/pixels.h"

struct mh_application;

enum mh_frontend_state {
	MH_FRONTEND_PREPARING,
	MH_FRONTEND_READY,
	MH_FRONTEND_STARTING,
	MH_FRONTEND_RACING,
	MH_FRONTEND_FINISHED,
	MH_FRONTEND_FAILED
};

enum mh_frontend_catalog {
	MH_FRONTEND_CARS,
	MH_FRONTEND_TRACKS,
	MH_FRONTEND_RENDERERS,
	MH_FRONTEND_CONTROLS,
	MH_FRONTEND_MODES,
	MH_FRONTEND_HORNS,
	MH_FRONTEND_MUSIC,
	MH_FRONTEND_AUDIO_PROFILES
};

/*
 * Car/course IDs are one based; music uses CD track numbers (2 and above).
 * Other selections are zero based.
 */
struct mh_race_request {
	char		name[MH_SETTINGS_PLAYER_BYTES];
	char		team[MH_SETTINGS_TEAM_BYTES];
	unsigned int	player_color;
	unsigned int	car, track, mode, laps, difficulty, automatic, catch_up;
	unsigned int	renderer, width, height, detail, show_fps;
	unsigned int	show_player_names;
	unsigned int effects_volume, music_volume, audio_profile, controls,
		horn;

	/* Zero preserves the selected course's saved music choice. */
	unsigned int	music_track;
};

/* Borrowed authored text and normalized original menu ratings (0..1). */
struct mh_frontend_car_details {
	int	has_ratings;
	double	speed, acceleration, handling;
};

struct mh_frontend_track_details {
	const char	*location, *length, *length_us;
	unsigned int	music_track;
};

/* Original personal-menu colors as 0xRRGGBB; -1 ends the palette. */
int	mh_application_name_color(const struct mh_application *app,
	unsigned int index);

int	mh_application_car_details(const struct mh_application *app,
	unsigned int id, struct mh_frontend_car_details *output);
int	mh_application_track_details(const struct mh_application *app,
	unsigned int id, struct mh_frontend_track_details *output);

/* Map pixels are caller owned; free output->rgba. IDs are one based. */
enum mh_error_code	mh_application_track_map(
	const struct mh_application *app, unsigned int id,
	struct mh_image *output, struct mh_error *error_out);
unsigned int		mh_application_resolution_count(
	const struct mh_application *app, unsigned int renderer);
int			mh_application_resolution(
	const struct mh_application *app, unsigned int renderer,
	unsigned int index, unsigned int *width, unsigned int *height);

/*
 * Select before the first application step. The ordinary menu path is
 * unchanged.
 */
enum mh_error_code	mh_application_use_race_frontend(
	struct mh_application *app, struct mh_error *error_out);
enum mh_frontend_state	mh_application_race_state(
	const struct mh_application *app);

/* The player crossed the finish line; the finish presentation may still run. */
int		 mh_application_race_completed(
	const struct mh_application *app);
unsigned int	 mh_application_race_count(const struct mh_application *app,
	enum mh_frontend_catalog kind);
const char	*mh_application_race_choice(const struct mh_application *app,
	enum mh_frontend_catalog kind, unsigned int index, int *available);

/* Action is a mh_control_action index. Invalid or unbound keys return NULL. */
const char	*mh_application_control_key(const struct mh_application *app,
	unsigned int profile, unsigned int action);

/* Edit an idle profile; the next race launch saves dirty control profiles. */
enum mh_error_code	mh_application_bind_key(struct mh_application *app,
	unsigned int profile, unsigned int action, const char *key,
	struct mh_error *error_out);
enum mh_error_code	mh_application_race_defaults(
	const struct mh_application *app, struct mh_race_request *output,
	struct mh_error *error_out);

/* Validates and queues a launch; session creation happens on the next step. */
enum mh_error_code		 mh_application_start_race(
	struct mh_application *app, const struct mh_race_request *request,
	struct mh_error *error_out);
enum mh_error_code		 mh_application_leave_race(
	struct mh_application *app, struct mh_error *error_out);
const struct mh_standings	*mh_application_race_results(
	const struct mh_application *app);
const char			*mh_application_race_error(
	const struct mh_application *app);
unsigned int			 mh_application_race_progress(
	const struct mh_application *app);

/* Named numeric fields for thin host adapters. Unknown names are errors. */
int			mh_race_request_get(
	const struct mh_race_request *request, const char *name);
enum mh_error_code	mh_race_request_set(struct mh_race_request *request,
	const char *name, unsigned int value, struct mh_error *error_out);

#endif
