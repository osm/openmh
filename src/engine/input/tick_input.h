#ifndef MH_TICK_INPUT_H
#define MH_TICK_INPUT_H

#include "engine/input/binding.h"
#include "engine/simulation/engine.h"

#define MH_TICK_INPUT_CAPACITY 128
#define MH_TICK_SHIFT_UP 1
#define MH_TICK_SHIFT_DOWN 2

struct mh_tick_input_sample {
	struct mh_binding_input	binding;
	struct mh_engine_input	fallback;
	unsigned int		shift_held, shift_pressed;
	int			enabled, horn;

	/* 0 none, 1 recovery, 2 grid restart. */
	unsigned int		reset;
};

struct mh_tick_input_entry {
	unsigned long			tick;
	struct mh_tick_input_sample	sample;
};

struct mh_tick_input {
	struct mh_tick_input_entry	entries[MH_TICK_INPUT_CAPACITY];
	struct mh_tick_input_sample	held;
	unsigned int			first, count, last_shift;
	unsigned long			consumed, last_sample;
};

void	mh_tick_input_init(struct mh_tick_input *input);

/*
 * Samples are assigned to future ticks, never to a catch-up tick in the past.
 * Within a tick the latest held state wins; short shift taps are retained.
 * Full queues fail explicitly without dropping input or changing state.
 */
enum mh_error_code	mh_tick_input_write(struct mh_tick_input *input,
	unsigned long tick, const struct mh_tick_input_sample *sample,
	struct mh_error *error_out);
enum mh_error_code	mh_tick_input_read(struct mh_tick_input *input,
	unsigned long tick, struct mh_tick_input_sample *sample,
	struct mh_error *error_out);

#endif
