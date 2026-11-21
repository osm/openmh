#ifndef MH_EXTRACT_HOSTS_APPLICATION_H
#define MH_EXTRACT_HOSTS_APPLICATION_H

#include "extract/progress/report.h"

/* A host observer replaces terminal progress; diagnostics remain on stderr. */
int	mh_extract_main(int argc, char *argv[], mh_extract_progress progress,
	void *context);

#endif
