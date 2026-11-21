#ifndef MH_CLIENT_SESSION_OPTIONS_H
#define MH_CLIENT_SESSION_OPTIONS_H

#include "client/diagnostics/options.h"
#include "engine/network/protocol.h"

struct mh_retro_source;
struct mh_horn_source;

struct mh_env_sound_source;
struct mh_material_source;
struct mh_catalog_source;
struct mh_profile_source;
struct mh_palette_source;
struct mh_menu_layout_source;
struct mh_navigation_source;
struct mh_animation_source;
struct mh_atlas_source;

struct mh_model_decoder;
struct mh_world_decoder;
struct mh_collision_source;

struct mh_control_binding_set;
struct mh_highscore_history;
struct mh_game_timer;
struct mh_resource_source;
struct mh_user_data;
struct mh_image_capture;

struct mh_image_source_provider;
struct mh_font_source_provider;
struct mh_wireframe_source;
struct mh_sprite_source_provider;

/* Shared launch options; pointed-to data remains borrowed until close. */
struct mh_session_options {
	/* Optional external frontend; borrowed through session close. */
	struct mh_single_control		*single_control;
	const struct mh_race_load_observer	*load_observer;

	/* Native race timing; zero selects the default, never variable dt. */
	unsigned int				simulation_rate;

	/* Explicit original multiplayer protocol and variable-frame timing. */
	int					network_legacy;
	struct mh_debug_options			debug;
	const struct mh_image_source_provider	*image_source;
	const struct mh_font_source_provider	*font_source;
	const struct mh_wireframe_source	*wireframe_source;
	const struct mh_sprite_source_provider	*sprite_source;
	const struct mh_model_decoder		*model_decoder;
	const struct mh_world_decoder		*world_decoder;
	const struct mh_atlas_source		*atlas_source;
	const struct mh_animation_source	*animation_source;
	const struct mh_palette_source		*palette_source;
	const struct mh_menu_layout_source	*layout_source;
	const struct mh_retro_source		*retro_source;
	const struct mh_profile_source		*profile_source;
	const struct mh_catalog_source		*catalog_source;
	const struct mh_material_source		*material_source;
	const struct mh_horn_source		*horn_source;
	const struct mh_env_sound_source	*env_sound_source;
	const struct mh_navigation_source	*navigation_source;
	const struct mh_collision_source	*collision_source;
	const struct mh_image_decoder_provider	*image_decoder;
	const struct mh_image_decoder_provider	*thumbnail_decoder;
	const struct mh_image_decoder_provider	*portrait_decoder;

	/* Optional diagnostic capture; borrowed through child teardown. */
	const struct mh_image_capture		*capture;

	/* Optional screenshot storage; usually the same view as user_data. */
	const struct mh_user_data		*screenshot_data;

	/* Renderer descriptors/configurations borrowed through teardown. */
	const struct mh_render_set		*renderers;
	const struct mh_canvas_provider		*canvases;

	/* Window system borrowed through child/window teardown. */
	const struct mh_window_system		*windows;

	/*
	 * Optional client window, borrowed through every child activity.
	 * The caller retires its canvas/renderer before launching a session.
	 */
	struct mh_window			*window;
	const struct mh_input_provider		*inputs;

	/*
	 * Selected decoder/output/scheduler, borrowed through audio teardown.
	 * NULL output provider disables audio.
	 */
	const struct mh_audio_decoder_provider	*audio_decoder;
	const struct mh_audio_provider		*audio_provider;
	unsigned int				audio_trace;

	/* Optional video provider; borrowed through session close. */
	const struct mh_video_provider		*videos;

	/* Optional transport, borrowed through discovery and session close. */
	const struct mh_transport_provider	*network;
	enum mh_net_wire			network_wire;
	int					network_trace;

	/* Optional clipboard capability; borrowed through child teardown. */
	const struct mh_clipboard_provider	*clipboard;

	/* Required host resources and timer, borrowed until close. */
	const struct mh_resource_source		*resources;
	struct mh_game_timer			*timer;

	/*
	 * Required for enabled record/recording saving; borrowed
	 * until session close and until any returned private recording take is
	 * closed.
	 */
	const struct mh_user_data		*user_data;

	/*
	 * NULL record_directory selects the view's default root.
	 * NULL recording_directory selects the view's default Recordings
	 * directory.
	 */
	const char	*record_directory, *recording_directory;

	/* Optional encoder, borrowed through screenshot teardown. */
	const struct mh_image_encoder		*screenshot_encoder;

	/* NULL selects the view's default Screenshots directory. */
	const char				*screenshot_directory;

	/* NULL selects extracted GAME_ROOT/Music. */
	const char				*music_root;

	/*
	 * Optional course_music_count CDTrack choices, indexed by
	 * installed course number minus one. NULL retains each TRK value.
	 */
	const unsigned int			*course_music;
	unsigned int				course_music_count;

	/* Optional applied menu profile. */
	const struct mh_control_binding_set	*controls;

	/* Optional process/session history retained across race assets. */
	struct mh_highscore_history		*score_history;

	/* Original catalog N/M selector, 0/1. */
	unsigned int				record_class;
	int					no_records;

	/* Let the player read the loaded course screen before local races. */
	int					wait_for_loading;
};

#endif
