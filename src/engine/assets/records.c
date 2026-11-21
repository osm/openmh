#include <string.h>

#include "engine/assets/records.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

enum {
	WORD_BYTES = 4,
	LAP_SPLITS_OFFSET = 4,
	LAP_RESERVED_OFFSET = 20,
	RACE_TIME_OFFSET = 4,
	RACE_ROWS_OFFSET = 8,
	RACE_ROW_BYTES = 20,
	RACE_SPLITS_OFFSET = 4,
	RACE_RESERVED_OFFSET = 548
};

enum mh_error_code
mh_lap_record_read(const unsigned char *data, size_t size,
	struct mh_lap_record *output, struct mh_error *error_out)
{
	struct mh_lap_record result;
	unsigned int i;

	if (data == NULL || output == NULL || size != MH_RECORD_LAP_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lap record size");

	memset(&result, 0, sizeof(result));
	result.milliseconds = mh_read_u32le(data);

	for (i = 0; i < MH_RECORD_SPLITS; i++)
		result.splits[i] = mh_read_u32le(
			data + LAP_SPLITS_OFFSET + WORD_BYTES * i);

	for (i = 0; i < MH_RECORD_RESERVED_WORDS; i++)
		result.reserved[i] = mh_read_u32le(
			data + LAP_RESERVED_OFFSET + WORD_BYTES * i);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lap_record_write(const struct mh_lap_record *record, unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	unsigned char result[MH_RECORD_LAP_BYTES];
	unsigned int i;

	if (record == NULL || data == NULL || size != sizeof(result) ||
		record->milliseconds > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lap record output");

	for (i = 0; i < MH_RECORD_SPLITS; i++)
		if (record->splits[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"lap record value exceeds disk word");

	for (i = 0; i < MH_RECORD_RESERVED_WORDS; i++)
		if (record->reserved[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"lap record value exceeds disk word");

	mh_write_u32le(result, record->milliseconds);

	for (i = 0; i < MH_RECORD_SPLITS; i++)
		mh_write_u32le(result + LAP_SPLITS_OFFSET + WORD_BYTES * i,
			record->splits[i]);

	for (i = 0; i < MH_RECORD_RESERVED_WORDS; i++)
		mh_write_u32le(result + LAP_RESERVED_OFFSET + WORD_BYTES * i,
			record->reserved[i]);

	memcpy(data, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_record_read(const unsigned char *data, size_t size,
	struct mh_race_record *output, struct mh_error *error_out)
{
	struct mh_race_record result;
	const unsigned char *row;
	unsigned int i, j;

	if (data == NULL || output == NULL || size != MH_RECORD_RACE_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race record size");

	memset(&result, 0, sizeof(result));
	result.reserved_head = mh_read_u32le(data);
	result.milliseconds = mh_read_u32le(data + RACE_TIME_OFFSET);

	for (i = 0; i < MH_RECORD_ROWS; i++) {
		row = data + RACE_ROWS_OFFSET + RACE_ROW_BYTES * i;
		result.rows[i].end_ms = mh_read_u32le(row);

		for (j = 0; j < MH_RECORD_SPLITS; j++)
			result.rows[i].splits[j] = mh_read_u32le(
				row + RACE_SPLITS_OFFSET + WORD_BYTES * j);
	}

	for (i = 0; i < MH_RECORD_RESERVED_WORDS; i++)
		result.reserved_tail[i] = mh_read_u32le(
			data + RACE_RESERVED_OFFSET + WORD_BYTES * i);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_record_write(const struct mh_race_record *record, unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	unsigned char result[MH_RECORD_RACE_BYTES];
	unsigned char *row;
	unsigned int i, j;

	if (record == NULL || data == NULL || size != sizeof(result) ||
		record->milliseconds > MH_U32_MAX ||
		record->reserved_head > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race record output");

	for (i = 0; i < MH_RECORD_ROWS; i++) {
		if (record->rows[i].end_ms > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"race record end exceeds disk word");

		for (j = 0; j < MH_RECORD_SPLITS; j++)
			if (record->rows[i].splits[j] > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"race record split exceeds disk word");
	}

	for (i = 0; i < MH_RECORD_RESERVED_WORDS; i++)
		if (record->reserved_tail[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"race record reserved field exceeds disk word");

	mh_write_u32le(result, record->reserved_head);
	mh_write_u32le(result + RACE_TIME_OFFSET, record->milliseconds);

	for (i = 0; i < MH_RECORD_ROWS; i++) {
		row = result + RACE_ROWS_OFFSET + RACE_ROW_BYTES * i;
		mh_write_u32le(row, record->rows[i].end_ms);

		for (j = 0; j < MH_RECORD_SPLITS; j++)
			mh_write_u32le(
				row + RACE_SPLITS_OFFSET + WORD_BYTES * j,
				record->rows[i].splits[j]);
	}

	for (i = 0; i < MH_RECORD_RESERVED_WORDS; i++)
		mh_write_u32le(result + RACE_RESERVED_OFFSET + WORD_BYTES * i,
			record->reserved_tail[i]);

	memcpy(data, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
