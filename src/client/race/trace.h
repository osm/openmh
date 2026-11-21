#ifndef MH_CLIENT_RACE_TRACE_H
#define MH_CLIENT_RACE_TRACE_H

#include <stdio.h>

#include "engine/simulation/field.h"

/*
 * Stable tab-separated diagnostic records; no simulation state is changed.
 * NULL streams disable tracing. The caller checks ferror() after writing.
 */
void	mh_race_trace_car(FILE *file, const struct mh_drive *drive);
void	mh_race_trace_field(FILE *file, const struct mh_field *field);

#endif
