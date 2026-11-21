#include "engine/core/error.h"
#include "engine/ui/menu_position.h"

#define MIX_BIAS (MH_MENU_POSITION_MIX_MAX - 1)

static long
interpolate(long first, long last, unsigned int mix)
{
	long product;

	product = (last - first) * (long)mix;

	return first +
		(product >= 0 ? product / MH_MENU_POSITION_MIX_MAX
			: -((-product + MIX_BIAS) /
					MH_MENU_POSITION_MIX_MAX));
}

enum mh_error_code
mh_menu_position(const struct mh_menu_layout *layout, unsigned int page,
	unsigned int slot, unsigned int mix, long *x, long *y,
	struct mh_error *error_out)
{
	const long *words;
	unsigned int i;

	if (layout == NULL || x == NULL || y == NULL ||
		page >= MH_MENU_LAYOUT_PAGES || slot >= MH_MENU_LAYOUT_SLOTS ||
		mix > MH_MENU_POSITION_MIX_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu position request");

	words = layout->words[page][slot];

	for (i = 0; i <= MH_MENU_LAYOUT_Y1; i++)
		if (words[i] < -MH_MENU_LAYOUT_COORD_LIMIT ||
			words[i] > MH_MENU_LAYOUT_COORD_LIMIT)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu coordinate out of range");

	*x = interpolate(
		words[MH_MENU_LAYOUT_X0], words[MH_MENU_LAYOUT_X1], mix);
	*y = interpolate(
		words[MH_MENU_LAYOUT_Y0], words[MH_MENU_LAYOUT_Y1], mix);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
