#ifndef MH_MENU_CHOICES_H
#define MH_MENU_CHOICES_H

/*
 * Zero initialization keeps every choice enabled. Restrictions only increase.
 */
enum mh_menu_availability {
	MH_MENU_ENABLED,
	MH_MENU_DISABLED,
	MH_MENU_HIDDEN
};

/* Borrowed view into one configuration, in a page's original row order. */
struct mh_menu_choices {
	const enum mh_menu_availability	*states;
	const unsigned int		*items;
	unsigned int			count;
};

/* NULL means the unrestricted page, for diagnostic tools. */
enum mh_menu_availability	mh_menu_choice_state(
	const struct mh_menu_choices *choices, unsigned int row);
unsigned int			mh_menu_choice_first(
	const struct mh_menu_choices *choices, unsigned int selected);
unsigned int			mh_menu_choice_move(
	const struct mh_menu_choices *choices, unsigned int selected,
	unsigned int count, int forward, int wrap);

unsigned int	mh_menu_choice_navigate(const struct mh_menu_choices *choices,
	unsigned int *selected, unsigned int count, int forward);

#endif
