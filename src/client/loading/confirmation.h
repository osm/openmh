#ifndef MH_CLIENT_LOADING_CONFIRMATION_H
#define MH_CLIENT_LOADING_CONFIRMATION_H

#include "interfaces/input/input.h"

enum mh_loading_action {
	MH_LOADING_WAIT,
	MH_LOADING_CONTINUE,
	MH_LOADING_QUIT
};

/* Own input while the loading window is visible; no race clock is sampled. */
struct mh_loading_confirmation {
	struct mh_input			input;
	const struct mh_input_provider	*provider;
	unsigned long			window;
	int				armed, confirmed;
};

enum mh_error_code	mh_loading_confirmation_open(
	struct mh_loading_confirmation *confirmation,
	const struct mh_input_provider *provider, struct mh_window *window,
	struct mh_error *error_out);
void			mh_loading_confirmation_close(
	struct mh_loading_confirmation *confirmation);

/* Drain queued startup input and require release before a fresh key press. */
enum mh_loading_action	mh_loading_confirmation_step(
	struct mh_loading_confirmation *confirmation);

#endif
