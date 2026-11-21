#ifndef MH_A3D20_QUEUE_H
#define MH_A3D20_QUEUE_H

#include "engine/audio/a3d20_listener.h"
#include "engine/audio/a3d20_playback.h"
#include "engine/audio/a3d20_source.h"

struct mh_world;

#define MH_A3D20_PENDING 32

/* Slot is one-based; zero means unassigned. Pending voices are active. */
struct mh_a3d20_voice {
	struct mh_a3d20_source_input	input;
	struct mh_spatial_source_state	source;
	struct mh_a3d20_playback_state	playback;
	const char			*name;
	unsigned long	sample_rate, fade_in_samples, fade_out_samples;
	unsigned int			slot;
	int				loaded, loop, retrigger;
};

struct mh_a3d20_queue {
	struct mh_a3d20_listener_state	listener;
	struct mh_a3d20_voice		*pending[MH_A3D20_PENDING];
	struct mh_a3d20_voice		*voices[MH_A3D20_VOICES];
};

/*
 * All callbacks are synchronous, required and must not reenter/mutate owners.
 * begin clears the scene, loads identity and, for non-NULL game position,
 * binds the listener at (x,y,-z) using push/translate/bind/pop.
 * source and playback submit the sequences documented by their policy headers.
 * listener submits orientation then velocity (position was already bound).
 * geometry requests nearby track data using game position and the borrowed MYW;
 * flush submits the complete scene. Geometry collection and materials are
 * separate consumers, not implemented by this queue policy. Status is in/out:
 * failures may retain the previous voice's playing/looping fields within this
 * frame. Callbacks apply writes even when a driver fails.
 */
struct mh_a3d20_queue_sink {
	void						*context;
	void	(*begin)(void *context, const float *position);
	void (*status)(void *context, const struct mh_a3d20_voice *voice,
		struct mh_a3d20_playback_status		*status);
	void (*source)(void *context, const struct mh_a3d20_voice *voice,
		const struct mh_a3d20_source_update	*update);
	void (*playback)(void *context, const struct mh_a3d20_voice *voice,
		const struct mh_a3d20_playback_request	*request);
	void (*listener)(
		void	*context, const struct mh_a3d20_listener_update *update);
	void (*geometry)(void *context, const float position[3],
		const struct mh_world			*world);
	void						(*flush)(void *context);
};

struct mh_a3d20_queue_frame {
	struct mh_a3d20_listener_input		listener;
	float					right[3];
	int					listener_bound;
	const struct mh_clock_source		*clock;
	const struct mh_world			*world;
	float					master_gain;
	struct mh_a3d20_source_parameters	parameters;
};

/*
 * Zero-initialize owners. Borrowed voices, names and position bindings remain
 * live until detached. Input contracts follow source/playback headers; slots
 * belong to the queue. Initialize frame.parameters before stepping the queue.
 *
 * Pending admission seeds source history from its binding before preparation,
 * passing zero elapsed. Motion requests carry the nonfinite classifications in
 * a3d20_source.h. Published linear gain feeds the shared scratch for subsequent
 * voices.
 *
 * Accepted starts skip the active pass once, even after failed Play. Then
 * active voices run before listener history advances. Stop/clear retain
 * listener history and original inactive guards. A bound listener requires
 * positive frame elapsed; without a pose, zero elapsed is allowed. A NULL voice
 * is a no-op. There is no allocation or audible processing.
 */
void	mh_a3d20_queue_request(struct mh_a3d20_queue *queue,
	struct mh_a3d20_voice *voice, const struct mh_a3d20_queue_sink *sink);
void	mh_a3d20_queue_stop(struct mh_a3d20_queue *queue,
	struct mh_a3d20_voice *voice, const struct mh_a3d20_queue_sink *sink);
void	mh_a3d20_queue_clear(struct mh_a3d20_queue *queue,
	const struct mh_a3d20_queue_sink *sink);
void	mh_a3d20_queue_step(struct mh_a3d20_queue *queue,
	struct mh_a3d20_queue_frame *frame,
	const struct mh_a3d20_queue_sink *sink);

#endif
