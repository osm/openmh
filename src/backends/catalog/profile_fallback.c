#include <stdio.h>
#include <string.h>

#include "backends/catalog/adp_provider.h"
#include "backends/catalog/provider.h"
#include "engine/assets/demo_drivers.h"
#include "engine/assets/demo_layout.h"
#include "engine/assets/path_catalog.h"
#include "engine/core/text.h"
#include "interfaces/catalog/profile_source_backend.h"

static enum mh_error_code
load_default(const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_profile *output,
	struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	char name[32], stem[32];
	unsigned int i;
	enum mh_error_code status;

	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((layout->fallbacks & MH_DEMO_FALLBACK_CPU_ROSTER) == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"no default profile for this resource layout");

	for (i = 0; i < MH_DEMO_DRIVERS; i++) {
		sprintf(stem, "CPU%u", i + 1);
		sprintf(name, MH_ASSET_CPU_PROFILE_NAME_FORMAT, i + 1);

		if (mh_ascii_equal(reference, name) ||
			mh_ascii_equal(reference, stem))
			return mh_profile_demo_default(i, output, error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
		"unknown default driver reference");
}

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_profile *output,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;
	status = mh_profile_load_from(mh_profile_adp_source(), resources, root,
		reference, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return load_default(resources, root, reference, output, error_out);
}

static enum mh_error_code
catalog(void *context, const struct mh_resource_source *resources,
	const char *root, struct mh_profile_catalog *output,
	struct mh_error *error_out)
{
	const struct mh_demo_asset_layout *layout;
	struct mh_profile_catalog catalog;
	unsigned int i;
	enum mh_error_code status;

	(void)context;
	status = mh_profile_catalog_read_from(
		mh_profile_adp_source(), resources, root, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	status =
		mh_demo_asset_layout_probe(resources, root, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((layout->fallbacks & MH_DEMO_FALLBACK_CPU_ROSTER) == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"no default driver catalog for this resource layout");

	memset(&catalog, 0, sizeof(catalog));
	catalog.count = MH_DEMO_DRIVERS;

	for (i = 0; i < catalog.count; i++) {
		sprintf(catalog.files[i], MH_ASSET_CPU_PROFILE_NAME_FORMAT,
			i + 1);
		status = load(NULL, resources, root, catalog.files[i],
			&catalog.profiles[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*output = catalog;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_profile_source *
mh_profile_fallback_source(void)
{
	static const struct mh_profile_source source = {NULL, load, catalog};

	return &source;
}
