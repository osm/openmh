#ifndef MH_INTERFACES_CLOCK_BACKEND_H
#define MH_INTERFACES_CLOCK_BACKEND_H

#include "interfaces/clock/clock.h"

/*
 * A source borrows its callbacks and context from its owner. They remain live
 * until every source user has finished. No allocation or device is implicit;
 * the implementation owns any resources behind context and releases them last.
 * Callbacks are serialized by the owner unless it promises thread safety.
 */
struct mh_clock_source {
	const struct mh_clock_ops	*ops;
	void				*context;
};

struct mh_clock_ops {
	struct mh_clock_counter	(*counter)(void *context);
	struct mh_clock_counter	(*frequency)(void *context);
	unsigned long		(*ticks)(void *context);

	/*
	 * Optional native pacing; a host-driven backend may omit blocking wait.
	 */
	void	(*delay)(void *context, unsigned long milliseconds);

	/* Optional local calendar; independent of counter/pacing support. */
	enum mh_error_code (*date)(void *context, struct mh_clock_date *output,
		struct mh_error	*error_out);
};

/* Initialize before sharing. Invalid callbacks leave source unchanged. */
enum mh_error_code	mh_clock_source_init(struct mh_clock_source *source,
	const struct mh_clock_ops *ops, void *context,
	struct mh_error *error_out);

#endif
