#ifndef MH_CLIENT_APPLICATION_INTERNAL_H
#define MH_CLIENT_APPLICATION_INTERNAL_H

#include "client/application/activity.h"
#include "client/application/application.h"
#include "client/application/host.h"
#include "client/menus/pages.h"
#include "client/menus/sound.h"
#include "client/session/options.h"
#include "client/settings/store.h"
#include "engine/assets/content_edition.h"
#include "engine/clock/game_timer.h"
#include "engine/clock/paused.h"
#include "engine/game/highscore_awards.h"
#include "engine/storage/storage.h"
#include "engine/storage/vfs.h"
#include "interfaces/input/input.h"

struct mh_menus;
struct mh_window;
struct mh_canvas;

enum mh_application_override {
	MH_APP_OVERRIDE_CAR = 1,
	MH_APP_OVERRIDE_TRACK = 2,
	MH_APP_OVERRIDE_MODE = 4,
	MH_APP_OVERRIDE_LAPS = 8,
	MH_APP_OVERRIDE_CATCH_UP = 16,
	MH_APP_OVERRIDE_AUTOMATIC = 32,
	MH_APP_OVERRIDE_RECORD = 64,
	MH_APP_OVERRIDE_CONFIRMATION = 128,
	MH_APP_OVERRIDE_FULLSCREEN = 256,
	MH_APP_OVERRIDE_SHOW_FPS = 512,
	MH_APP_OVERRIDE_GRAB_MOUSE = 1024,
	MH_APP_OVERRIDE_VSYNC = 2048,
	MH_APP_OVERRIDE_MAX_FPS = 4096
};

enum application_phase {
	APPLICATION_START,
	APPLICATION_OPENING,
	APPLICATION_MENUS_INIT,
	APPLICATION_MENU,
	APPLICATION_RACE_FRONTEND
};

/*
 * Private state owned by application.c; never copied after initialization.
 * Command-line strings borrow argv; a resolved progress path borrows menus.
 * preferences borrows settings_store. Menu callbacks and session options
 * borrow fields of this object and its menus.
 */
struct mh_application {
	struct mh_race_frontend				*race_frontend;
	struct mh_menu_configuration			menu_configuration;
	const struct mh_content_edition			*content_edition;
	struct mh_content_intro				content_intro;
	const char					*installation_root;
	struct mh_app_lifecycle				lifecycle;
	struct mh_paused_clock				clock;
	int						suspended;
	const struct mh_image_capture			*capture;

	/* Renderer descriptors/configurations borrowed through teardown. */
	const struct mh_render_set			*renderers;
	const struct mh_canvas_provider			*canvases;

	/* Window system borrowed through child/window teardown. */
	const struct mh_window_system			*windows;
	const struct mh_input_provider			*inputs;
	const struct mh_audio_decoder_provider		*audio_decoder;
	const struct mh_audio_provider			*audio_provider;
	const struct mh_user_data			*user_data;

	/*
	 * Owned selected user-data root view; source provider stays
	 * borrowed.
	 */
	const struct mh_user_data			*content_user_data;
	const struct mh_resource_source			*resources;
	const struct mh_resource_archive_provider	*resource_archives;
	struct mh_vfs					*asset_mount;
	int						assets_explicit;
	struct mh_game_timer				timer;
	char						game_directory[MH_APPLICATION_PATH_SIZE];
	const char *asset_root, *user_data_directory, *capture_path,
		*indexed_path, *horn_name, *progress_directory, *settings_path,
		*player_name, *team_name, *league_directory;
	const char	*controls_directory, *addresses_directory, *connect_address;
	struct mh_settings_store			*settings_store;
	struct mh_settings				defaults, *preferences;
	struct mh_session_options			session_options;

	const struct mh_image_encoder
		*screenshot_encoders[MH_SCREENSHOT_FORMAT_COUNT];
	struct mh_highscore_history			score_history;
	struct mh_menus					*menus;
	struct mh_keyboard				keyboard;
	struct mh_input					devices;
	unsigned char					down[MH_KEY_COUNT];
	struct mh_window				*window;
	struct mh_canvas				*canvas;
	unsigned long frame_index, frame_limit, start_selection, initial_seed,
		capture_step_ms, menu_confirmation, frame_started;
	unsigned long					launch_delay;
	enum mh_app_status				launch_status;
	unsigned long game_type, car_number, track_number, laps, catch_up,
		unlocked_tracks, unlocked_cars, special_cars_override,
		automatic, record_race, stage;
	unsigned int	startup_dialog, overrides;

	unsigned int
		renderer; /* Catalog count means no command-line override. */
	int running, success, window_acquired, audio_mode, no_settings,
		no_intro, opening_outcome, unlock_all;
	int						idle_enabled;
	unsigned int	fullscreen, window_fullscreen, show_fps, grab_mouse;
	unsigned int					vsync;
	unsigned long					max_fps;
	unsigned long					settings_seed;

	enum mh_menus_start_page			start_page;
	enum application_phase				phase;
	enum mh_app_status				status;
	struct mh_activity				opening;
	unsigned long					delay;
	int	exit_code, resume_launch;
	struct mh_storage				*storage;
	enum mh_storage_operation			storage_operation;
	enum mh_app_status				storage_resume_status;
	unsigned long					storage_resume_delay;
	int	storage_prepared, storage_waiting, storage_started, storage_failed;
	int						finalized;
	char						error[512];
};

/* Negative means startup can proceed; otherwise return the native exit code. */
int	mh_application_parse_arguments(struct mh_application *application,
	int argc, char **argv);

enum mh_error_code	mh_application_load_content(struct mh_application *app,
	struct mh_error *error_out);
enum mh_error_code	mh_application_content_saves(struct mh_application *app,
	struct mh_error *error_out);

enum mh_error_code	mh_application_load_settings(struct mh_application *app,
	struct mh_error *error_out);

/* Menu setup/frame adapters stop through the shared application lifetime. */
enum mh_error_code	mh_application_open_window(struct mh_application *app,
	const char *title, struct mh_error *error_out);
void			mh_application_close_window(struct mh_application *app);
enum mh_app_status	mh_application_open_menus(
	struct mh_application *application);
enum mh_app_status	mh_application_step_menus(
	struct mh_application *application);
enum mh_app_status	mh_application_stop(struct mh_application *application,
	int exit_code);

void			mh_application_begin_storage(struct mh_application *app,
	enum mh_storage_operation operation);
enum mh_error_code	mh_application_save_menus(struct mh_application *app,
	struct mh_error *error_out);

#endif
