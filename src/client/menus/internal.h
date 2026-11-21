#ifndef MH_CLIENT_MENUS_INTERNAL_H
#define MH_CLIENT_MENUS_INTERNAL_H

#include "client/menus/compact.h"
#include "client/menus/configuration.h"
#include "client/menus/pages.h"
#include "client/menus/sound.h"
#include "engine/assets/content_edition.h"
#include "engine/ui/menu_modal.h"
#include "interfaces/window/window.h"

/*
 * Private application menu state and page callbacks. Owned by
 * application.c; page modules borrow it synchronously and preserve original
 * event order.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/session/attract_session.h"
#include "client/session/demo_session.h"
#include "client/session/league_session.h"
#include "client/session/leagues.h"
#include "client/session/network_game.h"
#include "client/session/progress.h"
#include "client/session/single_session.h"
#include "client/settings/store.h"
#include "engine/audio/menu_playback.h"
#include "engine/audio/music_catalog.h"
#include "engine/catalog/load.h"
#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "engine/core/limits.h"
#include "engine/game/cheats.h"
#include "engine/game/mode.h"
#include "engine/geometry/wireframe.h"
#include "engine/input/control_type.h"
#include "engine/input/keyboard.h"
#include "engine/settings/menu_return.h"
#include "engine/settings/menu_sound.h"
#include "engine/storage/address_store.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/control_store.h"
#include "engine/storage/demo_file.h"
#include "engine/storage/highscore_file.h"
#include "engine/storage/league_file.h"
#include "engine/storage/recording_catalog.h"
#include "engine/storage/recording_path.h"
#include "engine/ui/attract.h"
#include "engine/ui/menu.h"
#include "engine/ui/menu_backdrop.h"
#include "engine/ui/menu_background.h"
#include "engine/ui/menu_car_draw.h"
#include "engine/ui/menu_catalog.h"
#include "engine/ui/menu_controls_draw.h"
#include "engine/ui/menu_credits.h"
#include "engine/ui/menu_demo_catalog.h"
#include "engine/ui/menu_dialog.h"
#include "engine/ui/menu_ghost_draw.h"
#include "engine/ui/menu_graphics_draw.h"
#include "engine/ui/menu_league_info.h"
#include "engine/ui/menu_main.h"
#include "engine/ui/menu_multi_draw.h"
#include "engine/ui/menu_noise.h"
#include "engine/ui/menu_options.h"
#include "engine/ui/menu_palette.h"
#include "engine/ui/menu_personal_draw.h"
#include "engine/ui/menu_race_draw.h"
#include "engine/ui/menu_rankings_draw.h"
#include "engine/ui/menu_recording_draw.h"
#include "engine/ui/menu_single_draw.h"
#include "engine/ui/menu_sound_draw.h"
#include "engine/ui/menu_unlocks.h"
#include "engine/ui/opening_videos.h"
#include "engine/window/graphics_catalog.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/geometry/wireframe_source.h"
#include "interfaces/image/palette.h"
#include "interfaces/input/input.h"
#include "interfaces/network/network.h"
#include "interfaces/storage/user_data.h"

struct mh_horn_source;

#define MH_MENUS_PATH_BYTES 1024
#define MH_MENUS_IPV4_BYTES 16
#define MH_MENUS_ATTRACT_NAME_BYTES 16

/*
 * The presented menu is fixed-size; the work canvas includes clipping padding.
 */
#define MH_MENUS_WIDTH 640
#define MH_MENUS_HEIGHT 400
#define MH_MENUS_PIXELS ((unsigned long)MH_MENUS_WIDTH * MH_MENUS_HEIGHT)

/* Private page-array indices, distinct from menu destinations/layout IDs. */
enum mh_menus_league_page {
	MH_MENUS_LEAGUE_SETUP = 0,
	MH_MENUS_LEAGUE_STANDINGS = 1,
	MH_MENUS_LEAGUE_LOAD = 2,
	MH_MENUS_LEAGUE_NEW = 3,
	MH_MENUS_LEAGUE_DELETE = 4,
	MH_MENUS_LEAGUE_COUNT = 5
};

struct mh_menus_launch;

/* Menu pages with native race and recording playback sessions. */
struct mh_resource_source;
struct mh_font_source_provider;
struct mh_catalog_source;
struct mh_profile_source;
struct mh_palette_source;
struct mh_menu_layout_source;
struct mh_wireframe_source;
struct mh_sprite_source_provider;
struct mh_image_decoder_provider;

struct mh_menus {
	struct mh_debug_options			debug;
	struct mh_menu_configuration		configuration;
	struct mh_menu_choices main_choices, options_choices, single_choices,
		compact_choices, controls_choices;

	/*
	 * Borrowed static content descriptor; compact menus need no retail art.
	 */
	const struct mh_content_edition		*edition;
	unsigned int				compact_row;
	char					compact_notice[256];
	struct mh_car_catalog			car_catalog;
	struct mh_track_catalog			track_catalog;

	/* Window system borrowed through child/window teardown. */
	const struct mh_window_system		*windows;
	const struct mh_input_provider		*inputs;
	const struct mh_audio_decoder_provider	*audio_decoder;
	const struct mh_audio_provider		*audio_provider;

	/* Borrowed from the application through menu/session teardown. */
	const struct mh_resource_source		*resources;
	const struct mh_font_source_provider	*font_source;
	const struct mh_horn_source		*horn_source;
	const struct mh_palette_source		*palette_source;
	const struct mh_menu_layout_source	*layout_source;
	const struct mh_profile_source		*profile_source;
	const struct mh_catalog_source		*catalog_source;
	const struct mh_wireframe_source	*wireframe_source;
	const struct mh_sprite_source_provider	*sprite_source;
	const struct mh_image_decoder_provider	*image_decoder;
	const struct mh_image_decoder_provider	*thumbnail_decoder;
	const struct mh_user_data		*user_data;
	struct mh_game_timer			*timer;
	struct mh_menus_launch			*launch;

	struct mh_menu_layout			layout;
	struct mh_menu_main_art			main_art;
	struct mh_menu_main_animation		main_animation;
	struct mh_menu_main_state		main;

	struct mh_menu_options_state		options;
	struct mh_menu_options_art		options_art;
	struct mh_menu_options_animation	options_animation;

	struct mh_menu_sound_state		sound;
	struct mh_menu_sound_values		sound_values;
	struct mh_menu_sound_art		sound_art;
	struct mh_menu_sound_animation		sound_animation;
	struct mh_menu_sound_input		sound_keys;
	struct mh_music_catalog			music_catalog;

	struct mh_menu_graphics_state		graphics;
	struct mh_menu_graphics_art		graphics_art;
	struct mh_menu_graphics_animation	graphics_animation;
	struct mh_menu_graphics_input		graphics_keys;
	struct mh_graphics_catalog		*graphics_catalog;

	struct mh_menu_personal_state		personal;
	struct mh_menu_personal_art		personal_art;
	struct mh_menu_personal_animation	personal_animation;
	struct mh_menu_personal_input		personal_keys;
	unsigned long				personal_colors[MH_MENU_PERSONAL_COLORS];

	struct mh_menu_controls_state		controls;
	struct mh_menu_controls_art		controls_art;
	struct mh_menu_controls_animation	controls_animation;
	struct mh_menu_controls_input		controls_keys;
	struct mh_control_catalog		controls_catalog;
	struct mh_control_binding_set		applied_controls;
	struct mh_control_store			*controls_store;
	unsigned int				controls_entries, input_mode;
	int					controls_persistent;

	struct mh_menu_multi_state		multi;
	struct mh_menu_multi_art		multi_art;
	struct mh_menu_multi_animation		multi_animation;
	struct mh_menu_multi_input		multi_keys;
	struct mh_menu_page			multi_page;
	struct mh_address_store			*address_store;
	struct mh_address_book			addresses;
	struct mh_menu_multi_address		address_rows[MH_ADDRESS_LIMIT];
	unsigned int				multi_entries;
	int					keyboard_reset;

	const struct mh_transport_provider	*network;
	enum mh_net_wire			network_wire;
	struct mh_transport			*discovery_transport;
	struct mh_discovery_catalog		discovered_sessions;
	const char				*session_names[MH_DISCOVERY_SESSIONS];
	unsigned long				discovery_token;
	unsigned int				network_port;
	struct mh_network_game_request		connection;
	struct mh_transport_target		*connection_target;
	char server_address[MH_MENUS_IPV4_BYTES],
		connection_name[MH_NET_NAME_BYTES];
	struct mh_lobby_settings_catalog	network_catalog;
	const char *network_cars[MH_CAR_CATALOG_LIMIT],
		*network_tracks[MH_TRACK_CATALOG_LIMIT];

	struct mh_menu_credits_art		credits_art;
	struct mh_menu_credits_animation	credits_animation;
	struct mh_menu_credits_input		credits_keys;
	struct mh_menu_page			credits_page;
	unsigned int				credits_entries;
	int					credits_bank;

	struct mh_menu_rankings_state		rankings;
	struct mh_menu_rankings_art		rankings_art;
	struct mh_menu_rankings_animation	rankings_animation;
	struct mh_menu_rankings_input		rankings_keys;
	struct mh_highscore_table		rankings_table;
	struct mh_menu_page			rankings_page;
	unsigned int	rankings_entries, rankings_loads;
	int					rankings_loaded;
	const char				*rankings_directory;
	char					*rankings_preferred;

	struct mh_menu_page options_page, sound_page, graphics_page,
		personal_page, controls_page;
	unsigned int	graphics_entries, personal_entries;

	unsigned long course_music[MH_TRACK_CATALOG_LIMIT]; /* Original sorted
							frontend catalog. */
	unsigned int source_music[MH_TRACK_CATALOG_LIMIT], options_entries,
		sound_entries;
	const char				*music_root;

	struct mh_menu_single_art		single_art;
	struct mh_menu_single_animation		single_animation;
	struct mh_menu_single_state		single;
	struct mh_menu_single_details		details;

	struct mh_menu_race_art			race_art;
	struct mh_menu_race_animation		race_animation;
	struct mh_menu_race_setup_state		race;
	struct mh_menu_keys			setup_keys;
	struct mh_menu_race_scene		race_scene;
	struct mh_menu_digits			digits;
	struct mh_menu_thumbnail		track_thumbnails[MH_TRACK_CATALOG_LIMIT];
	struct mh_wireframe			track_models[MH_TRACK_CATALOG_LIMIT];
	const char				*track_titles[MH_TRACK_CATALOG_LIMIT];
	unsigned int	track_numbers[MH_TRACK_CATALOG_LIMIT], availability;
	struct mh_menu_track_unlocks		track_unlocks;
	struct mh_menu_car_unlocks		car_unlocks;
	unsigned int				race_entries, race_leaves;

	struct mh_menu_car_art			car_art;
	struct mh_menu_car_animation		car_animation;
	struct mh_menu_car_setup_state		car;
	struct mh_menu_car_scene		car_scene;
	struct mh_menu_thumbnail		car_thumbnails[MH_CAR_CATALOG_LIMIT];
	struct mh_wireframe			car_models[MH_CAR_CATALOG_LIMIT];
	const char	*car_titles[MH_CAR_CATALOG_LIMIT], *horn_titles[MH_HORNS];
	unsigned long				car_divisions[MH_CAR_CATALOG_LIMIT];
	unsigned int car_numbers[MH_CAR_CATALOG_LIMIT], car_entries, car_leaves,
		horn_previews;
	float					car_statistics[MH_CAR_CATALOG_LIMIT][MH_MENU_CAR_STATISTICS];
	struct mh_horn_catalog			horns;

	struct mh_profile_catalog		*profiles;
	struct mh_settings			*settings;
	struct mh_leagues			*leagues;

	struct mh_menu_league_state		league;
	struct mh_menu_league_new_input		league_input;
	struct mh_menu_league_info		league_info[MH_LEAGUE_CATALOG_MAX_ENTRIES];
	struct mh_menu_league_art		league_art[MH_MENUS_LEAGUE_COUNT];
	struct mh_menu_league_animation		league_animation[MH_MENUS_LEAGUE_COUNT];
	struct mh_menu_page			league_page[MH_MENUS_LEAGUE_COUNT];
	const char				*league_directory;
	char					league_path[MH_MENUS_PATH_BYTES];
	const char				*progress_directory;
	char					progress_path[MH_PROGRESS_PATH_SIZE];
	int					track_override, car_override;
	unsigned int				league_entries[MH_MENUS_LEAGUE_COUNT];
	int					capture_only;
	int	special_cars, unlock_cheat, unlock_all;
	struct mh_menu_single_item cars[MH_CAR_CATALOG_LIMIT],
		tracks[MH_TRACK_CATALOG_LIMIT];

	char					demo_name[MH_MENU_DEMO_NAME_SIZE];
	struct mh_recording_catalog		recordings;
	const char				*asset_root;
	char					recording_directory[MH_MENUS_PATH_BYTES];
	const char				*demo_names[MH_MENU_DEMO_MAX_FILES];

	struct mh_menu_ghost_art		ghost_art;
	struct mh_menu_ghost_animation		ghost_animation;
	struct mh_menu_ghost_state		ghost;
	struct mh_menu_ghost_input		ghost_input;

	struct mh_menu_recording		saving;
	struct mh_menu_recording_input		saving_input;
	struct mh_demo_file			*take;
	struct mh_menu_page			save_page, *save_return;
	unsigned int	ghost_entries, ghost_initial_focus;
	unsigned int				single_entries, single_leaves;

	struct mh_menu_input			navigation_keys;
	struct mh_menu_modal_state		modal;
	struct mh_menu_dialog_text		dialog_text;
	struct mh_font				font;
	struct mh_sprite_indexed		dialog_sprite;

	struct mh_menu_backdrop			backdrop;
	struct mh_wireframe			background_models[MH_MENU_BACKGROUND_MODELS];
	struct mh_menu_background		background;
	struct mh_menu_background_draw		background_draw;
	struct mh_menu_canvas			work, present;
	struct mh_menu				menu;
	struct mh_menu_page main_page, single_page, race_page, car_page,
		ghost_page, launch_page, attract_page, exit_page;
	char					attract_previous[MH_MENUS_ATTRACT_NAME_BYTES];

	unsigned long				idle_started;

	unsigned char palette[MH_MENU_PALETTE_BYTES],
		present_palette[MH_MENU_PALETTE_BYTES],
		static_noise[MH_MENU_NOISE_BYTES], *rgba;
	unsigned long	random_state, row_noise, clock_ms;
	unsigned int dialog_kind, startup_dialog, exit_requested,
		launch_requested, launch_mode;

	struct mh_error				error;
	struct mh_menu_audio			*audio;
};

enum mh_error_code	mh_menus_catalogs_load(const char *asset_root,
	const char *progress_directory, struct mh_menus *menus,
	struct mh_error *error_out);
enum mh_error_code	mh_menus_unlocks_apply(struct mh_menus *menus,
	const unsigned long divisions[MH_TRACK_CATALOG_LIMIT],
	const unsigned long flags[MH_TRACK_CATALOG_LIMIT],
	struct mh_error *error_out);

enum mh_error_code	mh_menus_recordings_reload(const char *asset_root,
	struct mh_menus *menus, struct mh_error *error_out);

enum mh_error_code	mh_menus_unlocks_refresh(struct mh_menus *menus,
	struct mh_error *error_out);

/* Load once; partial resources remain owned here for destroy on failure. */
enum mh_error_code	mh_menus_resources_load(const char *asset_root,
	const char *progress_directory, struct mh_menus *menus,
	struct mh_error *error_out);

/*
 * Consume heap-allocated menu state, including its active launch and services.
 */
void	mh_menus_destroy(struct mh_menus *menus);

void	mh_menus_sound_event(void *context, unsigned int event);

void	mh_menus_set_input_mode(void *context, unsigned int mode);

enum mh_error_code	mh_menus_controls_load(struct mh_menus *menus,
	const char *directory, int persist, struct mh_error *error_out);

enum mh_error_code	mh_menus_controls_reload(struct mh_menus *menus,
	struct mh_error *error_out);

enum mh_error_code	mh_menus_controls_save(struct mh_menus *menus,
	struct mh_error *error_out);

void	mh_menus_options_init(struct mh_menu *menu, void *context);

void	mh_menus_options_draw(struct mh_menu *menu, void *context);

void	mh_menus_options_input(struct mh_menu *menu, void *context);

void	mh_menus_controls_init(struct mh_menu *menu, void *context);

void	mh_menus_controls_draw(struct mh_menu *menu, void *context);

void	mh_menus_controls_input(struct mh_menu *menu, void *context);

void	mh_menus_rankings_init(struct mh_menu *menu, void *context);

void	mh_menus_rankings_draw(struct mh_menu *menu, void *context);

void	mh_menus_rankings_input(struct mh_menu *menu, void *context);

void	mh_menus_rankings_leave(struct mh_menu *menu, void *context);

void	mh_menus_credits_init(struct mh_menu *menu, void *context);

void	mh_menus_credits_draw(struct mh_menu *menu, void *context);

void	mh_menus_credits_input(struct mh_menu *menu, void *context);

void	mh_menus_personal_init(struct mh_menu *menu, void *context);

void	mh_menus_personal_draw(struct mh_menu *menu, void *context);

void	mh_menus_personal_input(struct mh_menu *menu, void *context);

void	mh_menus_graphics_init(struct mh_menu *menu, void *context);

void	mh_menus_graphics_draw(struct mh_menu *menu, void *context);

void	mh_menus_graphics_input(struct mh_menu *menu, void *context);

void	mh_menus_sound_init(struct mh_menu *menu, void *context);

void	mh_menus_sound_draw(struct mh_menu *menu, void *context);

void	mh_menus_sound_input(struct mh_menu *menu, void *context);

void	mh_menus_sound_leave(struct mh_menu *menu, void *context);

void	mh_menus_main_init(struct mh_menu *menu, void *context);

void	mh_menus_main_draw(struct mh_menu *menu, void *context);

void	mh_menus_open_dialog(struct mh_menus *menus, unsigned int kind);

void	mh_menus_trace_frames(const struct mh_menus *menus, const char *page,
	const unsigned int frames[MH_MENU_LAYOUT_SLOTS]);

void	mh_menus_single_init(struct mh_menu *menu, void *context);

void	mh_menus_single_draw(struct mh_menu *menu, void *context);

void	mh_menus_show_notice(struct mh_menus *menus, const unsigned char *title,
	size_t size);

void	mh_menus_show_error(struct mh_menus *menus, struct mh_error error);

enum mh_error_code	mh_menus_multiplayer_addresses_load(
	struct mh_menus *menus, const char *directory, int persist,
	struct mh_error *error_out);
enum mh_error_code	mh_menus_multiplayer_addresses_reload(
	struct mh_menus *menus, struct mh_error *error_out);

void	mh_menus_multiplayer_init(struct mh_menu *menu, void *context);

void	mh_menus_multiplayer_draw(struct mh_menu *menu, void *context);

void	mh_menus_multiplayer_leave(struct mh_menu *menu, void *context);

void	mh_menus_multiplayer_input(struct mh_menu *menu, void *context);

enum mh_error_code	mh_menus_profiles_load(struct mh_menus *menus,
	struct mh_error *error_out);

enum mh_error_code	mh_menus_league_creation(struct mh_menus *menus,
	struct mh_league_creation *creation, struct mh_error *error_out);

enum mh_error_code	mh_menus_league_sync(struct mh_menus *menus,
	struct mh_error *error_out);

enum mh_error_code	mh_menus_league_prepare(struct mh_menus *menus,
	struct mh_error *error_out);

/* Returns MH_MENUS_LEAGUE_COUNT outside the five league pages. */
unsigned int	mh_menus_league_page_index(const struct mh_menus *menus);

enum mh_error_code	mh_menus_league_reload(struct mh_menus *menus,
	struct mh_error *error_out);

void	mh_menus_league_request(struct mh_menus *menus,
	unsigned int destination);

void	mh_menus_league_init(struct mh_menu *menu, void *context);

void	mh_menus_league_draw(struct mh_menu *menu, void *context);

void	mh_menus_league_input(struct mh_menu *menu, void *context);

void	mh_menus_save_input(struct mh_menu *menu, void *context);

void	mh_menus_request_launch(struct mh_menus *menus);

void	mh_menus_single_input(struct mh_menu *menu, void *context);

void	mh_menus_single_leave(struct mh_menu *menu, void *context);

void	mh_menus_ghost_init(struct mh_menu *menu, void *context);

void	mh_menus_ghost_draw(struct mh_menu *menu, void *context);

void	mh_menus_ghost_input(struct mh_menu *menu, void *context);

void	mh_menus_race_init(struct mh_menu *menu, void *context);

void	mh_menus_race_draw(struct mh_menu *menu, void *context);

void	mh_menus_race_input(struct mh_menu *menu, void *context);

void	mh_menus_race_leave(struct mh_menu *menu, void *context);

void	mh_menus_car_init(struct mh_menu *menu, void *context);

void	mh_menus_car_draw(struct mh_menu *menu, void *context);

void	mh_menus_car_input(struct mh_menu *menu, void *context);

void	mh_menus_car_leave(struct mh_menu *menu, void *context);

void	mh_menus_dialog_step(struct mh_menu *menu, void *context);

void	mh_menus_exit_input(struct mh_menu *menu, void *context);

void	mh_menus_launch_input(struct mh_menu *menu, void *context);

void	mh_menus_main_input(struct mh_menu *menu, void *context);

int	mh_menus_special_cars_available(
	const struct mh_resource_source *resources, const char *disc_root);

void	mh_menus_publish_settings(const struct mh_menus *menus,
	struct mh_settings *settings);

/* Queue one native address connection through the normal lobby launch. */
enum mh_error_code	mh_menus_multiplayer_connect(struct mh_menus *menus,
	const char *address, unsigned int port, struct mh_error *error_out);

enum mh_error_code	mh_menus_multiplayer_open(struct mh_menus *menus,
	const char *asset_root, struct mh_settings *settings,
	const struct mh_session_options *options, int *quit_requested,
	struct mh_activity *output, struct mh_error *error_out);

/* Borrowed arguments remain live until launch_close or terminal step. */
void	mh_menus_launch_open(struct mh_menus *menus, struct mh_window *window,
	const char *asset_root, const char *disc_root,
	struct mh_settings *settings, struct mh_settings_store *store,
	const struct mh_session_options *options, int audio_mode);

enum mh_app_status	mh_menus_launch_step(struct mh_menus *menus,
	unsigned long *delay_ms);
void			mh_menus_launch_close(struct mh_menus *menus);

extern const unsigned int mh_menus_league_destinations[MH_MENUS_LEAGUE_COUNT];

/* Inspection output; neither operation changes menu state. */
enum mh_error_code	mh_menus_save_indexed(const char *path,
	const struct mh_menus *menus, const struct mh_user_data *data,
	struct mh_error *error_out);
void			mh_menus_print_summary(const struct mh_menus *menus,
	const struct mh_settings *preferences);

/* Copy each frame's sampled input. */
void	mh_menus_set_input(struct mh_menus *menus,
	const struct mh_keyboard *keyboard, const unsigned char *down);

void	mh_menus_configuration_bind(struct mh_menus *menus);
int	mh_menus_mode_enabled(const struct mh_menus *menus, unsigned int mode);
int	mh_menus_page_enabled(const struct mh_menus *menus,
	const struct mh_menu_page *page);
void	mh_menus_request(struct mh_menu *menu, struct mh_menu_page *page);
void	mh_menus_restrict_mode(struct mh_menus *menus);

#endif
