#include "engine/assets/menu_layout.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define WORD_BYTES 4U
#define RECORD_BYTES 32U
#define PAGE_BYTES 1024UL
#define EFFECT_PARAMETER_LIMIT 1048576L

static long
read_i32(const unsigned char *data)
{
	unsigned long value;

	value = mh_read_u32le(data);

	return value <= (unsigned long)MH_I32_MAX
		? (long)value
		: -1L - (long)(MH_U32_MAX - value);
}

enum mh_error_code
mh_menu_layout_dta_decode(const unsigned char *data, size_t size,
	struct mh_menu_layout *output, struct mh_error *error_out)
{
	unsigned int page, slot, field;
	long value;
	size_t offset;

	if (data == NULL || output == NULL || size != MH_MENU_LAYOUT_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu layout size");

	/*
	 * Validate before publishing, without a large temporary stack object.
	 */
	for (page = 0; page < MH_MENU_LAYOUT_PAGES; page++)
		for (slot = 0; slot < MH_MENU_LAYOUT_SLOTS; slot++) {
			offset =
				(size_t)page * PAGE_BYTES + slot * RECORD_BYTES;

			for (field = 0; field < MH_MENU_LAYOUT_FIELDS;
				field++) {
				value = read_i32(
					data + offset + field * WORD_BYTES);

				if (field <= MH_MENU_LAYOUT_Y1 &&
					(value < -MH_MENU_LAYOUT_COORD_LIMIT ||
						value > MH_MENU_LAYOUT_COORD_LIMIT))
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"menu coordinate out of range");

				if (field == MH_MENU_LAYOUT_PARAMETER &&
					(value < -EFFECT_PARAMETER_LIMIT ||
						value > EFFECT_PARAMETER_LIMIT))
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"menu effect parameter out of range");
			}
		}

	for (page = 0; page < MH_MENU_LAYOUT_PAGES; page++)
		for (slot = 0; slot < MH_MENU_LAYOUT_SLOTS; slot++)
			for (field = 0; field < MH_MENU_LAYOUT_FIELDS; field++)
				output->words[page][slot][field] = read_i32(
					data + (size_t)page * PAGE_BYTES +
					slot * RECORD_BYTES +
					field * WORD_BYTES);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
