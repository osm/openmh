#ifndef MH_CLIENT_RACE_FRONTEND_INTERNAL_H
#define MH_CLIENT_RACE_FRONTEND_INTERNAL_H

#include "client/application/application_internal.h"
#include "client/application/race_frontend.h"
#include "client/session/single_session.h"
#include "engine/audio/music_catalog.h"
#include "engine/catalog/load.h"
#include "engine/settings/control_catalog.h"
#include "engine/storage/control_store.h"
#include "engine/ui/menu_personal.h"
#include "engine/window/graphics_catalog.h"

struct mh_race_frontend {
	struct mh_multiplayer		*network;
	struct mh_graphics_catalog	*graphics;
	enum mh_frontend_state		state;
	struct mh_race_request		request;
	struct mh_settings		runtime_settings;
	struct mh_car_catalog		cars;
	struct mh_track_catalog		tracks;
	struct mh_music_catalog		music_catalog;
	unsigned char			car_available[MH_CAR_CATALOG_LIMIT];
	unsigned char			track_available[MH_TRACK_CATALOG_LIMIT];
	unsigned long			name_colors[MH_MENU_PERSONAL_COLORS];
	struct mh_horn_catalog		horns;
	struct mh_profile_catalog	profiles;
	struct mh_control_catalog	controls;
	struct mh_control_store		*control_store;
	struct mh_control_binding_set	bindings;
	struct mh_activity		activity;
	struct mh_single_control	control;
	struct mh_demo_file		*take;
	unsigned long			random;
	unsigned int	music[MH_TRACK_CATALOG_LIMIT], record_class, progress;
	struct mh_race_load_observer	observer;
	int				quit;
	char				error[512];
};

enum mh_error_code	mh_race_frontend_open(struct mh_application *app,
	struct mh_error *error_out);
enum mh_app_status	mh_race_frontend_step(struct mh_application *app);
void			mh_race_frontend_close(struct mh_application *app);
enum mh_error_code	mh_race_frontend_catalogs(struct mh_application *app,
	struct mh_error *error_out);
enum mh_error_code	mh_race_frontend_configure(struct mh_application *app,
	const struct mh_race_request *request, struct mh_settings *output,
	struct mh_error *error_out);
void			mh_race_frontend_defaults(
	const struct mh_application *app, struct mh_race_request *output);

int			mh_race_frontend_idle(const struct mh_application *app);
enum mh_error_code	mh_race_frontend_prepare_session(
	struct mh_application *app, int load_profiles,
	struct mh_session_options *options, struct mh_error *error_out);
void			mh_race_frontend_failed(
	struct mh_race_frontend *frontend, const struct mh_error *error);

#endif
