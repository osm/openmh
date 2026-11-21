#ifndef MH_CLIENT_MENUS_SOUND_H
#define MH_CLIENT_MENUS_SOUND_H

#include "engine/core/error.h"
struct mh_menus;

/* AUTO permits a missing device; captures select DISABLED explicitly. */
enum mh_menus_audio_mode {
	MH_MENUS_AUDIO_AUTO = 0,
	MH_MENUS_AUDIO_REQUIRED = 1,
	MH_MENUS_AUDIO_DISABLED = 2
};

enum mh_error_code	mh_menus_open_audio(struct mh_menus *menus,
	const char *asset_root, const char *music_root, int audio_mode,
	struct mh_error *error_out);

#endif
