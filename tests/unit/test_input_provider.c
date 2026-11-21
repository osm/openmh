#include <stdio.h>
#include <string.h>

#include "interfaces/input/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "input provider line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_event		events[4];
	unsigned int		count, next, opened, closed, samples;
	unsigned int		scan, forces, force_stops;
	unsigned char		character;
	int			focused, fail_open, fail_poll, grab_mouse;
	struct mh_device_keys	keys;
};

static enum mh_error_code
mouse_grab(void *context, void *token, int enabled, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	fixture = token;
	fixture->grab_mouse = enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_input(void *context, struct mh_window *window, int grab_mouse,
	void **output, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)window;
	fixture = context;

	if (fixture->fail_open)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"fixture cannot open input");

	fixture->grab_mouse = grab_mouse;
	fixture->opened++;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_input(void *context, void *input)
{
	struct fixture *fixture;

	(void)context;
	fixture = input;
	fixture->closed++;

	/* Detect teardown before the force stop callback. */
	if (fixture->forces && !fixture->force_stops)
		fixture->closed += 100;
}

static int
focused(void *context, const void *input)
{
	const struct fixture *fixture;

	(void)context;
	fixture = input;

	return fixture->focused;
}

static int
event(void *context, void *input, struct mh_event *output)
{
	struct fixture *fixture;
	struct mh_event next;

	fixture = context;

	if (fixture->next == fixture->count)
		return 0;

	next = fixture->events[fixture->next++];

	if (input != NULL && next.type == MH_EVENT_FOCUS_LOST)
		fixture->focused = 0;

	*output = next;

	return 1;
}

static enum mh_error_code
poll_devices(void *context, void *input, struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
	unsigned long mode, struct mh_device_input *snapshot,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_device_input next;
	enum mh_error_code status;

	(void)context;
	fixture = input;

	if (fixture->fail_poll)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"fixture cannot sample input");

	memset(&next, 0, sizeof(next));
	status = mh_keyboard_poll_devices(
		keyboard, down, now_ms, &fixture->keys, &next, mode, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture->samples++;
	*snapshot = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
key_down(void *context, unsigned int scan)
{
	struct fixture *fixture;

	fixture = context;

	return fixture->scan == scan;
}

static void
characters(void *context, const unsigned char down[MH_KEY_COUNT],
	unsigned char output[MH_KEY_COUNT])
{
	struct fixture *fixture;

	fixture = context;

	if (down[fixture->scan] & MH_KEY_DOWN_BIT)
		output[fixture->scan] = fixture->character;
}

static enum mh_error_code
poll_force(void *context, void *input,
	const struct mh_force_effect initial[MH_FORCE_EFFECTS],
	struct mh_force_status *output, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	(void)initial;
	fixture = input;
	fixture->forces++;
	memset(output, 0, sizeof(*output));
	output->generation = 7;
	output->available[MH_FORCE_SPRING] = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_force(void *context, void *input,
	const struct mh_force_effect effects[MH_FORCE_EFFECTS],
	unsigned int changed, unsigned int start, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)context;
	(void)effects;
	fixture = input;
	fixture->forces += changed + start;

	return mh_error_set(
		error_out, MH_ERROR_BACKEND, "fixture force failure");
}

static void
stop_force(void *context, void *input)
{
	struct fixture *fixture;

	(void)context;
	fixture = input;
	fixture->force_stops++;
}

static const struct mh_force_ops force_ops = {
	poll_force, apply_force, stop_force};

static const struct mh_input_ops ops = {open_input, close_input, focused, event,
	poll_devices, key_down, characters, NULL, mouse_grab};

int
main(void)
{
	struct fixture first_state, second_state;
	struct mh_input_provider first, second;
	struct mh_input_ops incomplete, force_input;
	struct mh_force_ops incomplete_force;
	struct mh_force_effect effects[MH_FORCE_EFFECTS];
	struct mh_force_status force_status;
	struct mh_input first_input, second_input;
	struct mh_keyboard keyboard;
	struct mh_device_input snapshot;
	struct mh_event received;
	struct mh_error error;
	unsigned char down[MH_KEY_COUNT], text[MH_KEY_COUNT];

	memset(&first_state, 0, sizeof(first_state));
	memset(&second_state, 0, sizeof(second_state));
	memset(&first_input, 0, sizeof(first_input));
	memset(&second_input, 0, sizeof(second_input));
	memset(down, 0, sizeof(down));
	first_state.focused = second_state.focused = 1;
	first_state.scan = second_state.scan = MH_KEY_Q;
	first_state.character = 'q';
	second_state.character = 'a';
	first_state.events[0].type = MH_EVENT_KEY_DOWN;
	first_state.events[0].scan = MH_KEY_Q;
	first_state.events[0].window = 17;
	first_state.events[1].type = MH_EVENT_FOCUS_LOST;
	first_state.events[1].window = 17;
	first_state.count = 2;
	second_state.events[0].type = MH_EVENT_QUIT;
	second_state.count = 1;
	CHECK(mh_input_provider_init(&first, &ops, &first_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_input_provider_init(&second, &ops, &second_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_input_open_from(NULL, &first_input, NULL, 1, NULL) ==
		MH_ERROR_UNSUPPORTED);
	first_state.fail_open = 1;
	CHECK(mh_input_open_from(&first, &first_input, NULL, 1, &error) ==
		MH_ERROR_BACKEND);
	CHECK(first_input.backend == NULL && first_state.opened == 0 &&
		first_state.closed == 0);
	CHECK(strcmp(error.message, "fixture cannot open input") == 0);
	first_state.fail_open = 0;
	CHECK(mh_input_open_from(&first, &first_input, NULL, 1, &error) ==
		MH_ERROR_NONE);
	CHECK(error.message == NULL && first_state.opened == 1);
	CHECK(first_state.grab_mouse == 1);
	CHECK(mh_input_open_from(&second, &second_input, NULL, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(second_state.grab_mouse == 0);
	CHECK(mh_input_set_mouse_grab(&first_input, 0, &error) ==
		MH_ERROR_NONE);
	CHECK(first_state.grab_mouse == 0 && error.message == NULL);
	CHECK(mh_input_set_mouse_grab(&second_input, 1, NULL) == MH_ERROR_NONE);
	CHECK(first_state.grab_mouse == 0 && second_state.grab_mouse == 1);
	CHECK(mh_input_set_mouse_grab(&first_input, 2, NULL) ==
		MH_ERROR_INVALID);
	CHECK(first_state.grab_mouse == 0);
	CHECK(mh_input_set_mouse_grab(NULL, 0, NULL) == MH_ERROR_STATE);
	CHECK(mh_input_open_from(&second, &first_input, NULL, 1, NULL) ==
		MH_ERROR_INVALID);
	CHECK(second_state.opened == 1);
	CHECK(mh_input_focused(&first_input) &&
		mh_input_focused(&second_input));
	CHECK(mh_input_event(&first_input, &received));
	CHECK(received.type == MH_EVENT_KEY_DOWN && received.scan == MH_KEY_Q &&
		received.window == 17);
	CHECK(second_state.next == 0);
	CHECK(mh_input_event(&first_input, &received));
	CHECK(received.type == MH_EVENT_FOCUS_LOST &&
		!mh_input_focused(&first_input));
	CHECK(mh_input_focused(&second_input));
	CHECK(!mh_input_event(&first_input, &received));
	CHECK(received.type == MH_EVENT_FOCUS_LOST);
	CHECK(mh_input_provider_event(&second, &received));
	CHECK(received.type == MH_EVENT_QUIT);
	CHECK(!mh_input_provider_event(&second, &received));

	/* New host callbacks may enqueue after an earlier empty drain. */
	second_state.events[second_state.count++].type = MH_EVENT_KEY_UP;
	CHECK(mh_input_event(&second_input, &received));
	CHECK(received.type == MH_EVENT_KEY_UP);
	down[MH_KEY_Q] = MH_KEY_DOWN_BIT;
	mh_input_characters(&first, down, text);
	CHECK(text[MH_KEY_Q] == 'q' && text[MH_KEY_W] == 0);
	mh_input_characters(&second, down, text);
	CHECK(text[MH_KEY_Q] == 'a');
	CHECK(mh_input_key_down(&first, MH_KEY_Q));
	CHECK(!mh_input_key_down(&first, MH_KEY_W));
	CHECK(!mh_input_key_down(NULL, MH_KEY_Q));
	mh_input_characters(NULL, down, text);
	CHECK(text[MH_KEY_Q] == 0);
	CHECK(mh_keyboard_init(&keyboard, NULL) == MH_ERROR_NONE);
	CHECK(mh_input_poll(&first_input, &keyboard, down, 10, 1, &snapshot,
		NULL) == MH_ERROR_NONE);
	CHECK(snapshot.mouse_flags == 0 && first_state.samples == 1 &&
		second_state.samples == 0);
	first_state.fail_poll = 1;
	snapshot.mouse[0] = 42;
	CHECK(mh_input_poll(&first_input, &keyboard, down, 20, 1, &snapshot,
		&error) == MH_ERROR_BACKEND);
	CHECK(snapshot.mouse[0] == 42 && first_state.samples == 1);
	CHECK(strcmp(error.message, "fixture cannot sample input") == 0);
	incomplete = ops;
	incomplete.event = NULL;
	CHECK(mh_input_provider_init(&first, &incomplete, &second_state,
		NULL) == MH_ERROR_INVALID);
	CHECK(first.ops == &ops && first.context == &first_state);
	mh_input_close(&first_input);
	mh_input_close(&first_input);
	CHECK(first_input.backend == NULL && first_state.closed == 1 &&
		second_state.closed == 0);
	CHECK(!mh_input_event(&first_input, &received) &&
		!mh_input_focused(&first_input));
	CHECK(mh_input_poll(&first_input, &keyboard, down, 30, 1, &snapshot,
		NULL) == MH_ERROR_STATE);
	mh_input_close(&second_input);
	CHECK(second_state.closed == 1);
	CHECK(!mh_input_has_force(&first));
	CHECK(mh_input_open_from(&first, &first_input, NULL, 1, NULL) ==
		MH_ERROR_NONE);
	memset(effects, 0, sizeof(effects));
	memset(&force_status, 1, sizeof(force_status));
	CHECK(mh_input_force_poll(&first_input, effects, &force_status, NULL) ==
		MH_ERROR_NONE);
	CHECK(!force_status.available[0] && !force_status.playing[2] &&
		!force_status.generation);
	CHECK(mh_input_force_apply(&first_input, effects, 1, 1, NULL) ==
		MH_ERROR_UNSUPPORTED);
	mh_input_close(&first_input);
	force_input = ops;
	incomplete_force = force_ops;
	incomplete_force.stop = NULL;
	force_input.force = &incomplete_force;
	CHECK(mh_input_provider_init(&second, &force_input, &second_state,
		NULL) == MH_ERROR_INVALID);
	force_input.force = &force_ops;
	CHECK(mh_input_provider_init(&second, &force_input, &second_state,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_input_has_force(&second));
	CHECK(mh_input_open_from(&second, &second_input, NULL, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_input_force_poll(&second_input, effects, &force_status,
		NULL) == MH_ERROR_NONE);
	CHECK(force_status.available[2] && force_status.generation == 7 &&
		second_state.forces == 1);
	CHECK(mh_input_force_apply(&second_input, effects, 4, 4, &error) ==
		MH_ERROR_BACKEND);
	CHECK(second_state.forces == 9 &&
		strcmp(error.message, "fixture force failure") == 0);
	CHECK(mh_input_force_apply(&second_input, effects, 8, 0, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_input_force_apply(&second_input, effects, 0, 1, NULL) ==
		MH_ERROR_INVALID);
	CHECK(second_state.forces == 9);
	mh_input_close(&second_input);
	CHECK(second_state.closed == 2 && second_state.force_stops == 1);
	mh_input_close(&second_input);
	mh_input_force_stop(&second_input);
	CHECK(second_state.force_stops == 1);

	puts("Independent input queues, layout, sampling and owner lifetimes passed");

	return 0;
}
