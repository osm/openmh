#include <string.h>

#include "client/loading/confirmation.h"
#include "interfaces/window/window.h"

enum mh_error_code
mh_loading_confirmation_open(struct mh_loading_confirmation *confirmation,
	const struct mh_input_provider *provider, struct mh_window *window,
	struct mh_error *error_out)
{
	if (confirmation == NULL || provider == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading input request");

	memset(confirmation, 0, sizeof(*confirmation));

	confirmation->provider = provider;
	confirmation->window = mh_window_id(window);

	return mh_input_open_from(
		provider, &confirmation->input, window, 0, error_out);
}

void
mh_loading_confirmation_close(struct mh_loading_confirmation *confirmation)
{
	mh_input_close(&confirmation->input);
}

static int
keys_released(const struct mh_input_provider *provider)
{
	unsigned int scan;

	for (scan = 0; scan < MH_KEY_COUNT; scan++)
		if (mh_input_key_down(provider, scan))
			return 0;

	return 1;
}

enum mh_loading_action
mh_loading_confirmation_step(struct mh_loading_confirmation *confirmation)
{
	struct mh_event event;
	int accepted;

	accepted = 0;

	while (mh_input_event(&confirmation->input, &event)) {
		if (event.type == MH_EVENT_QUIT)
			return MH_LOADING_QUIT;

		if (event.window != confirmation->window)
			continue;

		if (event.type == MH_EVENT_WINDOW_CLOSE)
			return MH_LOADING_QUIT;

		if (event.type == MH_EVENT_FOCUS_LOST ||
			event.type == MH_EVENT_FOCUS_GAINED ||
			event.type == MH_EVENT_INPUT_RESET) {
			confirmation->armed = confirmation->confirmed = 0;
			accepted = 0;
		}

		if (confirmation->armed && event.type == MH_EVENT_KEY_DOWN &&
			!event.repeat)
			accepted = 1;
	}

	if (!mh_input_focused(&confirmation->input)) {
		confirmation->armed = confirmation->confirmed = 0;
		return MH_LOADING_WAIT;
	}

	if (accepted)
		confirmation->confirmed = 1;

	if (confirmation->confirmed && keys_released(confirmation->provider))
		return MH_LOADING_CONTINUE;

	if (!confirmation->armed && keys_released(confirmation->provider))
		confirmation->armed = 1;

	return MH_LOADING_WAIT;
}
