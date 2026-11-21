#include <stddef.h>
#include <string.h>

#include "engine/audio/spatial_queue.h"

static void
playback_input(const struct mh_spatial_queue_voice *voice,
	const struct mh_spatial_source_parameters *parameters,
	struct mh_spatial_playback_input *input)
{
	input->spatial = voice->input.spatial;

	input->prepared_gain = parameters == NULL ? 0 : parameters->gain;
	input->pan = parameters == NULL ? 0 : parameters->pan;

	input->loaded = voice->loaded;
	input->has_buffer = voice->has_buffer;
	input->loop = voice->loop;
	input->fade_in_samples = voice->fade_in_samples;
	input->fade_out_samples = voice->fade_out_samples;
	input->gain = voice->input.gain;
	input->frequency = parameters == NULL ? 0 : parameters->frequency;
}

static void
detach_voice(
	struct mh_spatial_queue *queue, struct mh_spatial_queue_voice *voice)
{
	unsigned int pending_index;

	if (voice->slot != 0) {
		queue->voices[voice->slot - 1] = NULL;
		voice->slot = 0;
	}

	for (pending_index = 0; pending_index < MH_SPATIAL_QUEUE_PENDING;
		pending_index++)
		if (queue->pending[pending_index] == voice)
			queue->pending[pending_index] = NULL;
}

void
mh_spatial_queue_stop(struct mh_spatial_queue *queue,
	struct mh_spatial_queue_voice *voice,
	const struct mh_spatial_queue_sink *sink)
{
	struct mh_spatial_playback_input input;
	struct mh_spatial_playback_request output;
	unsigned int slot;

	if (voice == NULL || !voice->loaded || !voice->playback.active)
		return;

	slot = voice->slot;
	detach_voice(queue, voice);

	if (slot != 0) {
		playback_input(voice, NULL, &input);
		mh_spatial_playback_stop(&voice->playback, &input, &output);

		if (output.action != MH_SPATIAL_PLAYBACK_NONE)
			sink->playback(sink->context, voice, &output);
	}

	voice->playback.active = 0;
}

void
mh_spatial_queue_request(struct mh_spatial_queue *queue,
	struct mh_spatial_queue_voice *voice,
	const struct mh_spatial_queue_sink *sink)
{
	unsigned int pending_index;

	if (voice == NULL || !voice->loaded)
		return;

	if (voice->playback.active) {
		if (!voice->retrigger)
			return;

		mh_spatial_queue_stop(queue, voice, sink);
	}

	for (pending_index = 0; pending_index < MH_SPATIAL_QUEUE_PENDING;
		pending_index++) {
		if (queue->pending[pending_index] == voice)
			return;

		if (queue->pending[pending_index] == NULL) {
			voice->playback.active = 1;
			queue->pending[pending_index] = voice;
			return;
		}
	}
}

void
mh_spatial_queue_clear(struct mh_spatial_queue *queue,
	const struct mh_spatial_queue_sink *sink)
{
	unsigned int slot_index, pending_index;

	for (slot_index = 0; slot_index < MH_SPATIAL_QUEUE_VOICES; slot_index++)
		mh_spatial_queue_stop(queue, queue->voices[slot_index], sink);

	for (pending_index = 0; pending_index < MH_SPATIAL_QUEUE_PENDING;
		pending_index++)
		mh_spatial_queue_stop(
			queue, queue->pending[pending_index], sink);
}

static int
prepare_source(struct mh_spatial_queue_voice *voice,
	struct mh_spatial_queue_frame *frame,
	const struct mh_spatial_queue_sink *sink)
{
	struct mh_spatial_source_update output;
	struct mh_spatial_source_input input;
	int eligible;

	input = voice->input;
	input.elapsed = frame->listener.elapsed;
	eligible = mh_spatial_source_prepare_bound(&voice->source, &input,
		frame->anchor, frame->secondary_bound, frame->master_gain,
		&frame->parameters, &output);

	if (output.changed)
		sink->source(sink->context, voice, &output);

	return eligible;
}

static unsigned int
select_slot(const struct mh_spatial_queue *queue, float score)
{
	const struct mh_spatial_source_state *voices[MH_SPATIAL_QUEUE_VOICES];
	unsigned int slot_index;

	for (slot_index = 0; slot_index < MH_SPATIAL_QUEUE_VOICES;
		slot_index++) {
		if (queue->voices[slot_index] == NULL)
			voices[slot_index] = NULL;
		else
			voices[slot_index] = &queue->voices[slot_index]->source;
	}

	return mh_spatial_source_slot(score, voices, MH_SPATIAL_QUEUE_VOICES);
}

static void
publish_playback(struct mh_spatial_queue *queue,
	struct mh_spatial_queue_voice *voice,
	struct mh_spatial_queue_frame *frame,
	const struct mh_spatial_queue_sink *sink,
	const struct mh_spatial_playback_request *output)
{
	if (output->action == MH_SPATIAL_PLAYBACK_NONE)
		return;

	if (output->action == MH_SPATIAL_PLAYBACK_STOP)
		detach_voice(queue, voice);
	else {
		frame->parameters.gain = output->log_gain;

		if (output->set_pan)
			frame->parameters.pan = output->log_pan;
	}

	sink->playback(sink->context, voice, output);
}

static void
admit_voice(struct mh_spatial_queue *queue,
	struct mh_spatial_queue_voice *voice,
	struct mh_spatial_queue_frame *frame,
	const struct mh_spatial_queue_sink *sink)
{
	struct mh_spatial_playback_input input;
	struct mh_spatial_playback_request output;
	unsigned int slot;

	slot = prepare_source(voice, frame, sink)
		? select_slot(queue, voice->source.score)
		: 0;

	if (slot == 0) {
		voice->playback.active = 0;
		return;
	}

	mh_spatial_queue_stop(queue, queue->voices[slot - 1], sink);
	voice->slot = slot;
	playback_input(voice, &frame->parameters, &input);
	mh_spatial_playback_start(
		&voice->playback, &input, frame->clock, &output);
	publish_playback(queue, voice, frame, sink, &output);
	queue->voices[slot - 1] = voice;
}

static void
update_voice(struct mh_spatial_queue *queue,
	struct mh_spatial_queue_voice *voice,
	struct mh_spatial_queue_frame *frame,
	const struct mh_spatial_queue_sink *sink,
	struct mh_spatial_playback_status *status)
{
	struct mh_spatial_playback_input input;
	struct mh_spatial_playback_request output;

	if (!voice->loaded || !voice->has_buffer)
		return;

	sink->status(sink->context, voice, status);

	if (!status->playing || !prepare_source(voice, frame, sink)) {
		mh_spatial_queue_stop(queue, voice, sink);
		return;
	}

	status->eligible = 1;
	playback_input(voice, &frame->parameters, &input);
	mh_spatial_playback_step(
		&voice->playback, &input, status, frame->clock, &output);
	publish_playback(queue, voice, frame, sink, &output);
}

void
mh_spatial_queue_step(struct mh_spatial_queue *queue,
	struct mh_spatial_queue_frame *frame,
	const struct mh_spatial_queue_sink *sink)
{
	struct mh_spatial_playback_status status;
	unsigned int slot_index, pending_index;

	memset(&status, 0, sizeof(status));

	for (pending_index = 0; pending_index < MH_SPATIAL_QUEUE_PENDING;
		pending_index++) {
		if (queue->pending[pending_index] == NULL)
			continue;

		admit_voice(queue, queue->pending[pending_index], frame, sink);
		queue->pending[pending_index] = NULL;
	}

	for (slot_index = 0; slot_index < MH_SPATIAL_QUEUE_VOICES; slot_index++)
		if (queue->voices[slot_index] != NULL)
			update_voice(queue, queue->voices[slot_index], frame,
				sink, &status);
}
