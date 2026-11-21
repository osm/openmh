#ifndef MH_TEST_BROWSER_LEAGUE_FIXTURE_H
#define MH_TEST_BROWSER_LEAGUE_FIXTURE_H

struct mh_league_document;

/*
 * Borrow the current menu document solely to construct isolated save fixtures.
 */
void	mh_browser_league_fixture_source(
	const struct mh_league_document *document);

#endif
