#include <stddef.h>

#include "backends/input/sdl_provider.h"
#include "client/diagnostics/input.h"
#include "interfaces/input/backend.h"

/* Native conveniences for standalone diagnostic callers. */
enum mh_error_code
mh_input_open(struct mh_input *input, struct mh_window *window,
	struct mh_error *error_out)
{
	return mh_input_open_from(
		mh_sdl_input_provider(), input, window, 1, error_out);
}

int
mh_event_poll(struct mh_input *input, struct mh_event *event)
{
	return input != NULL
		? mh_input_event(input, event)
		: mh_input_provider_event(mh_sdl_input_provider(), event);
}

int
mh_key_down(unsigned int scan)
{
	return mh_input_key_down(mh_sdl_input_provider(), scan);
}
