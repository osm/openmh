#ifndef MH_ASSETS_PROFILE_H
#define MH_ASSETS_PROFILE_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/catalog/profile.h"

/*
 * Presentation and league vehicle names from decoded ADP text.
 * Unknown tuning fields are ignored. Vehicle names remain unresolved.
 * Legacy single-byte names; picture is a checked ASCII basename.
 * Output unchanged on failure.
 */
enum mh_error_code	mh_profile_adp_decode(const unsigned char *text,
	size_t size, struct mh_profile *output, struct mh_error *error_out);

#endif
