#include <string.h>

#include "backends/input/sdl_provider.h"
#include "input_provider.h"

static enum mh_error_code
mouse_grab(void *context, void *token, int enabled, struct mh_error *error_out)
{
	struct mh_test_input *input;

	input = context;

	return input->native->ops->mouse_grab(
		input->native->context, token, enabled, error_out);
}

static enum mh_error_code
open_input(void *context, struct mh_window *window, int grab_mouse,
	void **output, struct mh_error *error_out)
{
	struct mh_test_input *input;
	enum mh_error_code status;

	input = context;

	if (input->fail_open)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "host input open failed");

	status = input->native->ops->open(
		input->native->context, window, grab_mouse, output, error_out);

	if (status == MH_ERROR_NONE)
		input->opened++;

	return status;
}

static void
close_input(void *context, void *token)
{
	struct mh_test_input *input;

	input = context;
	input->closed++;
	input->native->ops->close(input->native->context, token);
}

static int
focused(void *context, const void *token)
{
	struct mh_test_input *input;

	input = context;

	return input->native->ops->focused(input->native->context, token);
}

static int
event(void *context, void *token, struct mh_event *output)
{
	struct mh_test_input *input;

	input = context;
	input->events++;

	if (input->pending) {
		*output = input->event;
		input->pending = 0;
		return 1;
	}

	return input->native->ops->event(input->native->context, token, output);
}

static enum mh_error_code
poll_devices(void *context, void *token, struct mh_keyboard *keyboard,
	const unsigned char down[MH_KEY_COUNT], unsigned long now_ms,
	unsigned long mode, struct mh_device_input *snapshot,
	struct mh_error *error_out)
{
	struct mh_test_input *input;

	input = context;
	input->samples++;

	return input->native->ops->poll(input->native->context, token, keyboard,
		down, now_ms, mode, snapshot, error_out);
}

static int
key_down(void *context, unsigned int scan)
{
	struct mh_test_input *input;

	input = context;
	input->keys++;

	return input->native->ops->key_down(input->native->context, scan);
}

static void
characters(void *context, const unsigned char down[MH_KEY_COUNT],
	unsigned char output[MH_KEY_COUNT])
{
	struct mh_test_input *input;

	input = context;
	input->layouts++;
	input->native->ops->characters(input->native->context, down, output);
}

enum mh_error_code
mh_test_input_init(struct mh_test_input *input, struct mh_error *error_out)
{
	static const struct mh_input_ops ops = {open_input, close_input,
		focused, event, poll_devices, key_down, characters, NULL,
		mouse_grab};

	memset(input, 0, sizeof(*input));
	input->native = mh_sdl_input_provider();

	return mh_input_provider_init(&input->provider, &ops, input, error_out);
}
