#include <string.h>

#include "engine/assets/highscore.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

enum {
	ROW_BYTES = 56,
	NAME_OFFSET = 4,
	CAR_OFFSET = 28,
	AUXILIARY_OFFSET = 44,
	LAP_OFFSET = 48,
	RACE_OFFSET = 52
};

enum mh_error_code
mh_highscore_read(const unsigned char *data, size_t size,
	struct mh_highscore_table *output, struct mh_error *error_out)
{
	struct mh_highscore_table next;
	struct mh_highscore_row *row;
	const unsigned char *record;
	unsigned int i;

	if (data == NULL || size != MH_HIGHSCORE_BYTES || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score payload");

	memset(&next, 0, sizeof(next));

	for (i = 0; i < MH_HIGHSCORE_ROWS; i++) {
		row = &next.rows[i];
		record = data + i * ROW_BYTES;
		row->transmission = mh_read_u32le(record);
		memcpy(row->name, record + NAME_OFFSET, sizeof(row->name));
		memcpy(row->car, record + CAR_OFFSET, sizeof(row->car));
		row->auxiliary = mh_read_u32le(record + AUXILIARY_OFFSET);
		row->lap_ms = mh_read_u32le(record + LAP_OFFSET);
		row->race_ms = mh_read_u32le(record + RACE_OFFSET);
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_highscore_write(const struct mh_highscore_table *table, unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	const struct mh_highscore_row *row;
	unsigned char *record;
	unsigned int i;

	if (table == NULL || data == NULL || size != MH_HIGHSCORE_BYTES ||
		table->dirty > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score output");

	for (i = 0; i < MH_HIGHSCORE_ROWS; i++) {
		row = &table->rows[i];

		if (row->transmission > MH_U32_MAX ||
			row->auxiliary > MH_U32_MAX ||
			row->lap_ms > MH_U32_MAX || row->race_ms > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid high-score word");
	}

	for (i = 0; i < MH_HIGHSCORE_ROWS; i++) {
		row = &table->rows[i];
		record = data + i * ROW_BYTES;
		mh_write_u32le(record, row->transmission);
		memcpy(record + NAME_OFFSET, row->name, sizeof(row->name));
		memcpy(record + CAR_OFFSET, row->car, sizeof(row->car));
		mh_write_u32le(record + AUXILIARY_OFFSET, row->auxiliary);
		mh_write_u32le(record + LAP_OFFSET, row->lap_ms);
		mh_write_u32le(record + RACE_OFFSET, row->race_ms);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
