#include "engine/assets/demo_track.h"
#include "engine/assets/prepare_demo_track.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track.h"

enum mh_error_code
mh_demo_track_prepare(const struct mh_file *source,
	enum mh_demo_track_layout layout, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_definition_text definition;
	struct mh_track decoded;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing demo preparation output");

	status = mh_track_demo_definition(
		source, layout, &definition, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_config_trk_decode(
			definition.data, definition.size, &decoded, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_tbl_encode_named(definition.data, definition.size, NULL,
		&output->data, &output->size, error_out);
}
