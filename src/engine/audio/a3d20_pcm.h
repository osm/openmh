#ifndef MH_A3D20_PCM_H
#define MH_A3D20_PCM_H

#include "engine/audio/a3d20_acoustics.h"
#include "engine/audio/a3d20_queue.h"
#include "engine/audio/spatial_pcm.h"

/*
 * A3D 2.0 queue consumer with native occlusion and first-order reflections.
 * Owns PCM cursors, allocated acoustic storage and the collected scene; borrows
 * prepared mono samples and voice identities. No HRTF or device ownership.
 * All stored vectors/triangles use game coordinates. Destroy
 * before reinitializing; do not shallow-copy this owner.
 */
struct mh_a3d20_pcm {
	struct mh_spatial_pcm		output;
	struct mh_a3d20_acoustics	*acoustics;
	struct mh_a3d20_geometry	geometry;
	struct mh_a3d20_material	material;

	/* Final effects bus gain, including travelling echoes. */
	double				volume;
	int				building;
};

enum mh_error_code	mh_a3d20_pcm_init(unsigned long rate, double gain,
	struct mh_a3d20_pcm *pcm, struct mh_error *error_out);

/* Clear the queue first, then drop pending echoes on race reset/teardown. */
void	mh_a3d20_pcm_clear(struct mh_a3d20_pcm *pcm);
void	mh_a3d20_pcm_destroy(struct mh_a3d20_pcm *pcm);

/*
 * Bind inactive voices before queueing. Caller supplies the prepared sample
 * payload and normalized distances from a3d20_sample; duplicate voices may
 * borrow the same PCM bytes while owning independent cursors. Samples remain
 * live until queue clear. Bound voices stay live until binding replacement or
 * owner retirement, even after queue clear. All A3D 2.0 sources use
 * positional mono buffers, even when their game policy is listener-relative.
 */
enum mh_error_code	mh_a3d20_pcm_bind(struct mh_a3d20_pcm *pcm,
	unsigned int record_index, const struct mh_a3d20_voice *voice,
	const struct mh_audio_pcm *wave, struct mh_error *error_out);

/*
 * Borrowed synchronous callbacks. begin clears geometry, geometry collects a
 * provided parsed, immutable MYW, and flush ends the update. NULL world
 * explicitly selects an empty scene for direct-sound fixtures. Collection
 * failures latch an error rather than silently dropping geometry. Nonfinite
 * velocity requests are rejected as a whole, preserving the last finite vector
 * (initially zero); position binding still proceeds. This is a native policy
 * for nonfinite startup results, independent of driver behavior. Serialize all
 * queue and render operations. No callback allocates or does I/O.
 */
void	mh_a3d20_pcm_sink(struct mh_a3d20_pcm *pcm,
	struct mh_a3d20_queue_sink *sink);

/* Render only after flush. Pause emits silence and retains playback cursors. */
enum mh_error_code	mh_a3d20_pcm_render(struct mh_a3d20_pcm *pcm,
	float *stereo, size_t frames, int paused, struct mh_error *error_out);

#endif
