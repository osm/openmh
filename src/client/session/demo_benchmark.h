#ifndef MH_SESSION_DEMO_BENCHMARK_H
#define MH_SESSION_DEMO_BENCHMARK_H

#include "client/session/race_context.h"
#include "engine/demo/benchmark.h"
#include "interfaces/clock/clock.h"
#include "interfaces/storage/user_data.h"

/* Replace Fps.txt in the supplied writable view with one completed run. */
enum mh_error_code	mh_demo_benchmark_save(const struct mh_user_data *data,
	const struct mh_clock_source *clock,
	const struct mh_demo_benchmark *benchmark, const char *recording,
	const struct mh_race_context *context, int muted,
	struct mh_error *error_out);

#endif
