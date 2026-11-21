#ifndef MH_ASSET_PATHS_CATALOG_H
#define MH_ASSET_PATHS_CATALOG_H

#include "engine/assets/path_directories.h"

/* Installed definitions. *_NAME values are relative to Game. */
#define MH_ASSET_PROFILE_EXTENSION ".adp"
#define MH_ASSET_TRACK_EXTENSION ".trk"

#define MH_ASSET_CAR_CONFIG_NAME_FORMAT "car%02u.car"
#define MH_ASSET_CAR_CONFIG_FORMAT                                             \
	MH_ASSET_GAME_PREFIX MH_ASSET_CAR_CONFIG_NAME_FORMAT
#define MH_ASSET_TRACK_CONFIG_NAME_FORMAT "Track%u.trk"
#define MH_ASSET_TRACK_CONFIG_FORMAT                                           \
	MH_ASSET_GAME_PREFIX MH_ASSET_TRACK_CONFIG_NAME_FORMAT
#define MH_ASSET_KEYBOARD_CONTROLS_NAME "Keyboard.clo"
#define MH_ASSET_KEYBOARD_CONTROLS                                             \
	MH_ASSET_GAME_PREFIX MH_ASSET_KEYBOARD_CONTROLS_NAME

/* Original CPU profiles and portrait directory. */
#define MH_ASSET_LEAGUE_DIR "League"
#define MH_ASSET_PROFILES_SUBDIR "Profiles"
#define MH_ASSET_PROFILES_DIR MH_ASSET_LEAGUE_DIR "/" MH_ASSET_PROFILES_SUBDIR
#define MH_ASSET_PROFILE_FORMAT MH_ASSET_PROFILES_DIR "/%s"
#define MH_ASSET_PROFILE_PICTURES_DIR MH_ASSET_PROFILES_DIR "/Gfx"
#define MH_ASSET_CPU_PROFILE_NAME_FORMAT "CPU%u.adp"

#endif
