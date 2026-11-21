#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_RECORDS_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_RECORDS_H

#include "client/session/records.h"

/*
 * Bind ordinary/league/network local records before simulation. NULL directory
 * selects the configured per-user Motorhead storage; explicit directories must
 * exist. Diagnostics leave this unopened unless a record directory was
 * requested. This convenience entry point selects filesystem storage.
 */
enum mh_error_code	mh_race_records_open(struct mh_race_context *context,
	struct mh_field *field, const char *directory,
	struct mh_error *error_out);
#endif
