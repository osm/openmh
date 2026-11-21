#include <string.h>

#include "engine/input/tick_input.h"

void
mh_tick_input_init(struct mh_tick_input *input)
{
	memset(input, 0, sizeof(*input));
}

enum mh_error_code
mh_tick_input_write(struct mh_tick_input *input, unsigned long tick,
	const struct mh_tick_input_sample *sample, struct mh_error *error_out)
{
	struct mh_tick_input_entry *entry;
	unsigned int index, pressed, reset;
	int merge;

	if (input == NULL || sample == NULL || tick <= input->consumed ||
		tick < input->last_sample || sample->shift_held > 3 ||
		sample->shift_pressed > 3 || sample->reset > 2 ||
		(sample->enabled != 0 && sample->enabled != 1) ||
		(sample->horn != 0 && sample->horn != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid simulation input sample");

	index = (input->first + input->count) % MH_TICK_INPUT_CAPACITY;
	merge = input->count != 0 && tick == input->last_sample;

	if (!merge && input->count == MH_TICK_INPUT_CAPACITY)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"simulation input backlog is full");

	pressed = sample->shift_pressed |
		(sample->shift_held & ~input->last_shift);
	reset = sample->reset;

	if (merge) {
		index = (index + MH_TICK_INPUT_CAPACITY - 1) %
			MH_TICK_INPUT_CAPACITY;
		pressed |= input->entries[index].sample.shift_pressed;

		if (reset < input->entries[index].sample.reset)
			reset = input->entries[index].sample.reset;
	} else
		input->count++;

	entry = &input->entries[index];
	entry->tick = tick;
	entry->sample = *sample;
	entry->sample.shift_pressed = pressed;
	entry->sample.reset = reset;
	input->last_shift = sample->shift_held;
	input->last_sample = tick;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_tick_input_read(struct mh_tick_input *input, unsigned long tick,
	struct mh_tick_input_sample *sample, struct mh_error *error_out)
{
	struct mh_tick_input_entry *entry;
	unsigned int pressed, reset;

	if (input == NULL || sample == NULL || tick != input->consumed + 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"simulation input ticks must be consecutive");

	pressed = reset = 0;

	while (input->count != 0) {
		entry = &input->entries[input->first];

		if (entry->tick > tick)
			break;

		input->held = entry->sample;
		pressed |= entry->sample.shift_pressed;

		if (reset < entry->sample.reset)
			reset = entry->sample.reset;

		input->first = (input->first + 1) % MH_TICK_INPUT_CAPACITY;
		input->count--;
	}

	*sample = input->held;
	sample->shift_pressed = pressed;
	sample->reset = reset;
	input->consumed = tick;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
