#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/network/discovery.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Discovery line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_discovery_advertisement game;
	struct mh_discovery_catalog catalog, saved;
	unsigned int i;

	memset(&game, 0, sizeof(game));
	strcpy(game.name, "Night race");
	strcpy(game.track, "RedRock");
	game.players = 3;
	game.password = 1;
	game.racing = 1;
	game.mode = 1;
	game.laps = 5;
	game.car_count = 10;
	memset(&catalog, 0, sizeof(catalog));

	for (i = 0; i < 12; i++)
		CHECK(mh_discovery_collect(&catalog, 0x7f000001UL, 1998 + i,
			&game, NULL) == MH_ERROR_NONE);

	CHECK(catalog.count == 9);
	game.players = 8;
	CHECK(mh_discovery_collect(&catalog, 0x7f000001UL, 2001, &game, NULL) ==
			MH_ERROR_NONE &&
		catalog.count == 9);
	CHECK(catalog.entries[3].game.players == 8 &&
		catalog.entries[4].game.players == 3);
	saved = catalog;
	CHECK(mh_discovery_collect(&catalog, 0, 1998, &game, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&catalog, &saved, sizeof(catalog)) == 0);
	CHECK(mh_discovery_collect(&catalog, 0x7f000001UL, 65536, &game,
		NULL) != MH_ERROR_NONE &&
		memcmp(&catalog, &saved, sizeof(catalog)) == 0);

	return 0;
}
