#include <stddef.h>

#include "engine/ui/menu.h"
#include "engine/ui/menu_choices.h"

enum mh_menu_availability
mh_menu_choice_state(const struct mh_menu_choices *choices, unsigned int row)
{
	if (choices == NULL)
		return MH_MENU_ENABLED;

	if (row >= choices->count)
		return MH_MENU_HIDDEN;

	return choices->states[choices->items[row]];
}

unsigned int
mh_menu_choice_first(
	const struct mh_menu_choices *choices, unsigned int selected)
{
	unsigned int i;

	if (choices == NULL ||
		mh_menu_choice_state(choices, selected) == MH_MENU_ENABLED)
		return selected;

	for (i = 0; i < choices->count; i++)
		if (mh_menu_choice_state(choices, i) == MH_MENU_ENABLED)
			return i;

	/* A page with no enabled rows still accepts Escape. */

	return selected;
}

unsigned int
mh_menu_choice_move(const struct mh_menu_choices *choices,
	unsigned int selected, unsigned int count, int forward, int wrap)
{
	unsigned int row, i;

	row = selected;

	for (i = 0; i < count; i++) {
		if (forward) {
			if (row + 1 == count && !wrap)
				break;

			row = row + 1 == count ? 0 : row + 1;
		} else {
			if (row == 0 && !wrap)
				break;

			row = row == 0 ? count - 1 : row - 1;
		}

		if (mh_menu_choice_state(choices, row) == MH_MENU_ENABLED)
			return row;
	}

	return selected;
}

unsigned int
mh_menu_choice_navigate(const struct mh_menu_choices *choices,
	unsigned int *selected, unsigned int count, int forward)
{
	unsigned int next;

	next = mh_menu_choice_move(choices, *selected, count, forward, 0);

	if (next == *selected)
		return MH_MENU_SOUND_LIMIT;

	*selected = next;

	return forward ? MH_MENU_SOUND_NEXT : MH_MENU_SOUND_PREVIOUS;
}
