#ifndef MH_ASSET_PATHS_MENUS_H
#define MH_ASSET_PATHS_MENUS_H

#include "engine/assets/path_directories.h"

/*
 * Shared menu artwork, default player picture and demo presentation screens.
 * Artwork paths are installation-relative; *_NAME values are relative to Data.
 * Previews use car/track roots; the player picture uses its profile catalog.
 */
#define MH_ASSET_MENU_BACKDROP MH_ASSET_DATA_PREFIX "back.tga"
#define MH_ASSET_MENU_NOISE MH_ASSET_DATA_PREFIX "Noise.TGA"
#define MH_ASSET_MENU_SPRITES MH_ASSET_DATA_PREFIX "MENU.SPR"
#define MH_ASSET_MENU_LAYOUT MH_ASSET_DATA_PREFIX "sprpos.dta"
#define MH_ASSET_MENU_PALETTE MH_ASSET_DATA_PREFIX "Default.ACT"
#define MH_ASSET_MENU_LINE_OBJECT_FORMAT                                       \
	MH_ASSET_DATA_PREFIX "LINEOBJ/lobj%02u.lob"
#define MH_ASSET_LAST_RACE_BACKDROP MH_ASSET_DATA_PREFIX "LastRace.TGA"
#define MH_ASSET_SERVER_BACKDROP MH_ASSET_DATA_PREFIX "Server.tga"
#define MH_ASSET_CLIENT_BACKDROP MH_ASSET_DATA_PREFIX "Client.tga"
#define MH_ASSET_LEAGUE_RESULTS MH_ASSET_DATA_PREFIX "lresults.tga"
#define MH_ASSET_LEAGUE_RESULTS_TWO MH_ASSET_DATA_PREFIX "lresults2.tga"
#define MH_ASSET_CAR_PREVIEW "data/_menuimg.tga"
#define MH_ASSET_TRACK_PREVIEW_FORMAT "%s/Data/_MenuImg.TGA"
#define MH_ASSET_PLAYER_PICTURE "player.tga"
#define MH_ASSET_LOADING_BACKDROP_NAME "Loading.TGA"
#define MH_ASSET_LOADING_BACKDROP                                              \
	MH_ASSET_DATA_PREFIX MH_ASSET_LOADING_BACKDROP_NAME
#define MH_ASSET_LOADING_ANIMATION_NAME "LoadAnim.tga"
#define MH_ASSET_LOADING_ANIMATION                                             \
	MH_ASSET_DATA_PREFIX MH_ASSET_LOADING_ANIMATION_NAME
#define MH_ASSET_RESULTS_BACKDROP_NAME "results.tga"
#define MH_ASSET_RESULTS_BACKDROP                                              \
	MH_ASSET_DATA_PREFIX MH_ASSET_RESULTS_BACKDROP_NAME
#define MH_ASSET_RESULTS_TWO_BACKDROP_NAME "results2.tga"
#define MH_ASSET_RESULTS_TWO_BACKDROP                                          \
	MH_ASSET_DATA_PREFIX MH_ASSET_RESULTS_TWO_BACKDROP_NAME
#define MH_ASSET_DEMO_SCREEN_FORMAT MH_ASSET_DATA_PREFIX "data%u.bin"
#define MH_ASSET_MPLAYER_SCREEN_FORMAT MH_ASSET_DATA_PREFIX "d3d/screen%u.tga"

#endif
