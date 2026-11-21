#include "backends/geometry/myw_provider.h"
#include "engine/assets/myw.h"
#include "interfaces/geometry/decoder_backend.h"

static enum mh_error_code
decode(void *context, const unsigned char *data, size_t size,
	struct mh_world *output, struct mh_error *error_out)
{
	(void)context;

	return mh_world_myw_decode(data, size, output, error_out);
}

const struct mh_world_decoder *
mh_world_myw_decoder(void)
{
	static const struct mh_world_decoder decoder = {NULL, NULL, decode};

	return &decoder;
}
