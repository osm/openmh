#ifndef MH_AUDIO_MIXER_INTERNAL_H
#define MH_AUDIO_MIXER_INTERNAL_H

#include "engine/audio/mixer.h"
#include "engine/core/error.h"

/*
 * Optional synchronous PCM processor. voice receives an interpolated source
 * sample and replaces its already weighted stereo contribution. frame runs
 * once after all voices, including silent frames, before final saturation.
 * Neither callback runs while paused. Callbacks cannot fail or allocate.
 */
struct mh_mixer_processor {
	void		*context;
	void (*voice)(void *context, unsigned int index,
		const struct mh_mixer_voice *voice, const double source[2],
		double	stereo[2]);
	void		(*frame)(void *context, double stereo[2]);
};

enum mh_error_code	mh_mixer_render_processed(struct mh_mixer *mixer,
	float *stereo, size_t frames, int paused,
	const struct mh_mixer_processor *processor, struct mh_error *error_out);

/*
 * Stored pitch times source rate followed by original device frequency
 * saturation. Negative/zero frequency is handled by the consumer's step
 * conversion. Reject nonfinite/out-of-binary32-domain pitch atomically.
 */
enum mh_error_code	mh_mixer_frequency(double pitch, unsigned long rate,
	float *frequency, struct mh_error *error_out);

/*
 * Transactional sound consumers copy their shared pool with their local
 * mixer. Commit publishes the pool and restores the borrowed pointer before
 * copying the containing consumer state. Nested consumers work identically.
 */
void	mh_mixer_begin(const struct mh_mixer *source, struct mh_mixer *copy,
	struct mh_sound_pool *storage);
void	mh_mixer_commit(struct mh_mixer *copy, const struct mh_mixer *source);

/*
 * Pool records are authoritative after a different bank or global stop has
 * changed their activity. Preserve all sample/parameter/cursor fields.
 */
void	mh_mixer_sync(struct mh_mixer *mixer);

/*
 * Channel-then-pending walk for bound banks. Unbound diagnostic banks stop
 * their fixed physical slots. Physical pooled stops are applied by
 * effects_clear or the next effects_update.
 */
enum mh_error_code	mh_mixer_clear(struct mh_mixer *mixer,
	struct mh_error *error_out);

/*
 * Positional eligibility is consumed during scheduler admission/refresh,
 * after all producer requests. Unbound diagnostic voices stop immediately.
 */
enum mh_error_code	mh_mixer_eligibility(struct mh_mixer *mixer,
	unsigned int voice, int eligible, struct mh_error *error_out);

/*
 * Retain geometry for deferred preparation; these do not request/stop sounds,
 * refresh scores or advance PCM. Bind validates the complete query. Move and
 * listener publish only owned geometry. Errors leave the mixer unchanged.
 */
enum mh_error_code	mh_mixer_spatial_bind(struct mh_mixer *mixer,
	unsigned int voice, const struct mh_sound_listener *listener,
	const double position[3], double volume, double near_distance,
	double far_distance, struct mh_error *error_out);
enum mh_error_code	mh_mixer_vehicle_bind(struct mh_mixer *mixer,
	unsigned int voice, const struct mh_sound_listener *listener,
	const struct mh_motion_state *motion, double volume,
	double near_distance, double far_distance, struct mh_error *error_out);
enum mh_error_code	mh_mixer_spatial_move(struct mh_mixer *mixer,
	unsigned int voice, const double position[3],
	struct mh_error *error_out);
enum mh_error_code	mh_mixer_spatial_listener(struct mh_mixer *mixer,
	const struct mh_sound_listener *listener, struct mh_error *error_out);

/* Preserve source levels without changing software ear gains. */
enum mh_error_code	mh_mixer_source_levels(struct mh_mixer *mixer,
	unsigned int voice, double gain, double pan,
	struct mh_error *error_out);

/*
 * Native per-sample transition. Repeating the same target does not restart it.
 */
enum mh_error_code	mh_mixer_transition(struct mh_mixer *mixer,
	unsigned int voice, double pitch, double left, double right,
	size_t frames, struct mh_error *error_out);

#endif
