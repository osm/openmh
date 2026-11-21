#ifndef MH_CLIENT_RACE_H
#define MH_CLIENT_RACE_H

#include <stdio.h>

#include "client/application/status.h"
#include "client/diagnostics/options.h"
#include "client/session/race_context.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"
#include "engine/input/chat.h"

struct mh_retro_source;
struct mh_game_timer;
struct mh_user_data;
struct mh_loading_view;

struct mh_race_results_handler {
	void			*context;

	/*
	 * Called on Enter/Escape after local results are finalized (or directly
	 * for Time Attack, whose original ordinary board is skipped). Return
	 * MH_ERROR_NONE only after persistence succeeds. Errors leave results
	 * open for retry.
	 */
	enum mh_error_code (*commit)(void *context,
		const struct mh_standings *standings,
		struct mh_error	*error_out);
};

/*
 * Main-thread session. advance owns simulation and runs once per fixed tick
 * (once per frame for legacy playback). service runs once per outer iteration,
 * including iterations with no tick, for real-time network maintenance. The
 * counter interval stays long double through the clock accumulation; converting
 * it to double early changes original binary64 clock rounding.
 *
 * By default focus loss/results suppress input only (network sessions).
 * pause_with_ui uses the original local menu policy: entry visibility gates
 * elapsed time, updated visibility gates physics. Focus/results suspension
 * passes MH_FRAME_SUSPEND; zero elapsed alone does not suspend a frame.
 *
 * done exits normally (e.g. host returned to lobby); errors end the run.
 * restart handles menu restart/return; hud may alter a presentation snapshot.
 * The field/asset addresses must remain stable throughout the run.
 */
struct mh_race_session_hooks {
	/*
	 * Host presents pause/results controls instead of the original menus.
	 */
	int				external_ui;
	void				*context;

	/* Local suspension policy; zero keeps network frames running. */
	int				pause_with_ui;

	/* Unpaced track-camera playback; completion belongs to the session. */
	int				benchmark;

	/* Open the results view on entry. */
	int				initial_results;

	/* Original pause menu labels: Abort race versus Retire race. */
	int				network_host;

	/* Local League handoff, with League pause/result routing. */
	int				league;
	enum mh_error_code (*service)(
		void	*context, int *done, struct mh_error *error_out);
	enum mh_error_code (*advance)(void *context,
		const struct mh_engine_input *controls, long double elapsed,
		enum mh_frame_action action, unsigned int *steps, int *done,
		struct mh_error		*error_out);
	enum mh_error_code (*restart)(
		void			*context, struct mh_error *error_out);
	void	(*hud)(void *context, struct mh_hud_state *state);

	/*
	 * Received client rank for the sign; fallback is the pre-progress rank.
	 */
	unsigned int	(*finish_position)(void *context, unsigned int fallback);

	/*
	 * Add a local notice to the session's existing history and clock. NULL
	 * uses the renderer's local history (for sessions without a producer).
	 */
	enum mh_error_code (*message)(void *context, const char *text,
		const char *suffix, unsigned long color,
		struct mh_error		*error_out);

	/*
	 * Network race chat: persistent draft history and current race
	 * identity. send_chat publishes and records the complete name-prefixed
	 * line using the current frame's sampled game milliseconds. This local
	 * timestamp is not transmitted and must not come from the transport
	 * clock.
	 */
	struct mh_chat			*chat;
	const struct mh_chat_config	*chat_config;
	enum mh_error_code (*send_chat)(void *context, const char *text,
		unsigned long color, unsigned long milliseconds,
		struct mh_error		*error_out);
};

/*
 * Frame-driven race lifetime. Assets, field, option pointers and their
 * referenced data are borrowed until close, including output slots. The
 * caller initializes completed/quit/attract_exit to zero and music to NULL.
 * Only one race may be open: hidden-effect history is process-owned.
 * A failed open leaves output unchanged. Initialization may already update
 * borrowed scene/settings state; this is not a rollback of game state.
 * With loading confirmation enabled, initialization completes during step;
 * the race clock remains stopped until the player continues.
 */
struct mh_race_loop_options {
	struct mh_debug_options			debug;

	/*
	 * Optional loading view, retained while waiting for confirmation. Its
	 * window is transferred with its ownership flag at window setup, even
	 * if later setup fails. A borrowed client window remains client-owned.
	 * The caller releases its view reference after open returns in either
	 * case.
	 */
	struct mh_loading_view			*loading;
	int					wait_for_loading;

	/* Renderer descriptors/configurations borrowed through teardown. */
	const struct mh_render_set		*renderers;

	/* Window system borrowed through child/window teardown. */
	const struct mh_window_system		*windows;

	/*
	 * Optional client window, borrowed until close. Only the supplied
	 * loading view may have a renderer attached on entry.
	 */
	struct mh_window			*window;
	const struct mh_input_provider		*inputs;

	/* Optional effect source, borrowed through initialization. */
	const struct mh_retro_source		*retro_source;

	/*
	 * Selected decoder/output/scheduler, borrowed through audio teardown.
	 * NULL output provider disables audio.
	 */
	const struct mh_audio_decoder_provider	*audio_decoder;
	const struct mh_audio_provider		*audio_provider;
	unsigned int				audio_trace;

	/* Optional screenshot storage, borrowed until close; NULL disables. */
	const struct mh_user_data		*user_data;

	/* Optional encoder, borrowed through screenshot teardown. */
	const struct mh_image_encoder		*screenshot_encoder;

	/* NULL selects the view's default Screenshots directory. */
	const char				*screenshot_directory;

	/*
	 * Required, borrowed through close. Its source must also outlive any
	 * results audio transferred to the caller.
	 */
	struct mh_game_timer			*timer;
	const char				*capture;
	int					headless;
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
};

struct mh_race_loop;

/* Set by the Club TBL HUD, retained when returning to menus. */
int	mh_race_club_credits(void);

enum mh_error_code	mh_race_loop_leave(struct mh_race_loop *loop,
	struct mh_error *error_out);

/* Failed-open diagnostics remain valid until the next open on this thread. */
enum mh_error_code	mh_race_loop_open(struct mh_race_context *context,
	struct mh_field *field, const struct mh_race_loop_options *options,
	struct mh_race_loop **output, struct mh_error *error_out);

/* Advances at most one frame; terminal states remain terminal until close. */
enum mh_app_status	mh_race_loop_step(struct mh_race_loop *loop);

/* Terminal failure, borrowed until close. Copy before releasing the loop. */
enum mh_error_code	mh_race_loop_error(const struct mh_race_loop *loop,
	struct mh_error *error_out);

/* Native pacing hint from the last step; stepping itself never sleeps. */
unsigned long	mh_race_loop_delay(const struct mh_race_loop *loop);

/* Release resources and transfer accepted results music, returning success. */
int	mh_race_loop_close(struct mh_race_loop *loop);

#endif
