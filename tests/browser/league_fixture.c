/* Completed-save fixtures exercise presentation, not driving or race awards. */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "engine/storage/league_codec.h"
#include "league_fixture.h"

static const struct mh_league_document *source;

void	mh_browser_flow_fixture_bytes(const unsigned char *bytes, size_t size);

void
mh_browser_league_fixture_source(const struct mh_league_document *document)
{
	source = document;
}

EMSCRIPTEN_KEEPALIVE int
mh_browser_flow_make_league(unsigned int slot, unsigned int races, int other)
{
	struct mh_league_document document;
	struct mh_league_entry human;
	struct mh_file bytes;
	unsigned int i;
	enum mh_error_code status;

	if (source == NULL || slot >= MH_LEAGUE_ENTRIES || races > 6)
		return MH_ERROR_INVALID;

	document = *source;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++)
		if (document.league.entries[i].human)
			break;

	if (i == MH_LEAGUE_ENTRIES)
		return MH_ERROR_INVALID;

	human = document.league.entries[i];
	document.league.entries[i] = document.league.entries[slot];
	document.league.entries[slot] = human;
	document.league.race_index = races;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		document.league.entries[i].points = 0;
		document.league.entries[i].wins = 0;
		document.league.entries[i].second_places = 0;
		document.league.entries[i].third_places = 0;
	}

	strcpy(document.name, other ? "Other" : "Default");
	status = mh_league_bytes_encode(
		&document, other ? "other.lgf" : "default.lgf", &bytes, NULL);

	if (status == MH_ERROR_NONE) {
		mh_browser_flow_fixture_bytes(bytes.data, bytes.size);
		free(bytes.data);
	}

	return status;
}
