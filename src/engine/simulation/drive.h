#ifndef MH_DRIVE_H
#define MH_DRIVE_H
#include "engine/core/error.h"
#include "engine/game/cheats.h"
#include "engine/simulation/ai_personality.h"
#include "engine/simulation/body_effects.h"
#include "engine/simulation/camera.h"
#include "engine/simulation/frame_time.h"
#include "engine/simulation/impact.h"
#include "engine/simulation/opponent.h"
#include "engine/simulation/placement.h"
#include "engine/simulation/race.h"
#include "engine/simulation/recovery.h"
#include "engine/simulation/wheels.h"

#define MH_DRIVE_DT (1.0 / 120.0)

enum mh_start_phase {
	MH_START_READY,
	MH_START_THREE,
	MH_START_TWO,
	MH_START_ONE,
	MH_START_GO,
	MH_START_RACING,
	/* No presentation cue, also before GO. */
	MH_START_NONE = MH_START_RACING
};

/*
 * Last frame's recovery inputs, retained for the existing simulation trace.
 * A reset otherwise erases the feedback needed to explain its decision.
 */
struct mh_drive_recovery_sample {
	struct mh_recovery_state	before;
	struct mh_recovery_feedback	feedback;
	double				entry_position[3];

	/* Zero when no recovery evaluation occurred this frame. */
	float				elapsed;

	/* Height reset preceded these timeout-predicate inputs. */
	int				fell;
};

/*
 * Frame clock. The explicit fixed-step inspection entry points retain their
 * diagnostic clock until an advance call activates this one.
 */
struct mh_drive_frame {
	int			active;

	/* Native fixed-tick clock; zero retains original variable frames. */
	unsigned int		fixed_rate;
	unsigned long		fixed_ticks;
	double			fixed_epoch;

	/*
	 * Clock sampled before restart; next advance consumes it with
	 * elapsed=0.
	 */
	int			pending;
	double			seconds;
	unsigned long		intro_ms;
	struct mh_frame_time	time;

	/* Current already-selected physical interval. */
	float			step;
};

/*
 * Native session policy around the vehicle tick. Borrowed assets must remain
 * alive. No SDL, wall clock, rendering API or owned allocations.
 */
struct mh_drive {
	/* Session flags; field setup applies to every entrant. */
	unsigned int			cheats;
	const struct mh_car		*car;
	const struct mh_collision	*col;
	const struct mh_materials	*materials;
	const struct mh_route		*route;
	const struct mh_track		*track;
	struct mh_engine_config		config;
	struct mh_suspension_config	suspension;

	/* Vehicle/contact state and retained recovery feedback. */
	struct mh_vehicle_state		vehicle;
	struct mh_impact_effects	impact_effects;

	/* Contact feedback accumulated within the latest frame. */
	struct mh_body_effects		body_effects;

	/* A main track impact, not residual contact alone. */
	int				feedback_track_hit;

	/* Published impulse; reset independently of the accumulator. */
	double				body_impulse;

	/* Spark publication, from local simulation or a received packet. */
	struct mh_body_sparks		body_sparks;
	struct mh_recovery_state	recovery;

	/* Automatic relocation at the latest frame end. */
	enum mh_recovery_reason		recovery_event;
	struct mh_drive_recovery_sample	recovery_sample;

	/* Untimed if track Laps is absent. */
	struct mh_race			race;

	/* Host elimination freezes progress, not vehicle motion. */
	int				race_frozen;

	/* NULL for human controls. Borrowed. */
	const struct mh_ai_path		*ai_path;
	struct mh_opponent_config	opponent;
	struct mh_ai_driver_result	ai;
	int				ai_personality_enabled, ai_boosted;
	float				ai_parameters[3];
	struct mh_ai_personality	ai_personality;
	unsigned int			ai_difficulty;

	/* Diagnostic counters and sampled frame clock. */
	unsigned long			race_ticks;
	int				countdown;

	/* Saturates after the two-second GO display. */
	unsigned int			start_ticks;
	unsigned int			slot;
	unsigned long			ticks;
	double				accumulator, seconds;
	struct mh_drive_frame		frame;

	/* Camera and wheel presentation retained between frames. */
	double				eye[3], target[3], forward[3];
	struct mh_camera_pose		camera;

	/* Borrowed track asset, optional. */
	const struct mh_camera_motion	*camera_motion;

	/* Borrowed raw-frame LWMO. */
	const struct mh_scene_object	*start_camera[2];

	/* Original opening latch/preference. */
	int				camera_intro, camera_saved_in_car;

	/* Last evaluated F3 lens. */
	float				camera_focal, camera_far;

	/* Pause-menu ViewDistance percentage; zero defaults to 100. */
	float				view_percent;

	/* Automatic finish presentation; preserves lens and listener. */
	int				camera_goal;

	/* Requested view; takes effect on camera update. */
	enum mh_camera_mode		camera_mode;

	/* Held request, temporarily suppressed during inversion fallback. */
	int				look_back;

	/* Whether the last published camera pose was a rear view. */
	int				camera_rear_view;
	struct mh_camera_roll		camera_roll;
	struct mh_wheel_state		wheels;

	/* Network playback: no local forces or recovery. */
	int				remote;

	/* Raw visual channels, separate from physics bounds. */
	double				remote_extension[4];

	/* Last effective drive pedal; presentation only. */
	double				sound_pedal;

	/* Post-engine pedals retained between frame slices. */
	double				input_throttle, input_brake;

	/* Held horn request, snapshot flag bit 1. */
	int				horn;
};

enum mh_error_code	mh_drive_start(const struct mh_car *car,
	const struct mh_collision *col, const struct mh_materials *materials,
	const struct mh_route *route, const struct mh_track *track,
	unsigned int slot, int manual, int countdown, struct mh_drive *output,
	struct mh_error *error_out);

/*
 * Original sampled countdown cues and strict GO boundary. Explicit diagnostic
 * ticks retain the old 120 Hz clock until a frame advance activates sampling.
 */
enum mh_start_phase	mh_drive_start_phase(const struct mh_drive *drive);
int			mh_drive_racing(const struct mh_drive *drive);
double			mh_drive_intro_ms(const struct mh_drive *drive);

/* Leave seconds*1000 in extended precision for deadline consumers. */
long double mh_drive_clock_ms(const struct mh_drive *drive);

/*
 * Preview the next clock without advancing state, so input and physics use
 * the same sampled interval/GO. elapsed retains the extended counter quotient
 * until the binary64 clock addition. Failure leaves output unchanged.
 */
enum mh_error_code	mh_drive_frame_sample(const struct mh_drive *drive,
	long double elapsed, struct mh_drive_frame *output,
	struct mh_error *error_out);

/*
 * Select one autonomous car, configure its original difficulty and restart
 * at the grid. League personality is configured separately by ai_profile.
 */
enum mh_error_code	mh_drive_enable_ai(struct mh_drive *drive,
	const struct mh_ai_path *path, unsigned int difficulty,
	struct mh_error *error_out);

/*
 * grid=0 recovers and re-anchors race progress without credit; grid=1 returns
 * to the selected slot and restarts countdown/race. Scenery time is retained.
 */
enum mh_error_code	mh_drive_reset(struct mh_drive *drive, int grid,
	struct mh_error *error_out);

/*
 * Diagnostic 1/120 physics/race tick, then frame recovery and presentation.
 * Rejects sessions already using frame sampling; failure preserves drive.
 */
enum mh_error_code	mh_drive_tick(struct mh_drive *drive,
	const struct mh_engine_input *controls, struct mh_error *error_out);

/* Remote presentation/race tick only, after network sample application. */
enum mh_error_code	mh_drive_remote_tick(struct mh_drive *drive,
	struct mh_error *error_out);

/*
 * Optional traffic decision; frame recovery follows its integration.
 * Fields produce controls in retained roster order before shared physics.
 */
enum mh_error_code	mh_drive_tick_traffic(struct mh_drive *drive,
	const struct mh_engine_input *controls,
	const struct mh_ai_traffic_result *traffic, struct mh_error *error_out);

/*
 * Sample original clock, clamp physics to 1s and run at most ten .04s slices.
 * Race time uses the supplied elapsed interval; no cross-frame backlog.
 * MENU skips physics but retains clock/presentation/progress. SUSPEND stops
 * the entire frame. Caller selects elapsed using the entry menu flag.
 * Engine pedal writes carry between slices; each new frame samples controls.
 * Failure leaves drive and steps unchanged. Successful steps may be zero.
 */
enum mh_error_code	mh_drive_advance(struct mh_drive *drive,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out);

/*
 * Apply a view using the inversion state already published by the enclosing
 * frame API. Does not advance per-car inversion timers. Failure is atomic.
 */
enum mh_error_code	mh_drive_camera_update(struct mh_drive *drive,
	float elapsed, struct mh_error *error_out);

/* Effective presentation flags after the original inversion fallback. */
int	mh_drive_camera_in_car(const struct mh_drive *drive);
int	mh_drive_camera_look_back(const struct mh_drive *drive);

/*
 * Original far clamp after the current TRK ViewDistance percentage.
 * Missing optional distance uses 300 as a native fallback.
 */
float			mh_drive_camera_far(const struct mh_drive *drive);
enum mh_error_code	mh_drive_view_distance(struct mh_drive *drive,
	float percent, struct mh_error *error_out);

/* Enable fresh-session league ADP behavior on an already configured AI. */
enum mh_error_code	mh_drive_ai_profile(struct mh_drive *drive,
	float aggressiveness, float eagerness, struct mh_error *error_out);
#endif
