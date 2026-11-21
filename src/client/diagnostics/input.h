#ifndef MH_CLIENT_DIAGNOSTICS_INPUT_H
#define MH_CLIENT_DIAGNOSTICS_INPUT_H

#include "interfaces/input/input.h"

/* Native diagnostic conveniences; portable owners supply providers. */
enum mh_error_code	mh_input_open(struct mh_input *input,
	struct mh_window *window, struct mh_error *error_out);

/* NULL input drains the native event stream without device sampling. */
int	mh_event_poll(struct mh_input *input, struct mh_event *event);

int	mh_key_down(unsigned int scan);

#endif
