#include "backends/geometry/myo_provider.h"
#include "engine/assets/myo.h"
#include "interfaces/geometry/decoder_backend.h"

static enum mh_error_code
decode(void *context, const unsigned char *data, size_t size,
	struct mh_model *output, struct mh_error *error_out)
{
	(void)context;

	return mh_model_myo_decode(data, size, output, error_out);
}

const struct mh_model_decoder *
mh_model_myo_decoder(void)
{
	static const struct mh_model_decoder decoder = {NULL, NULL, decode};

	return &decoder;
}
