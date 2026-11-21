#ifndef MH_SETTINGS_CONFIG_INTERNAL_H
#define MH_SETTINGS_CONFIG_INTERNAL_H

#include "engine/settings/config.h"

#define MH_CFG_RGB_MAX 0xffffffUL
#define MH_CFG_COLOR_BYTE 255UL
#define MH_CFG_SERIAL_PORT_MIN 1
#define MH_CFG_SERIAL_PORT_MAX 4
#define MH_CFG_CD_TRACK_MIN 2
#define MH_CFG_CD_TRACK_MAX 11

/* Shared catalog/state precondition for the CFG reader and writer. */
int	mh_cfg_catalog_valid(const struct mh_settings_catalog *catalog,
	const struct mh_settings *settings);

#endif
