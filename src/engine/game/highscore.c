#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/highscore.h"
#include "engine/game/race_limits.h"

#define DEFAULT_LAP_MS 240000UL
#define DEFAULT_LAP_STEP_MS 1000UL
#define LEAGUE_TIME_FACTOR 100
#define DEFAULT_AUXILIARY_BASE 13

enum {
	LEAGUE_PREFIX_SIZE = 3,
	TRACK_PREFIX_SIZE = 6
};

static const char difficulty_codes[] = "EMH";

static enum mh_error_code
track_basename(const struct mh_highscore_query *query, char *name, size_t *used,
	struct mh_error *error_out)
{
	static const char record_class[] = "NM";
	size_t length, i;

	if (query->record_class > 1 || query->track == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score track query");

	length = 0;

	while (length < MH_HIGHSCORE_TEXT_LIMIT && query->track[length] != 0)
		length++;

	if (length == 0 || length >= MH_HIGHSCORE_TEXT_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score track name");

	for (i = 0; i < length; i++)
		if ((unsigned char)query->track[i] < 32 ||
			(unsigned char)query->track[i] > 126 ||
			strchr("/\\:*?\"<>|", query->track[i]) != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid high-score track name");

	if (query->kind != MH_HIGHSCORE_LAP &&
		(query->laps < MH_RACE_MIN_LAPS ||
			query->laps > MH_RACE_MAX_LAPS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score lap count");

	if (query->kind == MH_HIGHSCORE_SINGLE && query->difficulty > 2)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score difficulty");

	name[0] = query->kind == MH_HIGHSCORE_SINGLE ? 'S' : 'B';
	name[1] = query->kind == MH_HIGHSCORE_LAP ? 'L' : 'R';
	name[2] = difficulty_codes[query->kind == MH_HIGHSCORE_SINGLE
			? query->difficulty
			: 1];
	name[3] = record_class[query->record_class];
	name[4] = query->kind == MH_HIGHSCORE_LAP
		? '0'
		: (char)('0' + query->laps / 10);
	name[5] = query->kind == MH_HIGHSCORE_LAP
		? '0'
		: (char)('0' + query->laps % 10);
	memcpy(name + TRACK_PREFIX_SIZE, query->track, length);
	*used = TRACK_PREFIX_SIZE + length;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_highscore_filename(const struct mh_highscore_query *query, char *output,
	size_t capacity, struct mh_error *error_out)
{
	static const char division[] = "321", extension[] = ".MHI";
	char name[MH_HIGHSCORE_FILENAME_SIZE];
	size_t used = 0;
	enum mh_error_code status;

	if (query == NULL || output == NULL ||
		query->kind > MH_HIGHSCORE_LEAGUE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score query");

	if (query->kind == MH_HIGHSCORE_LEAGUE) {
		if (query->difficulty > 2 || query->division > 2)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league high-score query");

		name[0] = 'L';
		name[1] = difficulty_codes[query->difficulty];
		name[2] = division[query->division];
		used = LEAGUE_PREFIX_SIZE;
	} else {
		status = track_basename(query, name, &used, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	memcpy(name + used, extension, sizeof(extension));
	used += sizeof(extension);

	if (used > capacity)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"high-score filename exceeds capacity");

	memcpy(output, name, used);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Default labels are written into an already zeroed table. */
static void
default_name(unsigned char *name, unsigned int index)
{
	static const char prefix[] = "DI dude ";
	size_t offset;
	unsigned int rank;

	offset = sizeof(prefix) - 1;
	rank = index + 1;
	memcpy(name, prefix, offset);

	if (rank == 10) {
		name[offset] = '1';
		name[offset + 1] = '0';
	} else
		name[offset] = (unsigned char)('1' + index);
}

enum mh_error_code
mh_highscore_defaults(unsigned int kind, unsigned int laps,
	struct mh_highscore_table *output, struct mh_error *error_out)
{
	static const char *const cars[MH_HIGHSCORE_ROWS] = {"Serpent", "Titan",
		"Avalanche", "Kobra X3", "R-Build 2", "ASC II", "Python V12",
		"Resonic", "Asc", "Adder MK2"};

	struct mh_highscore_table next;
	struct mh_highscore_row *row;
	unsigned int i;

	if (output == NULL || kind > MH_HIGHSCORE_LEAGUE ||
		((kind == MH_HIGHSCORE_RACE || kind == MH_HIGHSCORE_SINGLE) &&
			(laps < MH_RACE_MIN_LAPS || laps > MH_RACE_MAX_LAPS)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score defaults");

	memset(&next, 0, sizeof(next));

	for (i = 0; i < MH_HIGHSCORE_ROWS; i++) {
		row = &next.rows[i];
		row->transmission = i % 2;
		row->auxiliary = DEFAULT_AUXILIARY_BASE - i;
		default_name(row->name, i);

		memcpy(row->car, cars[i], strlen(cars[i]) + 1);
		row->lap_ms = DEFAULT_LAP_MS + i * DEFAULT_LAP_STEP_MS;

		if (kind == MH_HIGHSCORE_LAP)
			row->race_ms = 0;
		else if (kind == MH_HIGHSCORE_LEAGUE)
			row->race_ms = row->lap_ms * LEAGUE_TIME_FACTOR;
		else
			row->race_ms = row->lap_ms * laps;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_highscore_overlay(unsigned int kind, unsigned int laps,
	const unsigned char *data, size_t size,
	struct mh_highscore_table *output, struct mh_error *error_out)
{
	struct mh_highscore_table next;
	unsigned char bytes[MH_HIGHSCORE_BYTES];
	enum mh_error_code status;

	if (output == NULL || (data == NULL && size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score overlay");

	status = mh_highscore_defaults(kind, laps, &next, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_highscore_write(
			&next, bytes, sizeof(bytes), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (size > sizeof(bytes))
		size = sizeof(bytes);

	if (size != 0)
		memcpy(bytes, data, size);

	return mh_highscore_read(bytes, sizeof(bytes), output, error_out);
}

enum mh_error_code
mh_highscore_rank(const struct mh_highscore_table *table, int lap_column,
	unsigned long milliseconds, unsigned int *rank,
	struct mh_error *error_out)
{
	unsigned int i, result;
	unsigned long value;

	if (table == NULL || rank == NULL ||
		(lap_column != 0 && lap_column != 1) ||
		milliseconds > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score comparison");

	result = 0;

	for (i = 0; i < MH_HIGHSCORE_ROWS; i++) {
		value = lap_column ? table->rows[i].lap_ms
				: table->rows[i].race_ms;

		if (value > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid high-score time");

		if (result == 0 && milliseconds < value)
			result = i + 1;
	}

	*rank = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
update_row(struct mh_highscore_table *table, unsigned int rank,
	const struct mh_highscore_candidate *candidate, int insert,
	struct mh_error *error_out)
{
	struct mh_highscore_table next;
	struct mh_highscore_row *row;
	size_t name_length, car_length;
	unsigned int i;

	if (table == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing high-score table");

	if (rank == 0 || rank > MH_HIGHSCORE_ROWS)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (candidate == NULL || candidate->name == NULL ||
		candidate->car == NULL || candidate->name_size == 0 ||
		candidate->name_size > MH_HIGHSCORE_TEXT_LIMIT ||
		candidate->car_size == 0 ||
		candidate->car_size > MH_HIGHSCORE_TEXT_LIMIT ||
		candidate->transmission > MH_U32_MAX ||
		candidate->auxiliary > MH_U32_MAX ||
		candidate->lap_ms > MH_U32_MAX ||
		candidate->race_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score candidate");

	name_length = car_length = 0;

	while (name_length < candidate->name_size &&
		candidate->name[name_length] != 0)
		name_length++;

	while (car_length < candidate->car_size &&
		candidate->car[car_length] != 0)
		car_length++;

	if (name_length == candidate->name_size ||
		car_length == candidate->car_size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated high-score candidate");

	if (name_length > MH_HIGHSCORE_NAME_SIZE - 1)
		name_length = MH_HIGHSCORE_NAME_SIZE - 1;

	if (car_length > MH_HIGHSCORE_CAR_SIZE - 1)
		car_length = MH_HIGHSCORE_CAR_SIZE - 1;

	next = *table;

	if (insert)
		for (i = MH_HIGHSCORE_ROWS - 1; i >= rank; i--)
			next.rows[i] = next.rows[i - 1];

	row = &next.rows[rank - 1];
	memcpy(row->name, candidate->name, name_length);
	row->name[name_length] = 0;
	memcpy(row->car, candidate->car, car_length);
	row->car[car_length] = 0;
	row->transmission = candidate->transmission;
	row->auxiliary = candidate->auxiliary;
	row->lap_ms = candidate->lap_ms;
	row->race_ms = candidate->race_ms;
	next.dirty = 1;
	*table = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_highscore_set(struct mh_highscore_table *table, unsigned int rank,
	const struct mh_highscore_candidate *candidate,
	struct mh_error *error_out)
{
	return update_row(table, rank, candidate, 0, error_out);
}

enum mh_error_code
mh_highscore_insert(struct mh_highscore_table *table, unsigned int rank,
	const struct mh_highscore_candidate *candidate,
	struct mh_error *error_out)
{
	return update_row(table, rank, candidate, 1, error_out);
}
