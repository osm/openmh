#include <stdio.h>
#include <string.h>

#include "engine/assets/demo_drivers.h"
#include "engine/assets/path_menus.h"

struct demo_driver {
	const char	*car;
	unsigned long	paint[3];
};

/* Fixed car and paint assignments for each demo edition. */
static const struct demo_driver drivers[] = {
	{"Kobra X3", {0x4b5b22UL, 0xc04e00UL, 0xffffffUL}},
	{"Avalanche", {0x3269b5UL, 0xff7000UL, 0xe6e6e6UL}},
	{"Titan", {0xc8c8c8UL, 0x413f3cUL, 0x000000UL}},
	{"Kobra X3", {0x0a1474UL, 0xdc180aUL, 0xdcdcdcUL}},
	{"Kobra X3", {0x67523fUL, 0x38291dUL, 0xc3ad94UL}},
	{"Avalanche", {0x0a3d45UL, 0x957d66UL, 0xdcdcdcUL}},
	{"Titan", {0xf0640aUL, 0x5c5c5cUL, 0x000000UL}},
	{"Avalanche", {0xdc180aUL, 0x8a0f0eUL, 0xc3ad94UL}}};

enum mh_error_code
mh_profile_demo_default(unsigned int index, struct mh_profile *output,
	struct mh_error *error_out)
{
	struct mh_profile profile;
	const struct demo_driver *driver;
	unsigned int i;

	if (output == NULL || index >= MH_DEMO_DRIVERS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo driver index");

	driver = &drivers[index];
	memset(&profile, 0, sizeof(profile));
	strcpy(profile.name, "Computer Player");
	sprintf(profile.nick, "CPU%u", index + 1);
	strcpy(profile.picture, MH_ASSET_PLAYER_PICTURE);
	strcpy(profile.horn, "None");

	for (i = 0; i < 4; i++)
		strcpy(profile.cars[i], driver->car);

	profile.colors[0] = 0x00ffffUL;

	for (i = 0; i < 3; i++)
		profile.colors[i + 1] = driver->paint[i];

	profile.color_fields = MH_PROFILE_ALL_COLORS;
	profile.league_fields = MH_PROFILE_LEAGUE_FIELDS;
	profile.aggressiveness = profile.eagerness = .5;
	*output = profile;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
