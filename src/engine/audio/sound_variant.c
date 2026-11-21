#include "engine/audio/sound_variant.h"
#include "engine/core/limits.h"

#define VARIANT_COUNTER_STEP 313UL

unsigned int
mh_sound_variant_next(unsigned long *counter)
{
	*counter = (*counter + VARIANT_COUNTER_STEP) & MH_U32_MAX;

	return (unsigned int)(*counter & 1U);
}
