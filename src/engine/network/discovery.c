#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/discovery.h"

#define DISCOVERY_BYTE_MAX 255

static int
label_valid(const char *text, int allow_empty)
{
	unsigned int i;

	if (!allow_empty && text[0] == 0)
		return 0;

	for (i = 0; i < MH_DISCOVERY_LABEL_BYTES; i++) {
		if (text[i] == 0)
			return 1;

		if ((unsigned char)text[i] < 32)
			return 0;
	}

	return 0;
}

static int
advertisement_valid(const struct mh_discovery_advertisement *game)
{
	return game != NULL && label_valid(game->name, 1) &&
		label_valid(game->track, 0) && game->players >= 1 &&
		game->players <= MH_NET_PLAYERS && game->password <= 1 &&
		game->racing <= 1 && game->mode <= MH_NET_MODE_ELIMINATION &&
		game->laps >= 1 && game->laps <= DISCOVERY_BYTE_MAX &&
		game->car_count >= 1 && game->car_count <= DISCOVERY_BYTE_MAX;
}

static int
catalog_game_valid(const struct mh_discovery_advertisement *game)
{
	if (game == NULL)
		return 0;

	if (!game->has_roster)
		return advertisement_valid(game);

	return game->has_roster == 1 && label_valid(game->name, 1) &&
		label_valid(game->track, 0) && game->roster.player_count >= 1 &&
		game->roster.player_count <= MH_NET_WIRE_PLAYERS &&
		game->players <= game->roster.player_count &&
		game->password <= MH_DISCOVERY_PASSWORD_UNKNOWN &&
		game->racing <= 1 && game->mode <= DISCOVERY_BYTE_MAX &&
		game->laps <= DISCOVERY_BYTE_MAX &&
		game->car_count <= DISCOVERY_BYTE_MAX;
}

enum mh_error_code
mh_discovery_collect(struct mh_discovery_catalog *catalog, unsigned long ipv4,
	unsigned int port, const struct mh_discovery_advertisement *game,
	struct mh_error *error_out)
{
	unsigned int i;

	if (catalog == NULL || catalog->count > MH_DISCOVERY_SESSIONS ||
		ipv4 == 0 || ipv4 >= MH_U32_MAX || port == 0 ||
		port > MH_U16_MAX || !catalog_game_valid(game))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid discovered endpoint");

	for (i = 0; i < catalog->count; i++)
		if (catalog->entries[i].ipv4 == ipv4 &&
			catalog->entries[i].port == port)
			break;

	if (i == MH_DISCOVERY_SESSIONS)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	catalog->entries[i].ipv4 = ipv4;
	catalog->entries[i].port = port;
	catalog->entries[i].game = *game;

	if (i == catalog->count)
		catalog->count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
