#ifndef MH_SPATIAL_EFFECTS_H
#define MH_SPATIAL_EFFECTS_H
#include "engine/audio/spatial_bank.h"
#include "engine/audio/spatial_pcm.h"
#include "engine/audio/spatial_queue.h"
/*
 * Producer-bank integration, independent of optional audio profiles.
 * Queue operations, PCM bindings, loader selection and capability queries stay
 * with the profile owner. No allocation, device I/O or profile callbacks.
 */
enum mh_error_code	mh_spatial_effects_banks(
	const struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, unsigned int slots, struct mh_error *error_out);
void			mh_spatial_effects_listener(
	struct mh_spatial_queue_frame *frame,
	const struct mh_sound_listener *listener);

/*
 * Borrowed clock, initialized timing history. First/nonpositive steps use
 * the service interval; advance history and clear scratch before scheduling.
 */
void	mh_spatial_effects_frame(struct mh_spatial_queue_frame *frame,
	int listener_ready, int *serviced, long double *serviced_ms);

/* Valid loaded bank voice, with unchanged spatial preparation semantics. */
void			mh_spatial_effects_source(
	struct mh_spatial_queue_voice *voice, const struct mh_mixer *bank,
	unsigned int bank_voice);
enum mh_error_code	mh_spatial_effects_sample(
	struct mh_spatial_queue_voice *voice, const struct mh_mixer *bank,
	unsigned int bank_voice, int spatial, int software_only,
	struct mh_audio_pcm *wave, struct mh_error *error_out);

/*
 * Caller clears pool channels once before publishing every record. Banks
 * must pass validation; voice slots and PCM channels are scheduler-owned.
 */
void	mh_spatial_effects_activity(const struct mh_spatial_queue_voice *voice,
	const struct mh_spatial_pcm *pcm, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], unsigned int record_index);
#endif
