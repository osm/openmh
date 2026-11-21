#ifndef MH_ASSET_PATHS_MOVIES_H
#define MH_ASSET_PATHS_MOVIES_H

#include "engine/assets/path_directories.h"

/* Original DIVI movie containers, including source-disc and demo layouts. */
#define MH_ASSET_RETAIL_MOVIES_NAME "movies.pak"
#define MH_ASSET_RETAIL_MOVIES                                                 \
	MH_ASSET_MOVIES_PREFIX MH_ASSET_RETAIL_MOVIES_NAME
#define MH_ASSET_DISC_MOVIES "movies/" MH_ASSET_RETAIL_MOVIES_NAME
#define MH_ASSET_DEMO_MOVIES MH_ASSET_MOVIES_PREFIX "demo.pak"
#define MH_ASSET_DEMO2_MOVIES MH_ASSET_MOVIES_PREFIX "demo2.pak"

#endif
