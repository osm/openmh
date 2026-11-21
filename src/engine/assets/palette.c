#include "engine/assets/palette.h"
#include "engine/core/error.h"

enum mh_error_code
mh_palette_act_decode(const unsigned char *data, size_t size,
	unsigned long output[MH_PALETTE_COLORS], struct mh_error *error_out)
{
	unsigned int i;

	if (data == NULL || output == NULL || size != MH_PALETTE_COLORS * 3)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected 256 RGB palette entries");

	for (i = 0; i < MH_PALETTE_COLORS; i++)
		output[i] = ((unsigned long)data[i * 3] << 16) |
			((unsigned long)data[i * 3 + 1] << 8) | data[i * 3 + 2];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
