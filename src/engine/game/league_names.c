#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/core/text.h"
#include "engine/game/league_names.h"
#include "engine/storage/path_names.h"

#define NAME_LETTERS 25
#define DISPLAY_NAME_SIZE 64
#define CATALOG_NAME_SIZE 128
#define CATALOG_LIMIT 4096
#define DATE_YEAR_LIMIT 8099
#define DATE_YEAR_PIVOT 90

enum mh_error_code
mh_league_auto_name(unsigned long *random, char output[MH_LEAGUE_NAME_SIZE],
	struct mh_error *error_out)
{
	unsigned long seed;
	char name[MH_LEAGUE_NAME_SIZE];
	unsigned int first_draw, second_draw;

	if (random == NULL || output == NULL || *random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid automatic League name");

	seed = *random;
	first_draw = mh_random_next(&seed) % NAME_LETTERS;
	second_draw = mh_random_next(&seed) % NAME_LETTERS;
	memset(name, 0, sizeof(name));
	strcpy(name, "League");
	name[6] = (char)('A' + second_draw);
	name[7] = (char)('A' + first_draw);
	memcpy(output, name, sizeof(name));
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_filename(const unsigned char *name, const char *const *others,
	unsigned int count, char output[MH_LEAGUE_FILENAME_SIZE],
	struct mh_error *error_out)
{
	char candidate[MH_LEAGUE_FILENAME_SIZE];
	unsigned int i, stem_length, suffix, collision, length;

	if (name == NULL || output == NULL || count > CATALOG_LIMIT ||
		(count != 0 && others == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League filename input");

	for (i = 0; i < count; i++)
		if (others[i] != NULL) {
			for (length = 0; length < CATALOG_NAME_SIZE &&
				others[i][length] != 0;
				length++) {
			}

			if (length == CATALOG_NAME_SIZE)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"unterminated League catalog filename");
		}

	stem_length = 0;
	memset(candidate, 0, sizeof(candidate));

	for (i = 0; i < DISPLAY_NAME_SIZE && name[i] != 0; i++)
		if (name[i] >= 32 && name[i] <= 127 &&
			strchr(":;<>?*\\./", name[i]) == NULL)
			candidate[stem_length++] = (char)name[i];

	if (i == DISPLAY_NAME_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated League display name");

	for (suffix = 0; suffix <= count; suffix++) {
		sprintf(candidate + stem_length,
			"%04u" MH_USER_LEAGUE_EXTENSION, suffix);
		mh_ascii_lower(candidate);
		collision = 0;

		for (i = 0; i < count; i++)
			if (others[i] != NULL &&
				mh_ascii_equal(candidate, others[i])) {
				collision = 1;
				break;
			}

		if (!collision) {
			memcpy(output, candidate, sizeof(candidate));
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(
		error_out, MH_ERROR_RANGE, "League filename space exhausted");
}

enum mh_error_code
mh_league_date(unsigned int year, unsigned int month, unsigned int day,
	unsigned long *output, struct mh_error *error_out)
{
	static const unsigned int days[12] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	unsigned int full_year, month_days;

	if (output == NULL || year > DATE_YEAR_LIMIT || month >= 12 || day == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League creation date");

	full_year = year + (year < DATE_YEAR_PIVOT ? 2000 : 1900);
	month_days = days[month];

	if (month == 1 && full_year % 4 == 0 &&
		(full_year % 100 != 0 || full_year % 400 == 0))
		month_days++;

	if (day > month_days)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League creation date");

	*output = (unsigned long)full_year * 10000 + (month + 1) * 100 + day;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
