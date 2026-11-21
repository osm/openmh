#include <math.h>

#include "engine/ui/menu_sine.h"

#define PHASE_RADIANS .0015339807878856412
#define TABLE_ROUND_SCALE 10000000.0

float
mh_menu_sine(unsigned int phase)
{
	return (float)(floor(sin((phase & MH_MENU_SINE_PHASE_MASK) *
				PHASE_RADIANS) *
				TABLE_ROUND_SCALE +
			.5) /
		TABLE_ROUND_SCALE);
}
