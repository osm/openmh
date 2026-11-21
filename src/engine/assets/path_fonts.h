#ifndef MH_ASSET_PATHS_FONTS_H
#define MH_ASSET_PATHS_FONTS_H

#include "engine/assets/path_directories.h"

/* Original font files and names used by demo preparation. */
#define MH_ASSET_FONT1_NAME "FONT1.FNT"
#define MH_ASSET_FONT1 MH_ASSET_DATA_PREFIX MH_ASSET_FONT1_NAME
#define MH_ASSET_FONT1S_NAME "FONT1S.FNT"
#define MH_ASSET_FONT1S MH_ASSET_DATA_PREFIX MH_ASSET_FONT1S_NAME
#define MH_ASSET_FONT2S_NAME "FONT2S.FNT"
#define MH_ASSET_FONT2S MH_ASSET_DATA_PREFIX MH_ASSET_FONT2S_NAME
#define MH_ASSET_HUD_FONT_ATLAS MH_ASSET_DATA_PREFIX "d3d/myfont.iff"

/*
 * Preserve the request spelling at the provider boundary as well as on disk.
 * Alternate providers may match references before filesystem lookup.
 */
#define MH_ASSET_FONT1_LOWERCASE MH_ASSET_DATA_PREFIX "font1.fnt"
#define MH_ASSET_FONT2_LOWERCASE MH_ASSET_DATA_PREFIX "font2.fnt"
#define MH_ASSET_FONT1S_LOWERCASE MH_ASSET_DATA_PREFIX "font1s.fnt"
#define MH_ASSET_FONT2S_LOWERCASE MH_ASSET_DATA_PREFIX "font2s.fnt"

#endif
