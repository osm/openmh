#include <stddef.h>

#include "backends/collision/col_provider.h"
#include "backends/collision/executable_provider.h"
#include "backends/collision/provider.h"
#include "engine/assets/col.h"
#include "engine/collision/load.h"
#include "interfaces/collision/source_backend.h"

static enum mh_error_code
read_collision(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;
	status = mh_collision_read_from(mh_collision_col_source(), resources,
		root, reference, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return mh_collision_read_from(mh_collision_executable_source(),
		resources, root, reference, output, error_out);
}

static enum mh_error_code
decode(void *context, const unsigned char *data, size_t size,
	struct mh_collision *output, struct mh_error *error_out)
{
	(void)context;

	return mh_collision_col_decode(data, size, output, error_out);
}

const struct mh_collision_source *
mh_collision_fallback_source(void)
{
	static const struct mh_collision_source source = {
		NULL, NULL, read_collision, decode};

	return &source;
}
