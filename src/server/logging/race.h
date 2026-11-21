#ifndef MH_SERVER_LOGGING_RACE_H
#define MH_SERVER_LOGGING_RACE_H

#include "server/session/results.h"

enum {
	MH_SERVER_RACE_RECORD_BYTES = 2048
};

/* Borrowed observer; record text is valid only during the synchronous call. */
struct mh_server_race_logger {
	void	(*write)(void *context, const char *record);
	void	*context;
};

/*
 * Original tab-separated race fields, including twelve positions and trailing
 * newline. The native writer supplies the local date/time prefix. Call before
 * results_apply clears finished and before track rotation. Output is unchanged
 * on failure. Original single-byte names and padding are preserved.
 */
enum mh_error_code	mh_server_race_record(
	const struct mh_net_roster *roster,
	const struct mh_server_result results[MH_NET_WIRE_PLAYERS],
	char output[MH_SERVER_RACE_RECORD_BYTES], struct mh_error *error_out);

#endif
