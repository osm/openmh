#ifndef MH_ASSETS_PREPARE_DEMO_TRACK_H
#define MH_ASSETS_PREPARE_DEMO_TRACK_H

#include "engine/assets/demo_layout.h"
#include "engine/core/error.h"
#include "engine/core/file.h"

/*
 * A verified package selects the layout. Convert its TBL05 binary track to
 * canonical TBL05 TRK text, retaining paths and authored values. MPlayer's
 * CDTrack=4 loader override is applied. Output unchanged on failure; caller
 * frees output.data.
 */
enum mh_error_code	mh_demo_track_prepare(const struct mh_file *source,
	enum mh_demo_track_layout layout, struct mh_file *output,
	struct mh_error *error_out);

#endif
