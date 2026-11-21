#include <string.h>

#include "backends/audio/chf_provider.h"
#include "backends/audio/provider.h"
#include "engine/assets/demo_layout.h"
#include "interfaces/audio/horn_source_backend.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, struct mh_horn_catalog *output,
	struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	struct mh_horn_catalog catalog;
	enum mh_error_code status;

	(void)context;
	status = mh_horn_catalog_load_from(
		mh_horn_chf_source(), resources, root, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((layout->fallbacks & MH_DEMO_FALLBACK_SILENT_HORN) == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"no default horn catalog for this resource layout");

	memset(&catalog, 0, sizeof(catalog));
	catalog.count = 1;
	strcpy(catalog.horns[0].name, "None");
	strcpy(catalog.horns[0].sample, "<None>");
	catalog.horns[0].volume = catalog.horns[0].pitch = 1;
	*output = catalog;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_horn_source *
mh_horn_fallback_source(void)
{
	static const struct mh_horn_source source = {NULL, load};

	return &source;
}
