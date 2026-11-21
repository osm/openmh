#ifndef MH_ASSET_PATHS_EFFECTS_H
#define MH_ASSET_PATHS_EFFECTS_H

#include "engine/assets/path_directories.h"

/*
 * Shared race effects. Numbered frame formats retain their original numbering.
 */
#define MH_ASSET_SOFTWARE_GLOW MH_ASSET_CARS_PREFIX "Textures/HaloWhit.IFF"
#define MH_ASSET_SOFTWARE_HALO MH_ASSET_DATA_PREFIX "hex.iff"
#define MH_ASSET_LAMP_GLOW MH_ASSET_DATA_PREFIX "halofix.iff"
#define MH_ASSET_LAMP_FLARE MH_ASSET_DATA_PREFIX "hwhex.iff"
#define MH_ASSET_SOFTWARE_SMOKE_FORMAT                                         \
	MH_ASSET_DATA_PREFIX "smoke/smoke%02u.iff"
#define MH_ASSET_SOFTWARE_GRASS_FORMAT                                         \
	MH_ASSET_DATA_PREFIX "gras/_cgras%02u.iff"
#define MH_ASSET_SOFTWARE_GRAVEL_FORMAT                                        \
	MH_ASSET_DATA_PREFIX "grus/_grus%02u.iff"
#define MH_ASSET_SOFTWARE_SPARK_FORMAT                                         \
	MH_ASSET_DATA_PREFIX "sparks3/_spare%02u.iff"
#define MH_ASSET_HARDWARE_SMOKE_FORMAT                                         \
	MH_ASSET_DATA_PREFIX "d3d/smoke/smoke%02u.iff"
#define MH_ASSET_HARDWARE_GRASS_FORMAT                                         \
	MH_ASSET_DATA_PREFIX "d3d/grass/gras%02u.iff"
#define MH_ASSET_HARDWARE_SPARK_FORMAT                                         \
	MH_ASSET_DATA_PREFIX "d3d/sparks/spark%02u.iff"
#define MH_ASSET_FINISH_DIR MH_ASSET_DATA_PREFIX "Finish"
#define MH_ASSET_FINISH_MOTION MH_ASSET_DATA_PREFIX "Finish/finish.mot"
#define MH_ASSET_FINISH_FIRST MH_ASSET_DATA_PREFIX "Finish/first.myo"
#define MH_ASSET_FINISH_SECOND MH_ASSET_DATA_PREFIX "Finish/second.myo"
#define MH_ASSET_FINISH_THIRD MH_ASSET_DATA_PREFIX "Finish/third.myo"

#endif
