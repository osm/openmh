#ifndef MH_ASSETS_DEMO_PRESENTATION_H
#define MH_ASSETS_DEMO_PRESENTATION_H

#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_resource_source;

/* Native support artwork, not embedded original assets. Caller owns bytes. */
enum mh_error_code	mh_demo_results_tga(
	const struct mh_resource_source *resources, const char *root,
	unsigned int board, struct mh_file *output, struct mh_error *error_out);
enum mh_error_code	mh_demo_loading_tga(unsigned int image,
	struct mh_file *output, struct mh_error *error_out);

#endif
