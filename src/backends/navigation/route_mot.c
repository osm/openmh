#include <stdlib.h>

#include "backends/navigation/internal.h"
#include "backends/navigation/provider.h"
#include "engine/assets/route.h"
#include "engine/assets/tbl.h"

enum mh_error_code
mh_route_mot_load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, size_t byte_limit,
	struct mh_route *output, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	(void)context;
	status = mh_navigation_resource_read(
		resources, root, reference, byte_limit, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_route_mot_decode(file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

static enum mh_error_code
load_wrapped_route(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, size_t byte_limit,
	struct mh_route *output, struct mh_error *error_out)
{
	struct mh_file file;
	unsigned char *text;
	size_t size;
	enum mh_error_code status;

	(void)context;
	status = mh_navigation_resource_read(
		resources, root, reference, byte_limit, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	text = NULL;
	status = mh_tbl_decode(file.data, file.size, &text, &size, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_route_mot_parse(text, size, output, error_out);

	free(text);
	free(file.data);

	return status;
}

const struct mh_navigation_source *
mh_navigation_tbl_routes(void)
{
	static const struct mh_navigation_source source = {
		NULL, load_wrapped_route, NULL, NULL};

	return &source;
}
