#include <stdio.h>
#include <string.h>

#include "client/loading/confirmation.h"
#include "interfaces/input/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "loading confirmation:%d: %s\n",       \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_event	events[8];
	unsigned int	count, next, opened, closed;
	unsigned char	down[MH_KEY_COUNT];
	int		focused;
};

static enum mh_error_code
open_input(void *context, struct mh_window *window, int grab, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)window;
	(void)grab;
	fixture = context;
	fixture->opened++;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_input(void *context, void *input)
{
	struct fixture *fixture;

	(void)input;
	fixture = context;
	fixture->closed++;
}

static int
focused(void *context, const void *input)
{
	(void)input;

	return ((struct fixture *)context)->focused;
}

static int
event(void *context, void *input, struct mh_event *output)
{
	struct fixture *fixture;

	(void)input;
	fixture = context;

	if (fixture->next == fixture->count)
		return 0;

	*output = fixture->events[fixture->next++];

	if (output->window == 0) {
		if (output->type == MH_EVENT_KEY_DOWN)
			fixture->down[output->scan] = 1;

		if (output->type == MH_EVENT_KEY_UP)
			fixture->down[output->scan] = 0;

		if (output->type == MH_EVENT_FOCUS_LOST)
			fixture->focused = 0;

		if (output->type == MH_EVENT_FOCUS_GAINED)
			fixture->focused = 1;
	}

	return 1;
}

static enum mh_error_code
poll_input(void *context, void *input, struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now,
	unsigned long mode, struct mh_device_input *snapshot,
	struct mh_error *error_out)
{
	(void)context;

	(void)input;
	(void)keyboard;
	(void)down;
	(void)now;
	(void)mode;
	(void)snapshot;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
key_down(void *context, unsigned int scan)
{
	return ((struct fixture *)context)->down[scan];
}

static void
characters(void *context, const unsigned char down[MH_KEY_COUNT],
	unsigned char output[MH_KEY_COUNT])
{
	(void)context;

	(void)down;
	memset(output, 0, MH_KEY_COUNT);
}

static enum mh_error_code
mouse_grab(void *context, void *input, int enabled, struct mh_error *error_out)
{
	(void)context;
	(void)input;
	(void)enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
queue(struct fixture *fixture, enum mh_event_type type, int repeat,
	unsigned long window)
{
	struct mh_event *item;

	if (fixture->count == fixture->next)
		fixture->count = fixture->next = 0;

	item = &fixture->events[fixture->count++];
	memset(item, 0, sizeof(*item));
	item->type = type;
	item->scan = MH_KEY_ENTER;
	item->repeat = repeat;
	item->window = window;
}

static int
check_keys(
	struct mh_loading_confirmation *confirmation, struct fixture *fixture)
{
	/*
	 * The menu's held Enter and its repeat events cannot acknowledge
	 * loading.
	 */
	queue(fixture, MH_EVENT_KEY_DOWN, 0, 0);

	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_KEY_DOWN, 1, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_KEY_UP, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_KEY_DOWN, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_KEY_UP, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) ==
		MH_LOADING_CONTINUE);

	return 0;
}

static int
check_queued_and_focus(
	struct mh_loading_confirmation *confirmation, struct fixture *fixture)
{
	queue(fixture, MH_EVENT_KEY_DOWN, 0, 0);

	queue(fixture, MH_EVENT_KEY_UP, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_KEY_DOWN, 1, 0);
	queue(fixture, MH_EVENT_KEY_UP, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_KEY_DOWN, 0, 77);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_FOCUS_LOST, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_FOCUS_GAINED, 0, 0);
	queue(fixture, MH_EVENT_KEY_DOWN, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_KEY_UP, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_KEY_DOWN, 0, 0);
	queue(fixture, MH_EVENT_KEY_UP, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) ==
		MH_LOADING_CONTINUE);

	return 0;
}

static int
check_close(
	struct mh_loading_confirmation *confirmation, struct fixture *fixture)
{
	queue(fixture, MH_EVENT_WINDOW_CLOSE, 0, 77);

	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_WAIT);
	queue(fixture, MH_EVENT_WINDOW_CLOSE, 0, 0);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_QUIT);
	queue(fixture, MH_EVENT_QUIT, 0, 77);
	CHECK(mh_loading_confirmation_step(confirmation) == MH_LOADING_QUIT);

	return 0;
}

int
main(void)
{
	static const struct mh_input_ops ops = {open_input, close_input,
		focused, event, poll_input, key_down, characters, NULL,
		mouse_grab};

	int (*checks[])(struct mh_loading_confirmation *, struct fixture *) = {
		check_keys, check_queued_and_focus, check_close};

	struct mh_input_provider provider;
	struct mh_loading_confirmation confirmation;
	struct fixture fixture;
	unsigned int i;

	for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
		memset(&fixture, 0, sizeof(fixture));
		fixture.focused = 1;
		CHECK(mh_input_provider_init(&provider, &ops, &fixture, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_loading_confirmation_open(&confirmation, &provider,
			NULL, NULL) == MH_ERROR_NONE);
		CHECK(checks[i](&confirmation, &fixture) == 0);
		mh_loading_confirmation_close(&confirmation);
		CHECK(fixture.opened == 1 && fixture.closed == 1);
	}

	puts("Loading confirmation requires a fresh key, handles focus and closes cleanly");

	return 0;
}
