#ifndef MH_ASSETS_DEMO_TRACK_H
#define MH_ASSETS_DEMO_TRACK_H

#include "engine/assets/demo_definition.h"
#include "engine/assets/demo_layout.h"
#include "engine/core/file.h"
#include "interfaces/catalog/track.h"

/* Decode the original TBL05 binary record to the ordinary catalog value. */
enum mh_error_code	mh_track_demo_decode(const struct mh_file *source,
	enum mh_demo_track_layout layout, struct mh_track *output,
	struct mh_error *error_out);

/*
 * Shared bounded normalization for the decoder and openmotorex's text-file
 * writer.
 */
enum mh_error_code	mh_track_demo_definition(const struct mh_file *source,
	enum mh_demo_track_layout layout, struct mh_definition_text *output,
	struct mh_error *error_out);

#endif
