#include <stddef.h>
#include <string.h>

#include "engine/audio/horn.h"
#include "engine/catalog/profile.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"

#define PORTRAIT_RANDOM_DRAWS 13824UL

enum mh_error_code
mh_profile_catalog_advance_rng(
	unsigned int count, unsigned long *random, struct mh_error *error_out)
{
	unsigned long i, draws, seed;

	if (random == NULL || *random > MH_U32_MAX ||
		count > MH_PROFILE_CATALOG_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid profile catalog random state");

	seed = *random;

	/*
	 * One extra picture, 96*72 packed bytes each, high/low nibble from two
	 * consecutive random draws.
	 */
	draws = count == 0 ? 0 : (count + 1UL) * PORTRAIT_RANDOM_DRAWS;

	for (i = 0; i < draws; i++)
		(void)mh_random_next(&seed);

	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_profile_initialize(const struct mh_profile *definition,
	const struct mh_horn_catalog *horns, unsigned long *random,
	struct mh_profile *output, struct mh_error *error_out)
{
	struct mh_profile profile;
	unsigned long seed;
	unsigned int i, horn;
	enum mh_error_code status;

	if (definition == NULL || horns == NULL || random == NULL ||
		output == NULL || horns->count == 0 ||
		horns->count > MH_HORNS || *random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid profile initialization");

	for (i = 0; i < horns->count; i++)
		if (horns->horns[i].name[0] == 0 ||
			memchr(horns->horns[i].name, 0,
				sizeof(horns->horns[i].name)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid profile horn catalog");

	profile = *definition;

	/* Before parsing CarHorn. */
	seed = *random;
	horn = mh_random_next(&seed) % horns->count;

	if (profile.league_fields & MH_PROFILE_HORN_FIELD) {
		status = mh_horn_profile_find(
			horns, profile.horn, &seed, &horn, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	strcpy(profile.horn, horns->horns[horn].name);
	*output = profile;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
