#ifndef MH_ASSET_PATHS_TRACKS_H
#define MH_ASSET_PATHS_TRACKS_H

#include "engine/assets/path_directories.h"

/*
 * Track-local texture/AI formats take their base path from track data.
 * Collision recovery locations are tied to the original executable tables.
 */
#define MH_ASSET_TRACK_OBJECT_FORMAT "%s/Objects/%s"
#define MH_ASSET_TRACK_HARDWARE_PREFIX "Hardware/"
#define MH_ASSET_MODEL_EXTENSION ".myo"

#define MH_ASSET_MATERIALS MH_ASSET_DATA_PREFIX "Material.mat"
#define MH_ASSET_TRACK_TEXTURES_PRIMARY_FORMAT "%s/Packed/tex1.pdi"
#define MH_ASSET_TRACK_TEXTURES_SECONDARY_FORMAT "%s/Packed/tex2.pdi"
#define MH_ASSET_TRACK_REMAP_FORMAT "%s/Packed/remap%u%s.bin"
#define MH_ASSET_TRACK_AI_FORMAT "%s%sai\\ai.dat"
#define MH_ASSET_GOLDBRIDGE_OBJECTS_DIR MH_ASSET_TRACKS_PREFIX "Track8/Objects"
#define MH_ASSET_ATLANTIKA_OBJECTS_DIR MH_ASSET_TRACKS_PREFIX "Track4/Objects"
#define MH_ASSET_NEOCITY_OBJECTS_DIR MH_ASSET_TRACKS_PREFIX "Track3/Objects"
#define MH_ASSET_GOLDBRIDGE_COLLISION_NAME "goldbridge.col"
#define MH_ASSET_ATLANTIKA_COLLISION_NAME "atlantika.col"
#define MH_ASSET_NEOCITY_COLLISION_NAME "neocity.col"

#endif
