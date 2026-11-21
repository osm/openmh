/* Observe League menus and season presentation without changing their state. */
#include "client/menus/internal.h"
#include "client/session/season_screen.h"
#include "league.h"
#include "league_fixture.h"

void			mh_browser_flow_league(unsigned int stage,
	unsigned long race, unsigned long points, unsigned int car,
	unsigned int tracks, unsigned int cars);
void			mh_browser_flow_season(int active);
enum mh_app_status	__real_mh_season_screen_step(
	struct mh_season_screen *screen);

void
mh_browser_flow_league_menu(const struct mh_menus *menus)
{
	const struct mh_menu_league_info *info;
	unsigned int car;

	if (menus->leagues == NULL || menus->league.count == 0)
		return;

	mh_browser_league_fixture_source(&mh_leagues_catalog(menus->leagues)
			->entries[menus->league.selected]
			.document);
	info = &menus->league_info[menus->league.selected];
	car = menus->menu.current == &menus->car_page ? menus->car.car
						: menus->single.car;
	mh_browser_flow_league(info->stage, info->race_index, info->points,
		menus->car_numbers[car], menus->track_unlocks.unlocked,
		menus->car_unlocks.unlocked);
}

enum mh_app_status
__wrap_mh_season_screen_step(struct mh_season_screen *screen)
{
	enum mh_app_status status;

	status = __real_mh_season_screen_step(screen);
	mh_browser_flow_season(status == MH_APP_RUNNING);

	return status;
}
