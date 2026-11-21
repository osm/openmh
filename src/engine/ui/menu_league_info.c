#include <string.h>

#include "engine/core/error.h"
#include "engine/game/league_race.h"
#include "engine/ui/menu_league_info.h"

enum mh_error_code
mh_menu_league_info_read(const struct mh_league_document *document,
	struct mh_menu_league_info *output, struct mh_error *error_out)
{
	static const unsigned int division_starts[MH_LEAGUE_GROUPS] = {
		0, 2, 10, 18};

	struct mh_menu_league_info next;
	struct mh_league_roster roster;
	const struct mh_league_entry *entry;
	const struct mh_profile *profile;
	enum mh_error_code status;
	unsigned int i, division_start;

	if (document == NULL || output == NULL ||
		memchr(document->name, 0, sizeof(document->name)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League display document");

	status = mh_league_roster(&document->league, &roster, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	next.name = document->name;
	next.created = document->created;
	next.race_index = document->league.race_index;
	next.stage = roster.stage;
	division_start = division_starts[roster.stage];

	for (i = 0; i < roster.count; i++) {
		entry = &document->league.entries[division_start + i];

		if (entry->driver >= MH_LEAGUE_ENTRIES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid League display identity");

		profile = &document->profiles[entry->driver];

		if (memchr(profile->nick, 0, sizeof(profile->nick)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid League display nickname");

		next.drivers[i].nick = profile->nick;
		next.drivers[i].color = profile->colors[0];
		next.drivers[i].points = entry->points;
		next.drivers[i].wins = entry->wins;
		next.drivers[i].second_places = entry->second_places;
		next.drivers[i].third_places = entry->third_places;

		if (entry->human) {
			next.player = profile->nick;
			next.points = entry->points;
		}
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
