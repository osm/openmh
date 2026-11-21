#ifndef MH_CLIENT_RACE_INTERNAL_H
#define MH_CLIENT_RACE_INTERNAL_H

#include "client/race/race.h"
#include "client/screenshots/screenshot.h"
#include "engine/audio/race_playback.h"
#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/binding.h"
#include "engine/input/control_type.h"
#include "engine/input/drive_controls.h"
#include "engine/input/keyboard.h"
#include "engine/input/tick_input.h"
#include "engine/input/view_controls.h"
#include "engine/render/scene_gpu.h"
#include "engine/render/scene_matrices.h"
#include "engine/render/sky.h"
#include "engine/simulation/feedback.h"
#include "engine/simulation/tick_clock.h"
#include "engine/ui/attract.h"
#include "engine/ui/chat_draw.h"
#include "engine/ui/pause.h"
#include "engine/ui/software_hud.h"
#include "engine/ui/tron_draw.h"
#include "interfaces/clock/clock.h"
#include "interfaces/input/input.h"
#include "interfaces/render/renderer.h"
#include "interfaces/window/window.h"

/* Process history is borrowed by the one active loop. */
struct mh_race_history {

	struct mh_hud_secrets		hud;
	struct mh_software_hud_secrets	software_hud;
	struct mh_retro_state		retro;
	struct mh_feedback_state	feedback;
	int				feedback_initialized;
};

/* Only actions through the original pause-menu slot are sampled here. */
#define MH_RACE_ACTION_COUNT (MH_CONTROL_GAME_MENU + 1)

enum {
	/*
	 * Matches renderer-owned diagnostics without importing its private
	 * type.
	 */
	MH_RACE_ERROR_SIZE = 512,
	MH_RACE_RENDER_MESHES =
		MH_FIELD_MESHES + MH_SCENE_GPU_EFFECT_MESHES + MH_FINISH_SIGNS
};

struct mh_race_loop {
	struct mh_field				previous_field, presented_field;
	struct mh_field				*authoritative_field;
	int					presentation_history;
	struct mh_tick_clock			tick_clock;
	struct mh_tick_input			tick_input;
	int					in_tick, tick_restarted;
	struct mh_debug_options			debug;

	/* Borrowed during initialize; cleared at window handoff. */
	struct mh_loading_view			*loading;

	/*
	 * Retained during the optional ready prompt, before race
	 * initialization.
	 */
	struct mh_loading_view			*loading_wait;
	int					initialized;

	/* Renderer descriptors/configurations borrowed through teardown. */
	const struct mh_render_set		*renderers;

	/* Window system borrowed through child/window teardown. */
	const struct mh_window_system		*windows;
	const struct mh_input_provider		*inputs;

	/* Optional effect source, borrowed through initialization. */
	const struct mh_retro_source		*retro_source;

	/*
	 * Optional selected output/scheduler, borrowed through audio teardown.
	 */
	const struct mh_audio_decoder_provider	*audio_decoder;
	const struct mh_audio_provider		*audio_provider;
	unsigned int				audio_trace;
	struct mh_game_timer			*timer;
	struct mh_race_history			*history;
	struct mh_race_context			*context;
	struct mh_field				*field;
	const char				*capture;
	int					headless;
	long double				fps_elapsed;
	unsigned long				fps_frames;
	unsigned int				fps;
	int					results;
	const char				*root;
	const struct mh_audio_options		*audio_options;
	FILE					*trace;
	const struct mh_race_session_hooks	*session;
	const struct mh_race_results_handler	*result_handler;
	int					*completed;
	int					*quit;
	struct mh_audio				**music;
	int					*attract_exit;
	const struct mh_mesh			*meshes[MH_RACE_RENDER_MESHES];
	unsigned int				finish_mesh, finish_position;

	/* Zero when the content has no finish-sign presentation pack. */
	unsigned int				finish_count;
	int					finish_seen;
	float					finish_seconds;

	struct mh_sky_state			sky_state;
	struct mh_weather_camera		weather_camera;
	struct mh_water_frame			water_frame;
	const struct mh_weather_style		*weather;
	struct mh_brightness			previous_brightness;

	struct mh_drive				*drive, *viewed;
	enum mh_camera_mode			camera_preference;
	struct mh_engine_input			controls;
	struct mh_drive_frame			input_frame;
	struct mh_attract_frame			attract_frame;
	unsigned long				attract_phase;
	struct mh_hud_state			hud;
	struct mh_message_view			messages;
	struct mh_messages			local_messages;
	int	local_messages_present, screenshot_failed;

	struct mh_screenshot_capture		screenshot;
	struct mh_screenshot_result		screenshot_result;

	struct mh_results_screen		screen;
	struct mh_pause_state			menu;
	struct mh_pause_settings		menu_settings;
	struct mh_pause_input			menu_input;
	struct mh_pause_output			menu_output;

	struct mh_keyboard			keyboard;
	unsigned long				input_clock, screenshot_clock;
	struct mh_binding_input			binding_input;
	struct mh_control_binding_set		chat_defaults;
	struct mh_chat_output			chat_output;
	unsigned char				characters[MH_KEY_COUNT];
	int	chat_enabled, chat_block, control_racing;
	struct mh_drive_controls		shaped_controls;
	unsigned int	actions[MH_RACE_ACTION_COUNT], info_held;
	float					control_seconds;
	unsigned char				down[MH_KEY_COUNT];
	struct mh_audio				*audio;
	struct mh_audio_options			effective_audio;

	struct mh_ui_list			ui;
	struct mh_tron_intro			tron_intro;
	struct mh_retro_state			retro_before;
	struct mh_projection			scene_projection;
	struct mh_camera_lens			scene_lens;
	struct mh_particle_camera		particle_camera;
	struct mh_camera_pose	follow_previous, follow_car, follow_result;
	float					follow_position[3];
	double					follow_p[3], follow_velocity[3];
	struct mh_window			*window;
	int					owns_window;
	struct mh_renderer			*renderer;
	struct mh_event				event;
	struct mh_input				devices;
	struct mh_device_input			device_input;
	unsigned long				feedback_generation;
	int	feedback_active, feedback_failed;
	struct mh_view_controls			view_controls;
	unsigned long				input_mode;

	struct mh_clock_counter			previous, now;
	struct mh_clock_counter previous_counter, current_counter,
		frequency_counter;
	unsigned int	steps, mesh_count, map_mode, gap_mode;
	unsigned int	names_mode, width, height, visible_roots;
	float	models[(MH_RACE_RENDER_MESHES) * 16], matrix[16], view[16];
	struct mh_fog				fog;
	long double				elapsed, wall_elapsed;
	struct mh_frame_request			frame_request;
	double					view_clock;
	float	effect_dt, tron_before, water_before;
	unsigned long				weather_random;

	int running, success, paused, focused, reflections, results_shown,
		was_results;
	int done, live, continuous, save_failed, glenz_loaded, debug_glenz,
		debug_fixed, debug_trace, follow_active, tron, retro;
	int	entry_menu, frame_updated, debug_hidden;
	int was_menu, brightness_changed, dismissed_results, results_ready,
		accept_results;

	struct mh_error				error;

	/*
	 * Retains diagnostics across steps and partial presentation teardown.
	 */
	char					error_text[MH_RACE_ERROR_SIZE];

	int					process_clock;
	unsigned long				delay;
	int					pace_started;
	long double				pace_previous, pace_deadline;
};

/*
 * Frame operations return zero to stop this step, including normal exits.
 * running/success distinguish completion from failure. Keep call order.
 * These are private application operations, not backend contracts.
 */
enum mh_error_code	mh_race_restart_clock(
	const struct mh_clock_source *clock, struct mh_field *field,
	struct mh_clock_counter *previous, struct mh_error *error_out);

int			mh_race_prepare_hud(struct mh_race_loop *loop);
enum mh_error_code	mh_race_load_finish(struct mh_race_loop *loop,
	struct mh_error *error_out);
int			mh_race_prepare_finish(struct mh_race_loop *loop);

int	mh_race_build_overlay(struct mh_race_loop *loop);
int	mh_race_build_software_overlay(struct mh_race_loop *loop);

void	mh_race_frame_rate_sample(struct mh_race_loop *loop);
int	mh_race_frame_rate_draw(struct mh_race_loop *loop);

int	mh_race_pace_frame(struct mh_race_loop *loop);
int	mh_race_sample_frame_clock(struct mh_race_loop *loop);

void	mh_race_feedback_frame(struct mh_race_loop *loop);
void	mh_race_feedback_stop(struct mh_race_loop *loop);

int	mh_race_poll_window_events(struct mh_race_loop *loop);

int	mh_race_poll_input(struct mh_race_loop *loop);

int	mh_race_sample_input_time(struct mh_race_loop *loop);

int	mh_race_update_chat(struct mh_race_loop *loop);

int	mh_race_update_finish(struct mh_race_loop *loop);

int	mh_race_update_attract(struct mh_race_loop *loop);

int			mh_race_read_driving_controls(
	struct mh_race_loop *loop);
void			mh_race_reset_tick_clock(struct mh_race_loop *loop);
enum mh_error_code	mh_race_queue_tick_input(struct mh_race_loop *loop,
	struct mh_error *error_out);
enum mh_error_code	mh_race_read_tick_input(struct mh_race_loop *loop,
	struct mh_error *error_out);

int	mh_race_apply_view_controls(struct mh_race_loop *loop);

int	mh_race_update_pause_menu(struct mh_race_loop *loop);

int	mh_race_accept_results(struct mh_race_loop *loop);

int	mh_race_sample_simulation_time(struct mh_race_loop *loop);

void	mh_race_restore_camera(struct mh_race_loop *loop);
int	mh_race_remember_chase_camera(struct mh_race_loop *loop);

int	mh_race_advance_simulation(struct mh_race_loop *loop);

int	mh_race_update_tyre_effects(struct mh_race_loop *loop);

int	mh_race_update_chase_camera(struct mh_race_loop *loop);

void	mh_race_trace_camera(struct mh_race_loop *loop);

int	mh_race_update_results(struct mh_race_loop *loop);

int	mh_race_animate_scene(struct mh_race_loop *loop);

/* Failed setup releases the renderer; diagnostics remain valid with loop. */
enum mh_error_code	mh_race_open_software(struct mh_race_loop *loop,
	struct mh_error *error_out);
int			mh_race_draw_software_scene(struct mh_race_loop *loop);

int	mh_race_draw_frame(struct mh_race_loop *loop);

void	mh_race_update_window_title(struct mh_race_loop *loop);

int	mh_race_initialize(struct mh_race_loop *loop);

enum mh_error_code	mh_race_startup_open(struct mh_race_loop *loop,
	int wait_for_loading, struct mh_error *error_out);
enum mh_app_status	mh_race_startup_step(struct mh_race_loop *loop);

#endif
