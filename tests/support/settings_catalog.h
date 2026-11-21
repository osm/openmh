#ifndef MH_TEST_SETTINGS_CATALOG_H
#define MH_TEST_SETTINGS_CATALOG_H

#include "engine/settings/config.h"

static const char *const settings_cars[] = {
	"Car Alpha", "Car Beta", "CAR BETA", "1234567890123456789012345678901"};
static const char *const settings_tracks[] = {"Track Alpha", "Track Beta",
	"TRACK BETA", "1234567890123456789012345678901"};
static const char *const settings_horns[] = {
	"Honker1", "Honker2", "HONKER2", "Honker4"};
static const char *const settings_renderers[] = {
	"Software", "3dfx Glide", "3DFX GLIDE"};
static const struct mh_settings_catalog settings_catalog = {{settings_cars, 4},
	{settings_tracks, 4}, {settings_horns, 4}, {settings_renderers, 3}};

#endif
