#include <stddef.h>
#include <string.h>

#include "engine/audio/horn.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/core/text.h"

#define VOLUME_ROUNDING_FLOOR (-1e-12)
#define VOLUME_FACTOR_MAX 8
#define PITCH_FACTOR_MIN .1
#define PITCH_FACTOR_MAX 4
#define HORN_GAIN_SCALE 1.5

enum mh_error_code
mh_horn_find(const struct mh_horn_catalog *catalog, const char *name,
	unsigned int *index, struct mh_error *error_out)
{
	unsigned int i, found;

	if (catalog == NULL || name == NULL || index == NULL ||
		catalog->count == 0 || catalog->count > MH_HORNS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid horn catalog");

	found = catalog->count;

	for (i = 0; i < catalog->count; i++) {
		if (memchr(catalog->horns[i].name, 0,
			sizeof(catalog->horns[i].name)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid horn catalog name");

		if (!mh_ascii_equal(catalog->horns[i].name, name))
			continue;

		if (found != catalog->count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous horn name");

		found = i;
	}

	if (found == catalog->count)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"horn name is absent from the installed catalog");

	*index = found;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_horn_default(const struct mh_horn_catalog *catalog, unsigned int *index,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_horn_find(catalog, "Honker1", index, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return mh_horn_find(catalog, catalog->horns[0].name, index, error_out);
}

enum mh_error_code
mh_horn_profile_find(const struct mh_horn_catalog *catalog, const char name[16],
	unsigned long *random_state, unsigned int *index,
	struct mh_error *error_out)
{
	unsigned int i, found;
	unsigned long seed;

	if (catalog == NULL || name == NULL || random_state == NULL ||
		index == NULL || catalog->count == 0 ||
		catalog->count > MH_HORNS || *random_state > MH_U32_MAX ||
		memchr(name, 0, 16) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid profile horn lookup");

	found = catalog->count;

	for (i = 0; i < catalog->count; i++) {
		if (memchr(catalog->horns[i].name, 0,
			sizeof(catalog->horns[i].name)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid horn catalog name");

		if (found == catalog->count &&
			mh_ascii_equal(catalog->horns[i].name, name))
			found = i;
	}

	if (found != catalog->count) {
		*index = found;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (strlen(name) < 4)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"unknown profile horn name is too short");

	seed = 0;

	for (i = 0; i < 4; i++)
		seed |= (unsigned long)(unsigned char)name[i] << (i * 8);

	*index = mh_random_next(&seed) % catalog->count;
	*random_state = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_horn_parameters(const struct mh_horn *horn, double *volume, double *pitch,
	struct mh_error *error_out)
{
	double volume_factor, pitch_factor;

	/*
	 * With a double .01 multiplier, -100 percent can produce a tiny
	 * negative factor in extended precision. Squaring it gives positive
	 * gain.
	 */
	if (horn == NULL || volume == NULL || pitch == NULL ||
		!(horn->volume >= VOLUME_ROUNDING_FLOOR &&
			horn->volume <= VOLUME_FACTOR_MAX) ||
		!(horn->pitch >= PITCH_FACTOR_MIN &&
			horn->pitch <= PITCH_FACTOR_MAX))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid horn factors");

	volume_factor = (double)(float)horn->volume;
	pitch_factor = (double)(float)horn->pitch;
	*volume = (double)(float)((long double)(float)(volume_factor *
					HORN_GAIN_SCALE) *
		volume_factor);
	*pitch = (double)(float)((long double)pitch_factor * pitch_factor);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_horn_preview_parameters(const struct mh_horn *horn, double *volume,
	double *pitch, struct mh_error *error_out)
{
	if (horn == NULL || volume == NULL || pitch == NULL ||
		!(horn->volume >= VOLUME_ROUNDING_FLOOR &&
			horn->volume <= VOLUME_FACTOR_MAX) ||
		!(horn->pitch >= PITCH_FACTOR_MIN &&
			horn->pitch <= PITCH_FACTOR_MAX))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid horn factors");

	*volume = (double)(float)((long double)(float)horn->volume *
		HORN_GAIN_SCALE);
	*pitch = (double)(float)horn->pitch;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
