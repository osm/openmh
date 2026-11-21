#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/record.h"

#define RECORD_INTERVAL_MS 62.5

static unsigned int
player_mask(const struct mh_demo_recorder *recorder)
{
	return (1U << mh_demo_slots(recorder->header.version)) - 1;
}

static void
write_u32le(unsigned char *cursor, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		cursor[i] = (unsigned char)(value >> (8 * i));
}

static int
valid_recorder(const struct mh_demo_recorder *recorder)
{
	unsigned int slot, mask, count;

	if (recorder == NULL || recorder->closed ||
		recorder->ring_index >= MH_DEMO_RING_FRAMES ||
		recorder->initial_mask == 0 ||
		recorder->header.player_count < 1 ||
		recorder->header.player_count >
			mh_demo_slots(recorder->header.version) ||
		recorder->anchor_ms > recorder->last_frame_ms ||
		recorder->last_frame_ms > recorder->clock_ms ||
		recorder->clock_ms > MH_U32_MAX ||
		recorder->frame_count % MH_DEMO_RING_FRAMES !=
			recorder->ring_index ||
		recorder->header.frame_bytes !=
			MH_DEMO_FRAME_HEADER_BYTES +
				MH_DEMO_VEHICLE_BYTES *
					recorder->header.player_count)
		return 0;

	mask = count = 0;

	for (slot = 0; slot < mh_demo_slots(recorder->header.version); slot++)

		if (recorder->header.players[slot].active) {
			mask |= 1U << slot;
			count++;
		}

	return mask == recorder->initial_mask &&
		count == recorder->header.player_count;
}

enum mh_error_code
mh_demo_record_begin(const struct mh_demo *header,
	struct mh_demo_recorder *output, struct mh_error *error_out)
{
	struct mh_demo_recorder next;
	unsigned char encoded_header[MH_DEMO_MAX_HEADER_BYTES];
	unsigned int slot;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing recorder output");

	status = mh_demo_header_write(
		header, encoded_header, sizeof(encoded_header), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	next.header = *header;
	next.header.data = NULL;
	next.header.size = next.header.frame_count = 0;
	next.header.frame_bytes = MH_DEMO_FRAME_HEADER_BYTES +
		MH_DEMO_VEHICLE_BYTES * header->player_count;

	for (slot = 0; slot < mh_demo_slots(header->version); slot++)
		if (header->players[slot].active)
			next.initial_mask |= 1U << slot;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
append_frame(struct mh_demo_recorder *recorder,
	const struct mh_demo_record_sample *sample, int padding,
	struct mh_demo_record_event *event)
{
	unsigned char *cursor;
	unsigned int slot;
	unsigned long flags;
	size_t frame_bytes, offset;

	frame_bytes = recorder->header.frame_bytes;
	cursor = recorder->ring + recorder->ring_index * frame_bytes;
	flags = sample->active_mask & recorder->initial_mask;

	if (!padding || recorder->header.version != MH_DEMO_VERSION_25)
		flags |= (unsigned long)(sample->unavailable_mask &
				recorder->initial_mask)
			<< mh_demo_slots(recorder->header.version);

	write_u32le(cursor, flags);
	write_u32le(cursor + 4,
		(recorder->last_frame_ms - recorder->anchor_ms) & MH_U32_MAX);
	cursor += MH_DEMO_FRAME_HEADER_BYTES;

	for (slot = 0; slot < mh_demo_slots(recorder->header.version); slot++)
		if (recorder->initial_mask & (1U << slot)) {
			memcpy(cursor, sample->bodies[slot],
				MH_DEMO_VEHICLE_BYTES);
			cursor += MH_DEMO_VEHICLE_BYTES;
		}

	recorder->frame_count++;
	event->frames++;
	recorder->ring_index = (recorder->ring_index + 1) % MH_DEMO_RING_FRAMES;

	if (recorder->ring_index == 0 ||
		recorder->ring_index == MH_DEMO_BLOCK_FRAMES) {
		offset = recorder->ring_index == 0
			? MH_DEMO_BLOCK_FRAMES * frame_bytes
			: 0;
		event->size = MH_DEMO_BLOCK_FRAMES * frame_bytes;
		memcpy(event->data, recorder->ring + offset, event->size);
	}
}

enum mh_error_code
mh_demo_record_step(struct mh_demo_recorder *recorder, unsigned long clock_ms,
	const struct mh_demo_record_sample *sample,
	struct mh_demo_record_event *event, struct mh_error *error_out)
{
	struct mh_demo_recorder next;
	struct mh_demo_record_event next_event;

	if (!valid_recorder(recorder) || event == NULL || sample == NULL ||
		clock_ms > MH_U32_MAX || clock_ms < recorder->clock_ms ||
		sample->active_mask > player_mask(recorder) ||
		sample->unavailable_mask > player_mask(recorder))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording clock or sample");

	next = *recorder;
	memset(&next_event, 0, sizeof(next_event));
	next.clock_ms = clock_ms;

	if (next.anchor_ms == 0) {
		next.anchor_ms = next.last_frame_ms = clock_ms;
		next_event.started = 1;
	}

	/*
	 * Both comparison operands are converted unsigned words. Adding 62.5
	 * does not wrap at the u32 boundary.
	 */
	if ((double)clock_ms >
		(double)next.last_frame_ms + RECORD_INTERVAL_MS) {
		if (next.frame_count == (size_t)-1)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"recording frame count overflow");

		next.last_frame_ms = clock_ms;
		append_frame(&next, sample, 0, &next_event);
	}

	*recorder = next;
	*event = next_event;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_record_finish(struct mh_demo_recorder *recorder,
	const struct mh_demo_record_sample *sample,
	const struct mh_demo_player *players, unsigned int crossing_mask,
	struct mh_demo_record_event *event, struct mh_error *error_out)
{
	struct mh_demo_recorder next;
	struct mh_demo_record_event next_event;
	unsigned char encoded_header[MH_DEMO_MAX_HEADER_BYTES];
	unsigned int slot, frame, padding;
	enum mh_error_code status;

	if (!valid_recorder(recorder) || event == NULL || sample == NULL ||
		crossing_mask > player_mask(recorder) ||
		(players == NULL && crossing_mask != 0) ||
		sample->active_mask > player_mask(recorder) ||
		sample->unavailable_mask > player_mask(recorder))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording finalization");

	padding = (MH_DEMO_BLOCK_FRAMES -
			recorder->ring_index % MH_DEMO_BLOCK_FRAMES) %
		MH_DEMO_BLOCK_FRAMES;

	if (recorder->frame_count > (size_t)-1 - padding)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"recording frame count overflow");

	next = *recorder;
	memset(&next_event, 0, sizeof(next_event));

	for (slot = 0; slot < mh_demo_slots(recorder->header.version); slot++)
		if (crossing_mask & (1U << slot))
			memcpy(next.header.players[slot].crossings,
				players[slot].crossings,
				sizeof(players[slot].crossings));

	status = mh_demo_header_write(&next.header, encoded_header,
		sizeof(encoded_header), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (frame = 0; frame < padding; frame++) {
		next.last_frame_ms = (next.last_frame_ms + 1) & MH_U32_MAX;
		append_frame(&next, sample, 1, &next_event);
	}

	next.closed = 1;
	*recorder = next;
	*event = next_event;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
