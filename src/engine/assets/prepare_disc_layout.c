#include <string.h>

#include "engine/assets/prepare_cdrdao.h"
#include "engine/assets/prepare_cue.h"
#include "engine/assets/prepare_disc_layout.h"

enum mh_error_code
mh_disc_layout_parse(const void *data, size_t size,
	struct mh_disc_layout *output, struct mh_error *error_out)
{
	const unsigned char *bytes;
	struct mh_disc_layout layout;
	struct mh_cue cue;
	size_t start;
	unsigned int i;
	enum mh_error_code status;

	if (data == NULL || output == NULL || size == 0 ||
		size > MH_DISC_LAYOUT_TEXT_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid disc track layout");

	bytes = data;
	start = 0;

	while (start < size &&
		(bytes[start] == ' ' || bytes[start] == '\t' ||
			bytes[start] == '\r' || bytes[start] == '\n'))
		start++;

	if (size - start >= 6 && memcmp(bytes + start, "CD_ROM", 6) == 0)
		return mh_cdrdao_parse(data, size, output, error_out);

	status = mh_cue_parse(data, size, &cue, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&layout, 0, sizeof(layout));

	for (i = 0; i < MH_RETAIL_DISC_TRACKS; i++)
		strcpy(layout.tracks[i].file, cue.files[i]);

	layout.data_format = MH_DISC_MODE1;
	*output = layout;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
