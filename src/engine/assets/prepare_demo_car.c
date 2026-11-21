#include "engine/assets/car.h"
#include "engine/assets/demo_car.h"
#include "engine/assets/prepare_demo_car.h"
#include "engine/assets/tbl.h"

enum mh_error_code
mh_demo_car_prepare(const struct mh_file *source, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_definition_text definition;
	struct mh_car decoded;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing demo preparation output");

	status = mh_car_demo_definition(source, &definition, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_car_config_car_decode(
			definition.data, definition.size, &decoded, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_tbl_encode_named(definition.data, definition.size, NULL,
		&output->data, &output->size, error_out);
}
