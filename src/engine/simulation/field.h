#ifndef MH_FIELD_H
#define MH_FIELD_H

#include "engine/assets/records.h"
#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/game/result_prepare.h"
#include "engine/game/standings.h"
#include "engine/simulation/ai_roster.h"
#include "engine/simulation/drive.h"
#include "engine/simulation/object.h"
#include "engine/simulation/race_tuning.h"

/* Native limit; original tracks use at most 49. */
#define MH_FIELD_OBJECTS 64

enum mh_race_notice {
	MH_RACE_NOTICE_NONE,
	MH_RACE_NOTICE_WIN,
	MH_RACE_NOTICE_OUT
};

struct mh_field_tuning {
	/* Slice count since Home; clock source only for diagnostics. */
	unsigned long		ticks;
	int			initialized, catchup;

	/* Selected TRK AI[10], before percent conversion. */
	float			strength;
	struct mh_race_tuning	curve;

	/* Saved before the first adjustment. */
	struct mh_engine_config	base[MH_RACE_TUNING_CARS];
};

struct mh_field_object_source {
	const struct mh_object_body	*body;
	const struct mh_collision	*hull;
	const struct mh_scene_object	*scene;
	size_t				scene_index;
};

struct mh_field_object {
	/* Borrowed immutable assets. */
	struct mh_field_object_source	source;
	struct mh_object_state		state;
};

/*
 * Shared frame timing, retained AI traffic history and individual car/race
 * states. Car zero owns human controls; viewed_entry selects presentation.
 * Assets are borrowed, with no pointers into this struct's own storage.
 */
struct mh_field {
	struct mh_drive			cars[MH_RACE_MAX_CARS];

	/*
	 * Shared stream supplied by presentation; owned value for atomic ticks.
	 */
	unsigned long			ai_random;

	/* count=0 for an untimed custom session. */
	struct mh_standings		standings;
	struct mh_field_tuning		tuning;
	struct mh_ai_traffic_result	traffic[MH_RACE_MAX_CARS];
	struct mh_ai_roster		ai_roster;
	size_t				point[MH_RACE_MAX_CARS];
	double				lateral_rate[MH_RACE_MAX_CARS];
	const struct mh_collision	*hulls[MH_RACE_MAX_CARS];

	/*
	 * Survives restart/recovery; previous slice until that peer finishes
	 * its turn, then that turn's successful corner casts.
	 */
	struct mh_contact		body_contacts[MH_RACE_MAX_CARS][MH_CAR_BODY_POINTS];

	/*
	 * Selected local car's last A-side pair impulse; retained across
	 * frames.
	 */
	float				feedback_pair[3];

	/* Car/car + car/scenery hits this tick. */
	unsigned int			contact_count[MH_RACE_MAX_CARS];

	/* Scenery subset for diagnostics. */
	unsigned int			object_contact_count[MH_RACE_MAX_CARS];

	/* Largest absolute impulse. */
	double				contact_impulse[MH_RACE_MAX_CARS];
	const struct mh_ai_path		*path;
	unsigned int			count;

	/* Per-car listener selection; restart may retain several owners. */
	unsigned int			listener_mask;

	/* Explicit view commands, including repeated selection. */
	unsigned long			listener_revision;

	/* Presentation selection; controls remain entry zero. */
	unsigned int			viewed_entry;

	/* Current membership; departed entries stop presentation/progress. */
	unsigned int			active_mask;

	/* Current availability, including newly received network state. */
	unsigned int			unavailable_mask;

	/* Eligibility retained before network status refresh. */
	unsigned int			physics_mask;
	int				physics_selection_valid;

	/* Car zero local; every other entry is received playback. */
	int				network;

	/* MH_GAME_DEMO or MH_GAME_GHOST during playback, zero otherwise. */
	int				playback;

	/* Time attack has one local entrant and a record comparison HUD. */
	int				time_attack;

	/* Attract mode runs eight AI entrants without result awards. */
	int				attract;
	enum mh_race_notice		race_notice;

	/* Local received notice edge for audio. */
	unsigned long			race_notice_revision;
	double				accumulator;

	struct mh_field_object		objects[MH_FIELD_OBJECTS];
	unsigned int			object_count;

	/* Camera-selected cars, retained for the frame. */
	unsigned int			scenery_contact_mask;

	/* Fresh/reset fields sample on their first slice. */
	int				scenery_selection_valid;

	/* Shared animation clock; Home restarts it, recovery does not. */
	double				scene_seconds;

	int				records_bound;

	/* Initial eligibility, restored on a grid restart. */
	unsigned int			record_mask;

	/* Shared book survives recovery and grid restart. */
	struct mh_records		records;
};

/* Select native timing before the first fixed tick; frozen for the race. */
enum mh_error_code	mh_field_simulation_rate(struct mh_field *field,
	unsigned int rate, struct mh_error *error_out);

/*
 * Main-loop performance pass, called once BEFORE a frame's physics slices.
 * Normal/league/attract fields use the original launch adjustment and role-two
 * spin policy; network, time attack and playback skip them. Field is atomic.
 * Untimed synthetic fields have no classified ranks and skip adjustment.
 */
enum mh_error_code	mh_field_tuning_frame(struct mh_field *field,
	struct mh_error *error_out);

/*
 * Enable catch-up before the first tick of a fresh local race with a shared AI
 * path. Difficulty selects the track's catch-up strength. Catch-up is disabled
 * until explicitly enabled. Errors preserve the field.
 */
enum mh_error_code	mh_field_enable_catchup(struct mh_field *field,
	unsigned int difficulty, struct mh_error *error_out);

/*
 * Bind a loaded book to a fresh timed field. Mask selects eligible entrants:
 * ordinary/league/network local and the solo time-attack entrant = bit 0.
 * No filesystem access or borrowed pointers. Invalid bindings are atomic.
 */
enum mh_error_code	mh_field_bind_records(struct mh_field *field,
	const struct mh_records *records, unsigned int eligible_mask,
	struct mh_error *error_out);

/* Prepare separate original result rows, retaining live race state. */
enum mh_error_code	mh_field_result_estimates(const struct mh_field *field,
	unsigned long *random,
	struct mh_result_estimate output[MH_RACE_MAX_CARS],
	struct mh_error *error_out);

/*
 * Abort ranking and synthetic times use the patched preparation. Ordinary
 * local races require a packed roster with human entry zero; playback may
 * have departed rows. The field and physical state stay intact.
 * Output and RNG are unchanged on failure.
 */
enum mh_error_code	mh_field_result_estimates_abort(
	const struct mh_field *field, int aborted, unsigned long *random,
	struct mh_result_estimate output[MH_RACE_MAX_CARS],
	struct mh_error *error_out);

/*
 * Network departure: only remove participants, keeping local entry zero.
 * Reclassifies immediately even when no simulation tick is due. Atomic.
 */
enum mh_error_code	mh_field_set_active(struct mh_field *field,
	unsigned int active_mask, struct mh_error *error_out);

/*
 * Stage a viewed-car handoff without advancing physics or the camera. Keeps
 * the shared prior eye/basis/lens and request; the destination retains its
 * own inversion latch and immutable camera resource. Application binding is
 * separate from this transition. Invalid requests leave the field alone.
 */
enum mh_error_code	mh_field_select_view(struct mh_field *field,
	unsigned int entry, struct mh_error *error_out);
enum mh_error_code	mh_field_cycle_view(struct mh_field *field,
	struct mh_error *error_out);

/*
 * Before input: viewed finish or the local timer's initial finish trigger
 * selects authored GoalCamera without a sound command. Missing assets hold
 * the existing presentation. Then explicit F1-F4 may override this frame.
 */
enum mh_error_code	mh_field_camera_finish(struct mh_field *field,
	int local_trigger, struct mh_error *error_out);

/* Last inside/chase preference; trackside and attract retain it. */
enum mh_camera_mode	mh_field_camera_preference(const struct mh_field *field,
	enum mh_camera_mode previous);

/* F1/F2/F3 commands produce a listener event even if mode is unchanged. */
enum mh_error_code	mh_field_camera_mode(struct mh_field *field,
	enum mh_camera_mode mode, struct mh_error *error_out);

/* Safe presentation lookup; stale/absent selection falls back to local zero. */
unsigned int	mh_field_viewed(const struct mh_field *field);

/* Explicit sound flags are independent of automatic camera changes. */
int	mh_field_sound_detailed(const struct mh_field *field,
	unsigned int entry);
int	mh_field_listener_positional(const struct mh_field *field);

/*
 * Carry shared camera to local zero without deselecting prior sound owners.
 * Per-car inversion and borrowed resources remain separate.
 */
enum mh_error_code	mh_field_restart_listener(struct mh_field *field,
	struct mh_error *error_out);

/*
 * Apply supported original name flags to a fresh local session, including
 * AI cars and scenery. Flags are 0, MOON, IGNITION, SUPERCARS, DEMON or
 * G_RIDE, UNDERWATER, THUNDER, TRON or RETRO. Unsupported modes, combinations
 * and active/network sessions are rejected atomically.
 */
enum mh_error_code	mh_field_set_cheats(struct mh_field *field,
	unsigned int flags, struct mh_error *error_out);

/*
 * first is freshly started (no elapsed ticks); additional cars start on the
 * lowest unused slots. Single-car sessions need no path. Additional cars
 * require difficulty 1..3.
 */
enum mh_error_code	mh_field_start(const struct mh_drive *first,
	const struct mh_car *const *opponents, unsigned int count,
	const struct mh_ai_path *path, unsigned int difficulty,
	struct mh_field *output, struct mh_error *error_out);

/*
 * Explicit slots in field order, length count+1, unique in 0..63, with slot[0]
 * matching first->slot. NULL retains the lowest-unused-slot convention above.
 * Invalid slots leave output unchanged. The array is not retained.
 */
enum mh_error_code	mh_field_start_grid(const struct mh_drive *first,
	const struct mh_car *const *opponents, unsigned int count,
	const struct mh_ai_path *path, unsigned int difficulty,
	const unsigned int *slots, struct mh_field *output,
	struct mh_error *error_out);

/*
 * Enable time attack for a fresh, timed field with one local entrant in slot
 * zero. Retains its configured lap count and countdown. No filesystem access.
 */
enum mh_error_code	mh_field_enable_time_attack(struct mh_field *field,
	struct mh_error *error_out);

/*
 * Enable attract mode on a fresh eight-AI field with catch-up and records
 * unbound.
 */
enum mh_error_code	mh_field_enable_attract(struct mh_field *field,
	struct mh_error *error_out);

/*
 * Fresh explicitly placed players, local entry zero then remote entries.
 * Borrowed shared track assets; no AI or implicit slot reassignment.
 */
enum mh_error_code	mh_field_start_network(const struct mh_drive *cars,
	unsigned int count, struct mh_field *output,
	struct mh_error *error_out);

/*
 * Demo playback supplies all poses. Ghost racing has one local car followed
 * by recorded ghosts. Grid slots may coincide before frame zero.
 */
enum mh_error_code	mh_field_start_playback(const struct mh_drive *cars,
	unsigned int count, int ghost, struct mh_field *output,
	struct mh_error *error_out);

/* Refresh standings after recorded timing correction; no clocks advance. */
enum mh_error_code	mh_field_classify(struct mh_field *field,
	struct mh_error *error_out);

/*
 * Attach all decoded, borrowed car hulls atomically. Without attachment fields
 * retain traffic-only behavior for diagnostics; the race application attaches.
 */
enum mh_error_code	mh_field_set_hulls(struct mh_field *field,
	const struct mh_collision *hulls, unsigned int count,
	struct mh_error *error_out);

/*
 * Attach scenery to a fresh field after car hulls. Copies descriptors and
 * initializes sleeping bodies at their scene poses; all sources must outlive
 * the field. No internal pointers or per-tick allocations. Atomic.
 */
enum mh_error_code	mh_field_set_objects(struct mh_field *field,
	const struct mh_field_object_source *objects, unsigned int count,
	struct mh_error *error_out);
enum mh_error_code	mh_field_tick(struct mh_field *field,
	const struct mh_engine_input *controls, struct mh_error *error_out);

/*
 * First=1 starts a new frame; zero continues it. Keeps
 * transient body effects, post-engine human pedal values and camera-selected
 * scenery contact cars across the frame. A new first tick resamples controls
 * and scenery selection. Finish with mh_field_frame_update
 * once for the entire frame. mh_field_tick/advance do this automatically.
 * Grouped frames use the explicit-duration variant below so that scenery
 * receives the whole frame interval on its first slice.
 */
enum mh_error_code	mh_field_frame_tick(struct mh_field *field,
	const struct mh_engine_input *controls, int first,
	struct mh_error *error_out);

/*
 * Fixed-step diagnostic frame, rejected after frame sampling is activated. On
 * its first slice, scenery advances by frame_seconds (0<duration<=.25), halved
 * for underwater racing; later slices pass zero and retain scenery contacts.
 * Car physics still takes one native fixed step. The ordinary tick wrapper
 * supplies MH_DRIVE_DT on first=1, zero thereafter.
 */
enum mh_error_code	mh_field_frame_tick_duration(struct mh_field *field,
	const struct mh_engine_input *controls, int first, double frame_seconds,
	struct mh_error *error_out);
enum mh_error_code	mh_field_camera_update(struct mh_field *field,
	float elapsed, struct mh_error *error_out);

/*
 * Once after frame physics: advance active wheels, test local recovery with
 * final feedback and unhalved elapsed, update all active inversion states,
 * then apply the shared viewed camera.
 * Wheels always bind prior variants; animation requires elapsed > .0001.
 * Recovery requires elapsed > 0.
 * Zero elapsed initializes presentation without recovery. Failure is atomic.
 */
enum mh_error_code	mh_field_frame_update(struct mh_field *field,
	float elapsed, struct mh_error *error_out);

/*
 * Commit only the sampled clocks when an original early-exit gate ends the
 * frame before physics/media/presentation. No scene/progress/RNG updates.
 * Do not also advance this same interval through a full frame. Atomic.
 */
enum mh_error_code	mh_field_clock_advance(struct mh_field *field,
	long double elapsed, struct mh_error *error_out);

/*
 * A newly loaded race receives the process clock and a fresh intro (zero in
 * network mode), while retaining the previous race's float sample. No
 * physics/GO sampling runs here. Only fresh fields are accepted. Atomic.
 */
enum mh_error_code	mh_field_clock_load(struct mh_field *field,
	double seconds, const struct mh_frame_time *previous,
	struct mh_error *error_out);

/*
 * Restart owner: stage the entry frame's clock before resetting the race,
 * then add the unscaled reset interval and set its new intro timestamp.
 * The next RUN/MENU advance must pass elapsed=0 and consumes the retained
 * sampled interval once. Neither function runs physics or other stages.
 * Both are atomic; reset-clock requires every car's clock to be staged.
 */
enum mh_error_code	mh_field_clock_stage(struct mh_field *field,
	long double elapsed, struct mh_error *error_out);
enum mh_error_code	mh_field_restart_clock(struct mh_field *field,
	long double elapsed, struct mh_error *error_out);

/*
 * Original sampled frame clock/slices, with one final progress/ranking pass.
 * Race time is uncapped; physics uses at most ten .04s steps. MENU skips
 * physics only; SUSPEND skips all stages. Caller selects elapsed from the
 * entry menu flag, independently of the post-controller action. Atomic.
 */
enum mh_error_code	mh_field_advance(struct mh_field *field,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out);

/* Grid restart resets everyone; recovery moves only the controlled car. */
enum mh_error_code	mh_field_reset(struct mh_field *field, int grid,
	struct mh_error *error_out);

/* Advance with shared CRT RNG; field, stream and steps publish together. */
enum mh_error_code	mh_field_advance_random(struct mh_field *field,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps, unsigned long *random,
	struct mh_error *error_out);
#endif
