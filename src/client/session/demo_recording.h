#ifndef MH_CLIENT_SESSION_DEMO_RECORDING_H
#define MH_CLIENT_SESSION_DEMO_RECORDING_H

#include "engine/core/error.h"
#include "engine/demo/capture.h"
#include "engine/demo/ghost_capture.h"
#include "engine/demo/network_capture.h"
#include "engine/settings/control_layout.h"

struct mh_demo_recording;
struct mh_demo_file;
struct mh_data_directory;

/*
 * Selected prepared directory; adopted only on success, through the returned
 * private take's eventual publication/abort. The provider must outlive it.
 * Open/restart failures preserve the caller's field, output and current take.
 */
/*
 * Owned local/AI take. Open before countdown;
 * advance captures once between physics/playback and presentation, using the
 * clock and original >6s gate. Existing destinations are protected. Applied
 * controls supply the human's device identity; NULL retains the default
 * keyboard identity. No SDL dependency or retained controls pointer. Restart
 * preserves this identity.
 */
enum mh_error_code	mh_demo_recording_open_in(
	struct mh_data_directory *directory, const char *filename,
	const struct mh_field *field, const struct mh_profile *profiles,
	const struct mh_control_binding_set *controls,
	struct mh_demo_recording **output, struct mh_error *error_out);

/*
 * Borrow a stable mutable Ghost race until finish/abort. Subsequent frame,
 * restart and finish calls must supply &race->field. Restart resets both
 * playback and the take atomically, preserving lifetime time and slices.
 * Demo watching (mode 6) cannot record.
 */
enum mh_error_code	mh_demo_recording_open_ghost_in(
	struct mh_data_directory *directory, const char *filename,
	struct mh_demo_race *race, const struct mh_profile *human,
	const struct mh_control_binding_set *controls,
	struct mh_demo_recording **output, struct mh_error *error_out);

/*
 * Borrow a stable network race, freeze its wire identities before the first
 * frame, and gate capture on the released loading barrier. Finish on lobby
 * return before freeing assets. Network takes cannot restart in place.
 */
enum mh_error_code	mh_demo_recording_open_network_in(
	struct mh_data_directory *directory, const char *filename,
	struct mh_net_race *race, const struct mh_net_session *session,
	struct mh_demo_recording **output, struct mh_error *error_out);

/*
 * Active local/Ghost recorder: advances the race, captures at the frame
 * boundary, then writes only after the whole simulation frame succeeds. A frame
 * failure preserves world, RNG, recorder and outputs. A file write failure
 * keeps the completed world frame and disables the take. Ghost callers must
 * supply the bound &race->field.
 */
enum mh_error_code	mh_demo_recording_advance(
	struct mh_demo_recording *recording, struct mh_field *field,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps, unsigned long *random,
	struct mh_error *error_out);

/* Network equivalent, with the same world/file failure ownership. */
enum mh_error_code	mh_demo_recording_network_advance(
	struct mh_demo_recording *recording, struct mh_net_session *session,
	const struct mh_engine_input *controls, long double elapsed,
	double now_ms, unsigned int viewed_entry, unsigned int *steps,
	struct mh_error *error_out);

/*
 * Explicit end-of-frame sampling for fixed diagnostic callers.
 * Runtime sessions use the integrated advance functions above instead.
 */
enum mh_error_code	mh_demo_recording_frame(
	struct mh_demo_recording *recording, const struct mh_field *field,
	struct mh_error *error_out);

/*
 * A short append disables recording and discards its private file. The frame
 * still returns the error; live callers may continue racing only when this
 * query identifies it as a write failure. Other errors remain fatal.
 */
enum mh_error_code	mh_demo_recording_write_error(
	const struct mh_demo_recording *recording, struct mh_error *error_out);

/*
 * Local/Ghost session policy, called immediately after advancing an active
 * recorder. Pass that call's status and diagnostic. A confirmed write failure
 * is reported to stderr, clears the required record_race setting, and returns
 * success so racing continues. Other results and diagnostics are unchanged.
 * The recorder retains its failure for HUD notices and eventual cleanup.
 */
enum mh_error_code	mh_demo_recording_recover_write_error(
	const struct mh_demo_recording *recording, unsigned int *record_race,
	enum mh_error_code status, struct mh_error *error_out);
int			mh_demo_recording_active(
	const struct mh_demo_recording *recording);
unsigned long		mh_demo_recording_failure_ticks(
	const struct mh_demo_recording *recording);

/*
 * Prepare a fresh grid and new private take before discarding the old one.
 * Failure leaves the field and current take unchanged. After a write failure,
 * restart resets only the race, retains the failure and never reopens a take;
 * start/failure notices and timestamps clear on a successful restart.
 */
enum mh_error_code	mh_demo_recording_restart(
	struct mh_demo_recording *recording, struct mh_field *field,
	struct mh_error *error_out);

/*
 * Finish/abort consume the object. A take with fewer than 64 padded frames
 * is discarded with saved=0, since the original loader cannot play it.
 */
enum mh_error_code	mh_demo_recording_finish(
	struct mh_demo_recording *recording, const struct mh_field *field,
	int *saved, struct mh_error *error_out);

/*
 * Consume the recorder, retaining only a closed private take for the menu.
 * Sets *take to NULL on short takes or error. A successful nonempty take is
 * independent of the field/assets/Ghost race; caller must publish or abort.
 */
enum mh_error_code	mh_demo_recording_prepare(
	struct mh_demo_recording *recording, const struct mh_field *field,
	struct mh_demo_file **take, struct mh_error *error_out);
void			mh_demo_recording_abort(
	struct mh_demo_recording *recording);

/*
 * Notice presence is independent of physics ticks: a menu frame can start
 * recording or fail a write before the first physics step. Restart clears
 * both notices, including after a write failure disables the take.
 */
int	mh_demo_recording_start_notice(
	const struct mh_demo_recording *recording);
int	mh_demo_recording_failure_notice(
	const struct mh_demo_recording *recording);

/* Diagnostic lifetime tick at the first append; zero is also a valid tick. */
unsigned long	mh_demo_recording_start_ticks(
	const struct mh_demo_recording *recording);

/* Notification timestamps independent of the physics step count. */
unsigned long	mh_demo_recording_start_ms(
	const struct mh_demo_recording *recording);
unsigned long	mh_demo_recording_failure_ms(
	const struct mh_demo_recording *recording);

#endif
