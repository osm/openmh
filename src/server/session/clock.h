#ifndef MH_SERVER_CLOCK_H
#define MH_SERVER_CLOCK_H

#include "server/config/config.h"

struct mh_server_clock {
	long double	now_ms;
	unsigned long	race_start_ms, finish_anchor_ms;
	int		all_finished;
};

struct mh_server_clock_sample {
	/*
	 * Current race mode, which may differ from the startup configuration.
	 */
	enum mh_server_mode	mode;

	/* Wire-slot masks; internal server bit zero never participates. */
	unsigned int		racing, finished, eliminated;

	/* First start signal from the dedicated load/start scheduler. */
	unsigned long		countdown_ms;
	long double		now_ms;
};

enum mh_server_clock_notice {
	MH_SERVER_CLOCK_NOTICE_NONE,
	MH_SERVER_CLOCK_TIME_LEFT,
	MH_SERVER_CLOCK_DID_NOT_FINISH,
	MH_SERVER_CLOCK_RACE_ENDING,
	MH_SERVER_CLOCK_BLANK
};

enum mh_server_clock_end {
	MH_SERVER_CLOCK_RUNNING,
	MH_SERVER_CLOCK_FINISHED,
	MH_SERVER_CLOCK_TIME_LIMIT
};

struct mh_server_clock_tick {
	unsigned long			elapsed_ms, remaining_seconds;
	int				started;
	enum mh_server_clock_end	end;

	/* Send finish notices before time-limit notices, as in the original. */
	enum mh_server_clock_notice	finish_notice, limit_notice;
	unsigned int			finish_recipients, limit_recipients;
};

void	mh_server_clock_reset(struct mh_server_clock *clock);

/*
 * Evaluated before transport service and progress updates. Uses the previous
 * frame's finish/elimination flags. No sends or membership changes. Zero
 * anchors and unsigned deadline wrap follow the original. Caller owns explicit
 * aborts and the separate elimination winner timer. Failure preserves clock and
 * output.
 */
enum mh_error_code	mh_server_clock_step(struct mh_server_clock *clock,
	const struct mh_server_config *config,
	const struct mh_server_clock_sample *sample,
	struct mh_server_clock_tick *output, struct mh_error *error_out);

#endif
