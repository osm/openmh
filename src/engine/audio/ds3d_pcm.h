#ifndef MH_DS3D_PCM_H
#define MH_DS3D_PCM_H

#include "engine/audio/ds3d_queue.h"
#include "engine/audio/spatial_pcm.h"

/*
 * DS3D queue adapter. Bound voices remain live until their binding is replaced
 * or this owner is retired, even after the queue is cleared.
 */
struct mh_ds3d_pcm {
	struct mh_spatial_pcm	output;
};

enum mh_error_code	mh_ds3d_pcm_init(unsigned long rate, double gain,
	struct mh_ds3d_pcm *pcm, struct mh_error *error_out);

/*
 * Bind before queueing. WAV is already trimmed by its sample owner; it and its
 * bytes remain live until the queue is cleared and this owner is retired.
 * Spatial samples must be mono. Captures near/far distances at binding, with
 * the minimum near distance of 1. No active/queued rebinding.
 */
enum mh_error_code	mh_ds3d_pcm_bind(struct mh_ds3d_pcm *pcm,
	unsigned int record_index, const struct mh_ds3d_voice *voice,
	const struct mh_audio_pcm *wave, struct mh_error *error_out);

/* Borrowed synchronous sink; serialize queue operations and PCM rendering. */
void	mh_ds3d_pcm_sink(struct mh_ds3d_pcm *pcm,
	struct mh_ds3d_queue_sink *sink);

/*
 * Render after a complete queue step, so deferred source/listener updates are
 * heard together. Pause emits silence without advancing cursors. Reports the
 * first failed callback; clear the queue before reinitializing or freeing data.
 */
enum mh_error_code	mh_ds3d_pcm_render(struct mh_ds3d_pcm *pcm,
	float *stereo, size_t frames, int paused, struct mh_error *error_out);

#endif
