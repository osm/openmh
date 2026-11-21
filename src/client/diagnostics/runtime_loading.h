#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_LOADING_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_LOADING_H

#include "client/session/loading.h"
#include "client/session/options.h"

/* Native service composition for interactive diagnostic launchers. */
struct mh_diagnostic_loading {
	struct mh_session_options	options;
	struct mh_session_loading	screen;
};

void	mh_diagnostic_loading_init(struct mh_diagnostic_loading *loading,
	const char *root, int no_records, const char *record_directory);

#endif
