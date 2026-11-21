#ifndef MH_LEAGUE_TRACKS_H
#define MH_LEAGUE_TRACKS_H

/*
 * League track-list filters. Stage is 0..3; special selects division remainders
 * >=10 instead of ordinary remainders. Invalid inputs are ineligible. No asset
 * catalog or league state is mutated.
 */
int	mh_league_track_eligible(unsigned long division, unsigned int stage,
	int special);

#endif
