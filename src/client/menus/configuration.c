#include "client/menus/internal.h"

struct feature_requirement {
	unsigned int		content;
	enum mh_menu_feature	feature;
};

static void
restrict_feature(struct mh_menu_configuration *config,
	enum mh_menu_feature feature, enum mh_menu_availability state)
{
	if (config->items[feature] < state)
		config->items[feature] = state;
}

void
mh_menus_configuration_resolve(struct mh_menu_configuration *config,
	const struct mh_content_edition *edition, int network, int sound,
	int feedback)
{
	static const struct feature_requirement requirements[] = {
		{MH_CONTENT_NETWORK, MH_MENU_FEATURE_NETWORK},
		{MH_CONTENT_RANKINGS, MH_MENU_FEATURE_RANKINGS},
		{MH_CONTENT_CREDITS, MH_MENU_FEATURE_CREDITS},
		{MH_CONTENT_PERSONAL, MH_MENU_FEATURE_PERSONAL},
		{MH_CONTENT_RACE_MODES, MH_MENU_FEATURE_RACE},
		{MH_CONTENT_RACE_MODES, MH_MENU_FEATURE_TIME_ATTACK},
		{MH_CONTENT_LEAGUES, MH_MENU_FEATURE_LEAGUE},
		{MH_CONTENT_RECORDINGS, MH_MENU_FEATURE_GHOST},
		{MH_CONTENT_ATTRACT, MH_MENU_FEATURE_ATTRACT}};

	static const enum mh_menu_feature races[] = {MH_MENU_FEATURE_QUICK,
		MH_MENU_FEATURE_RACE, MH_MENU_FEATURE_LEAGUE,
		MH_MENU_FEATURE_TIME_ATTACK, MH_MENU_FEATURE_GHOST};

	static const enum mh_menu_feature options[] = {MH_MENU_FEATURE_PERSONAL,
		MH_MENU_FEATURE_GRAPHICS, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_SOUND, MH_MENU_FEATURE_DIFFICULTY};

	unsigned int i, features;

	features =
		edition == NULL ? MH_CONTENT_FEATURES_ALL : edition->features;

	for (i = 0; i < sizeof(requirements) / sizeof(requirements[0]); i++)
		if (!(features & requirements[i].content))
			restrict_feature(config, requirements[i].feature,
				MH_MENU_HIDDEN);

	/* Hosts may retain a disabled label for an unavailable service. */
	if (!network &&
		config->items[MH_MENU_FEATURE_NETWORK] == MH_MENU_ENABLED)
		restrict_feature(
			config, MH_MENU_FEATURE_NETWORK, MH_MENU_HIDDEN);

	if (!sound)
		restrict_feature(
			config, MH_MENU_FEATURE_SOUND, MH_MENU_DISABLED);

	/* A preference may be saved before connecting a wheel. */
	if (!feedback)
		restrict_feature(
			config, MH_MENU_FEATURE_FEEDBACK, MH_MENU_DISABLED);

	for (i = 0; i < sizeof(races) / sizeof(races[0]); i++)
		restrict_feature(config, races[i],
			config->items[MH_MENU_FEATURE_SINGLE]);

	for (i = 0; i < sizeof(options) / sizeof(options[0]); i++)
		restrict_feature(config, options[i],
			config->items[MH_MENU_FEATURE_OPTIONS]);

	restrict_feature(config, MH_MENU_FEATURE_FEEDBACK,
		config->items[MH_MENU_FEATURE_CONTROLS]);
}

int
mh_menus_feature_enabled(const struct mh_menu_configuration *config,
	enum mh_menu_feature feature)
{
	return (unsigned int)feature < MH_MENU_FEATURE_COUNT &&
		config->items[feature] == MH_MENU_ENABLED;
}

static void
bind_choices(struct mh_menu_choices *choices,
	const struct mh_menu_configuration *config, const unsigned int *items,
	unsigned int count)
{
	choices->states = config->items;

	choices->items = items;
	choices->count = count;
}

void
mh_menus_configuration_bind(struct mh_menus *menus)
{
	static const unsigned int controls[] = {MH_MENU_FEATURE_FEEDBACK,
		MH_MENU_FEATURE_CONTROLS, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_CONTROLS, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_CONTROLS, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_CONTROLS, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_CONTROLS, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_CONTROLS, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_CONTROLS};

	static const unsigned int main[] = {MH_MENU_FEATURE_SINGLE,
		MH_MENU_FEATURE_NETWORK, MH_MENU_FEATURE_RANKINGS,
		MH_MENU_FEATURE_OPTIONS, MH_MENU_FEATURE_CREDITS};

	static const unsigned int options[] = {MH_MENU_FEATURE_PERSONAL,
		MH_MENU_FEATURE_GRAPHICS, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_SOUND, MH_MENU_FEATURE_DIFFICULTY};

	static const unsigned int single[] = {MH_MENU_FEATURE_QUICK,
		MH_MENU_FEATURE_RACE, MH_MENU_FEATURE_LEAGUE,
		MH_MENU_FEATURE_TIME_ATTACK, MH_MENU_FEATURE_GHOST};

	static const unsigned int compact[] = {MH_MENU_FEATURE_QUICK,
		MH_MENU_FEATURE_SINGLE, MH_MENU_FEATURE_SINGLE,
		MH_MENU_FEATURE_SINGLE, MH_MENU_FEATURE_DIFFICULTY,
		MH_MENU_FEATURE_SINGLE, MH_MENU_FEATURE_CONTROLS,
		MH_MENU_FEATURE_SOUND, MH_MENU_FEATURE_GRAPHICS,
		MH_MENU_FEATURE_GRAPHICS, MH_MENU_FEATURE_EXIT};

	bind_choices(&menus->controls_choices, &menus->configuration, controls,
		sizeof(controls) / sizeof(controls[0]));
	bind_choices(&menus->main_choices, &menus->configuration, main,
		sizeof(main) / sizeof(main[0]));
	bind_choices(&menus->options_choices, &menus->configuration, options,
		sizeof(options) / sizeof(options[0]));
	bind_choices(&menus->single_choices, &menus->configuration, single,
		sizeof(single) / sizeof(single[0]));
	bind_choices(&menus->compact_choices, &menus->configuration, compact,
		sizeof(compact) / sizeof(compact[0]));

	menus->main.selected = mh_menu_choice_first(
		&menus->main_choices, menus->main.selected);
	menus->options.selected = mh_menu_choice_first(
		&menus->options_choices, menus->options.selected);
	menus->single.selected = mh_menu_choice_first(
		&menus->single_choices, menus->single.selected);
	menus->compact_row = mh_menu_choice_first(
		&menus->compact_choices, menus->compact_row);
}

int
mh_menus_mode_enabled(const struct mh_menus *menus, unsigned int mode)
{
	enum mh_menu_feature feature;

	switch (mode) {
	case MH_GAME_QUICK_RACE:
		return mh_menus_feature_enabled(
			&menus->configuration, MH_MENU_FEATURE_QUICK) ||
			mh_menus_feature_enabled(
				&menus->configuration, MH_MENU_FEATURE_RACE);

	case MH_GAME_LEAGUE:
		feature = MH_MENU_FEATURE_LEAGUE;
		break;

	case MH_GAME_TIME_ATTACK:
		feature = MH_MENU_FEATURE_TIME_ATTACK;
		break;

	case MH_GAME_GHOST:
	case MH_GAME_DEMO:
		feature = MH_MENU_FEATURE_GHOST;
		break;

	case MH_GAME_NETWORK:
		feature = MH_MENU_FEATURE_NETWORK;
		break;

	case MH_GAME_ATTRACT:
		feature = MH_MENU_FEATURE_ATTRACT;
		break;

	default:
		return 0;
	}

	return mh_menus_feature_enabled(&menus->configuration, feature);
}

void
mh_menus_restrict_mode(struct mh_menus *menus)
{
	/*
	 * A saved quick-start mode must not bypass today's menu configuration.
	 */
	if (!mh_menus_mode_enabled(menus, menus->single.mode))
		menus->single.mode = MH_GAME_QUICK_RACE;
}

static int
setup_enabled(const struct mh_menus *menus)
{
	unsigned int mode;

	mode = menus->single.mode;

	/* The departure callback commits the newly selected mode later. */
	if (menus->menu.current == &menus->single_page &&
		menus->single.selected != 0)
		mode = menus->single.selected - 1;

	if (mode == MH_GAME_QUICK_RACE)
		return mh_menus_feature_enabled(
			&menus->configuration, MH_MENU_FEATURE_RACE);

	return mh_menus_mode_enabled(menus, mode);
}

int
mh_menus_page_enabled(
	const struct mh_menus *menus, const struct mh_menu_page *page)
{
	enum mh_menu_feature feature;
	unsigned int i;

	if (page == &menus->main_page)
		return 1;

	if (page == NULL || page->input == NULL)
		return 0;

	if (page == &menus->launch_page)
		return mh_menus_mode_enabled(menus, menus->launch_mode);

	for (i = 0; i < MH_MENUS_LEAGUE_COUNT; i++)
		if (page == &menus->league_page[i])
			return mh_menus_feature_enabled(
				&menus->configuration, MH_MENU_FEATURE_LEAGUE);

	if (page == &menus->single_page)
		feature = MH_MENU_FEATURE_SINGLE;
	else if (page == &menus->multi_page)
		feature = MH_MENU_FEATURE_NETWORK;
	else if (page == &menus->rankings_page)
		feature = MH_MENU_FEATURE_RANKINGS;
	else if (page == &menus->options_page)
		feature = MH_MENU_FEATURE_OPTIONS;
	else if (page == &menus->credits_page)
		feature = MH_MENU_FEATURE_CREDITS;
	else if (page == &menus->personal_page)
		feature = MH_MENU_FEATURE_PERSONAL;
	else if (page == &menus->graphics_page)
		feature = MH_MENU_FEATURE_GRAPHICS;
	else if (page == &menus->controls_page)
		feature = MH_MENU_FEATURE_CONTROLS;
	else if (page == &menus->sound_page)
		feature = MH_MENU_FEATURE_SOUND;
	else if (page == &menus->ghost_page || page == &menus->save_page)
		feature = MH_MENU_FEATURE_GHOST;
	else if (page == &menus->car_page || page == &menus->race_page)
		return setup_enabled(menus);
	else if (page == &menus->attract_page)
		feature = MH_MENU_FEATURE_ATTRACT;
	else if (page == &menus->exit_page)
		feature = MH_MENU_FEATURE_EXIT;
	else
		return 0;

	return mh_menus_feature_enabled(&menus->configuration, feature);
}

void
mh_menus_request(struct mh_menu *menu, struct mh_menu_page *page)
{
	struct mh_menus *menus;

	menus = menu->context;

	if (!mh_menus_page_enabled(menus, page)) {
		mh_menus_sound_event(menus, MH_MENU_SOUND_LIMIT);
		return;
	}

	mh_menu_request(menu, page);
}
