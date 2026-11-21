#ifndef MH_CLIENT_RACE_CONSOLE_H
#define MH_CLIENT_RACE_CONSOLE_H

#include <stdio.h>

#include "engine/simulation/field.h"

/* Text standings for CLI/inspection runs, separate from the results screen. */
void	mh_race_print_results(FILE *file, const struct mh_field *field);

#endif
