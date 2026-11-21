#include <stddef.h>

#include "backends/collision/executable_provider.h"
#include "engine/assets/col.h"
#include "engine/assets/executable_collision.h"
#include "interfaces/collision/source_backend.h"

static enum mh_error_code
read_collision(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_file *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_collision_executable_read(
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
mh_collision_executable_source(void)
{
	static const struct mh_collision_source source = {
		NULL, NULL, read_collision, decode};

	return &source;
}
