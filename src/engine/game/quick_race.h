#ifndef MH_QUICK_RACE_H
#define MH_QUICK_RACE_H

#include "engine/core/error.h"
#include "interfaces/catalog/profile.h"

#define MH_QUICK_CARS 8
#define MH_QUICK_PROFILES 128
#define MH_QUICK_HUMAN MH_QUICK_PROFILES

struct mh_quick_roster {
	unsigned int	profiles[MH_QUICK_CARS];
	unsigned int	slots[MH_QUICK_CARS];

	/* Index into ADP cars[]: classes 1/2/3/4 use 3/2/1/1. */
	unsigned int	car_field;
};

/*
 * Original ordinary-race frontend constructor: choose eight profiles with
 * distinct case-insensitive nicknames, then prepare all eight random slots
 * before resolving collisions. Caller supplies catalog order and shared RNG.
 * This stage also supplies an all-AI roster for the original attract mode.
 * Valid car Division is 100..499; catalog has 8..128 entries and at least
 * eight distinct bounded single-byte nicknames. Output and RNG are atomic.
 */
enum mh_error_code	mh_quick_roster_prepare(
	const struct mh_profile *profiles, unsigned int count,
	unsigned long division, unsigned long *random,
	struct mh_quick_roster *output, struct mh_error *error_out);

/*
 * Original mode-zero local-player insertion. Move entry zero to the entry
 * occupying slot seven, then replace entry zero with the human at slot seven.
 * Its profile index becomes MH_QUICK_HUMAN; caller binds current preferences.
 * No random draws. Reject already bound or malformed rosters atomically.
 */
enum mh_error_code	mh_quick_roster_local(struct mh_quick_roster *roster,
	struct mh_error *error_out);

#endif
