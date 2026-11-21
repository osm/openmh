#ifndef MH_TEST_INPUT_PROVIDER_H
#define MH_TEST_INPUT_PROVIDER_H

#include "interfaces/input/backend.h"

/*
 * Selected wrapper that counts dispatch and can supply a host-queued event.
 * Keep it at the same address until all input owners using it have closed.
 */
struct mh_test_input {
	struct mh_input_provider	provider;
	const struct mh_input_provider	*native;
	unsigned int	opened, closed, events, samples, layouts, keys;
	struct mh_event			event;
	int				pending, fail_open;
};

enum mh_error_code	mh_test_input_init(struct mh_test_input *input,
	struct mh_error *error_out);

#endif
