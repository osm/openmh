#ifndef MH_CLIENT_ACTIVITY_H
#define MH_CLIENT_ACTIVITY_H

#include "client/application/status.h"
#include "engine/core/error.h"

#define MH_ACTIVITY_ERROR_SIZE 512

struct mh_clock_source;

struct mh_activity_ops {
	/* One frame or transition; error text is copied before returning. */
	enum mh_app_status (*step)(
		void	*context, unsigned long *delay, struct mh_error *error);

	/*
	 * Release resources; cancelling an unfinished activity does not finish
	 * it.
	 */
	void		(*close)(void *context);
};

/* Initialize before first use. Callback tables must outlive their activity. */
struct mh_activity {
	void				*context;
	const struct mh_activity_ops	*ops;
	enum mh_app_status		status;
	unsigned long			delay;
	struct mh_error			error;
	char				error_text[MH_ACTIVITY_ERROR_SIZE];
};

void	mh_activity_init(struct mh_activity *activity);
int	mh_activity_is_open(const struct mh_activity *activity);

/* Transfer context ownership on success; preserve both objects on failure. */
enum mh_error_code	mh_activity_start(struct mh_activity *activity,
	void *context, const struct mh_activity_ops *ops,
	struct mh_error *error_out);
enum mh_app_status	mh_activity_step(struct mh_activity *activity);
unsigned long		mh_activity_delay(const struct mh_activity *activity);

/* Error remains valid after close, until the activity is initialized/reused. */
enum mh_error_code	mh_activity_error(const struct mh_activity *activity,
	struct mh_error *error_out);
void			mh_activity_close(struct mh_activity *activity);

/*
 * Blocking runner borrows clock for this call and closes the activity when
 * done. A missing clock or one without delay is rejected before any step;
 * the caller still owns the open activity. Returned error text is borrowed
 * until the next run on this main-thread path. Frame-driven hosts use
 * step/error/close instead and never need a clock with blocking delay.
 */
enum mh_error_code	mh_activity_run_with_clock(struct mh_activity *activity,
	const struct mh_clock_source *clock, struct mh_error *error_out);

#endif
