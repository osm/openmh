#include "engine/core/limits.h"
#include "engine/core/random.h"

#define RANDOM_MULTIPLIER 1103515245UL
#define RANDOM_INCREMENT 12345UL
#define RANDOM_RESULT_SHIFT 16

unsigned int
mh_random_next(unsigned long *state)
{
	*state = (*state * RANDOM_MULTIPLIER + RANDOM_INCREMENT) & MH_U32_MAX;

	return (unsigned int)((*state >> RANDOM_RESULT_SHIFT) &
		MH_RANDOM_RESULT_MAX);
}
