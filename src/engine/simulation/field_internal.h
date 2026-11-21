#ifndef MH_FIELD_INTERNAL_H
#define MH_FIELD_INTERNAL_H

#include "engine/core/error.h"
#include "engine/simulation/field.h"

/* Native supported starting-grid slots; also used by diagnostic grid ranks. */
#define MH_FIELD_GRID_SLOTS 64

/*
 * Internal stages operate on the coordinator's unpublished working copy.
 * They may mutate on failure; the public caller owns rollback.
 */
enum mh_error_code	mh_field_rank(struct mh_field *field, int grid,
	struct mh_error *error_out);
enum mh_error_code	mh_field_records_update(struct mh_field *field,
	struct mh_error *error_out);
enum mh_error_code	mh_field_locate(struct mh_field *field,
	unsigned int entry, struct mh_error *error_out);
enum mh_error_code	mh_field_ai_inputs(const struct mh_field *field,
	unsigned int reset_mask,
	struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS],
	struct mh_error *error_out);
enum mh_error_code	mh_field_ai_reset(struct mh_field *field,
	struct mh_error *error_out);
enum mh_error_code	mh_field_traffic_sample(const struct mh_field *field,
	unsigned int entry, struct mh_ai_traffic_car *output,
	struct mh_error *error_out);
enum mh_error_code	mh_field_object_reset(struct mh_field_object *object,
	int fresh, struct mh_error *error_out);
enum mh_error_code	mh_field_objects_select(struct mh_field *field,
	struct mh_error *error_out);
enum mh_error_code	mh_field_objects_begin(struct mh_field *field,
	struct mh_motion_state *entry, double dt, int first,
	struct mh_error *error_out);
enum mh_error_code	mh_field_objects_contact(struct mh_field *field,
	const struct mh_motion_state *entry, double dt,
	struct mh_error *error_out);
enum mh_error_code	mh_field_contact_tick(struct mh_field *field,
	const struct mh_engine_input *controls, int held,
	const struct mh_ai_driver_result *decisions, int first,
	double object_dt, struct mh_error *error_out);
enum mh_error_code	mh_field_clock_sample(struct mh_field *field,
	long double elapsed, struct mh_error *error_out);

/*
 * Frame stages for local, recorded and network races. The enclosing adapter
 * owns a private copy and publishes only after EVERY stage succeeds. These
 * helpers may mutate their input on failure and are not public atomic
 * operations.
 *
 * Original order: physics -> recorded pose application -> frame_update
 * (wheels/recovery/camera) -> progress -> recorded time correction -> results.
 * The caller handles SUSPEND; RUN/MENU sample the clock once per frame,
 * with MENU omitting physical integration only.
 */
enum mh_error_code	mh_field_frame_physics(struct mh_field *field,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out);

/*
 * Begin captures clock and physics membership before network status/pose
 * updates; simulate filters that retained list using the updated poses.
 * These two stages together implement frame_physics above.
 */
enum mh_error_code	mh_field_frame_begin(struct mh_field *field,
	long double elapsed, struct mh_error *error_out);
enum mh_error_code	mh_field_frame_simulate(struct mh_field *field,
	const struct mh_engine_input *controls, unsigned int *steps,
	struct mh_error *error_out);
enum mh_error_code	mh_field_frame_progress(struct mh_field *field,
	struct mh_error *error_out);
enum mh_error_code	mh_field_frame_results(struct mh_field *field,
	struct mh_error *error_out);

/* Local frame tail, without recorded-time correction. */
enum mh_error_code	mh_field_frame_finish(struct mh_field *field,
	struct mh_error *error_out);

#endif
