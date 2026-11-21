#ifndef MH_TEST_RECORDING_FAILURE_H
#define MH_TEST_RECORDING_FAILURE_H

#include "client/session/demo_recording.h"
#include "client/session/demo_setup.h"

int	mh_test_recording_failure(struct mh_race_context *context,
	struct mh_field *field, struct mh_demo_race *race,
	struct mh_demo_recording **recording, int menu);

#endif
