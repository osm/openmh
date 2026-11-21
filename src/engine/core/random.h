#ifndef MH_CORE_RANDOM_H
#define MH_CORE_RANDOM_H

#define MH_RANDOM_RESULT_MAX 32767U
#define MH_RANDOM_RESULT_COUNT (MH_RANDOM_RESULT_MAX + 1U)

/* Advance the caller's wrapping 32-bit state and return bits 16 through 30. */
unsigned int	mh_random_next(unsigned long *state);

#endif
