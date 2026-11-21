#include <stddef.h>
#include <string.h>

#include "engine/audio/a3d12_queue.h"

/* The bound scheduler and profile source selector must agree on slots. */
typedef char queue_capacity_matches[(MH_A3D12_VOICES == MH_SPATIAL_QUEUE_VOICES)
		? 1
		: -1];

/*
 * Voice.common is the first member; these conversions preserve the profile
 * identity used by PCM bindings without aliasing arrays or copying state.
 */
static void
status(void *context, struct mh_spatial_queue_voice *voice,
	struct mh_spatial_playback_status *output)
{
	const struct mh_a3d12_queue_sink *sink = context;

	struct mh_a3d12_buffer_caps caps;
	struct mh_a3d12_voice *owner;

	owner = (struct mh_a3d12_voice *)voice;
	memset(&caps, 0, sizeof(caps));
	sink->capabilities(sink->context, owner, &caps);

	if (caps.hardware)
		owner->hardware = 1;

	if (caps.software)
		owner->hardware = 0;

	sink->status(
		sink->context, (const struct mh_a3d12_voice *)voice, output);
}

static void
source(void *context, const struct mh_spatial_queue_voice *voice,
	const struct mh_spatial_source_update *output)
{
	const struct mh_a3d12_queue_sink *sink = context;

	sink->source(
		sink->context, (const struct mh_a3d12_voice *)voice, output);
}

static void
playback(void *context, const struct mh_spatial_queue_voice *voice,
	const struct mh_spatial_playback_request *output)
{
	const struct mh_a3d12_queue_sink *sink = context;

	sink->playback(
		sink->context, (const struct mh_a3d12_voice *)voice, output);
}

static void
adapt(const struct mh_a3d12_queue_sink *sink,
	struct mh_spatial_queue_sink *output)
{
	output->context = (void *)sink;

	output->status = status;
	output->source = source;
	output->playback = playback;
}

void
mh_a3d12_queue_request(struct mh_a3d12_queue *queue,
	struct mh_a3d12_voice *voice, const struct mh_a3d12_queue_sink *sink)
{
	struct mh_spatial_queue_sink output;

	adapt(sink, &output);
	mh_spatial_queue_request(
		&queue->common, voice == NULL ? NULL : &voice->common, &output);
}

void
mh_a3d12_queue_stop(struct mh_a3d12_queue *queue, struct mh_a3d12_voice *voice,
	const struct mh_a3d12_queue_sink *sink)
{
	struct mh_spatial_queue_sink output;

	adapt(sink, &output);
	mh_spatial_queue_stop(
		&queue->common, voice == NULL ? NULL : &voice->common, &output);
}

void
mh_a3d12_queue_clear(
	struct mh_a3d12_queue *queue, const struct mh_a3d12_queue_sink *sink)
{
	struct mh_spatial_queue_sink output;

	adapt(sink, &output);
	mh_spatial_queue_clear(&queue->common, &output);
}

void
mh_a3d12_queue_step(struct mh_a3d12_queue *queue,
	struct mh_spatial_queue_frame *frame,
	const struct mh_a3d12_queue_sink *sink)
{
	struct mh_spatial_queue_sink output;
	struct mh_spatial_listener_update listener;

	adapt(sink, &output);
	mh_spatial_queue_step(&queue->common, frame, &output);
	mh_a3d12_listener_step(&queue->listener, &frame->listener, &listener);

	if (listener.changed)
		sink->listener(sink->context, &listener);
}
