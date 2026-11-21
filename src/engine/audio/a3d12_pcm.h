#ifndef MH_A3D12_PCM_H
#define MH_A3D12_PCM_H

#include "engine/audio/a3d12_queue.h"
#include "engine/audio/spatial_pcm.h"

/*
 * A3D 1.2 queue adapter. Reports software buffer placement for native CPU
 * mixing; no Aureal hardware/HRTF emulation or environmental processing.
 * Bound voices remain live until their binding is replaced or this owner is
 * retired, even after the queue is cleared.
 */
struct mh_a3d12_pcm {
	struct mh_spatial_pcm	output;
};

enum mh_error_code	mh_a3d12_pcm_init(unsigned long rate, double gain,
	struct mh_a3d12_pcm *pcm, struct mh_error *error_out);

/*
 * Bind before queueing. WAV is already trimmed by its sample owner; it and its
 * bytes remain live until the queue is cleared and this owner is retired.
 * Spatial samples must be mono. Captures near/far distances at binding, with
 * the minimum near distance of 1. No active/queued rebinding.
 */
enum mh_error_code	mh_a3d12_pcm_bind(struct mh_a3d12_pcm *pcm,
	unsigned int record_index, const struct mh_a3d12_voice *voice,
	const struct mh_audio_pcm *wave, struct mh_error *error_out);

/* Borrowed synchronous sink; serialize queue operations and PCM rendering. */
void	mh_a3d12_pcm_sink(struct mh_a3d12_pcm *pcm,
	struct mh_a3d12_queue_sink *sink);

/*
 * Render after a complete queue step, so deferred source/listener updates are
 * heard together. Pause emits silence without advancing cursors. Reports the
 * first failed callback; clear the queue before reinitializing or freeing data.
 */
enum mh_error_code	mh_a3d12_pcm_render(struct mh_a3d12_pcm *pcm,
	float *stereo, size_t frames, int paused, struct mh_error *error_out);

#endif
