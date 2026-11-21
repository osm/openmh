#include "engine/ui/menu_preview.h"

float
mh_menu_preview_advance(unsigned int selected, unsigned long elapsed_ms,
	unsigned int *shown, float fade)
{
	long double adjusted;

	if (fade == 0)
		*shown = selected;

	adjusted = (long double)fade +
		(*shown == selected ? 1 : -1) * (long double)elapsed_ms * 0.003;
	fade = (float)adjusted;

	if (adjusted > 1)
		fade = 1;

	if (adjusted < 0)
		fade = 0;

	return fade;
}
