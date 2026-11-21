#include "engine/core/limits.h"
#include "engine/game/league.h"
#include "engine/game/league_tracks.h"

#define DIVISION_GROUP_SCALE 100UL
#define SPECIAL_TRACK_FIRST 10
#define FINAL_TRACK_GROUP 4

int
mh_league_track_eligible(
	unsigned long division, unsigned int stage, int special)
{
	unsigned long group;

	if (division > MH_U32_MAX || stage > MH_LEAGUE_THIRD ||
		(special != 0 && special != 1))
		return 0;

	group = division / DIVISION_GROUP_SCALE;

	if (special)
		return division % DIVISION_GROUP_SCALE >= SPECIAL_TRACK_FIRST &&
			group == FINAL_TRACK_GROUP - stage;

	if (division % DIVISION_GROUP_SCALE >= SPECIAL_TRACK_FIRST)
		return 0;

	if (stage == MH_LEAGUE_THIRD)
		return group == 1;

	if (stage == MH_LEAGUE_SECOND)
		return group <= 2;

	if (stage == MH_LEAGUE_FIRST)
		return group <= 3;

	return group == FINAL_TRACK_GROUP;
}
