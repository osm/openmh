#include "backends/collision/col_provider.h"
#include "engine/assets/col.h"
#include "interfaces/collision/decoder_backend.h"
#include "interfaces/collision/source_backend.h"

static enum mh_error_code
decode(void *context, const unsigned char *data, size_t size,
	struct mh_collision *output, struct mh_error *error_out)
{
	(void)context;

	return mh_collision_col_decode(data, size, output, error_out);
}

const struct mh_collision_decoder *
mh_collision_col_decoder(void)
{
	static const struct mh_collision_decoder decoder = {NULL, NULL, decode};

	return &decoder;
}

const struct mh_collision_source *
mh_collision_col_source(void)
{
	static const struct mh_collision_source source = {
		NULL, NULL, NULL, decode};

	return &source;
}
