#include <stdlib.h>

#include "engine/catalog/profile.h"
#include "engine/catalog/profile_load.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "interfaces/catalog/profile_source.h"

enum mh_error_code
mh_profile_catalog_load_from(const struct mh_profile_source *profile_source,
	const struct mh_resource_source *source, const char *root,
	const struct mh_horn_catalog *horns, unsigned long *random,
	struct mh_profile_catalog *output, struct mh_error *error_out)
{
	struct mh_profile_catalog *catalog;
	unsigned long seed;
	unsigned int i, j;
	enum mh_error_code status;

	if (root == NULL || horns == NULL || random == NULL || output == NULL ||
		*random > MH_U32_MAX || horns->count == 0 ||
		horns->count > MH_HORNS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid profile catalog input");

	catalog = (struct mh_profile_catalog *)calloc(1, sizeof(*catalog));

	if (catalog == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading profile catalog");

	seed = *random;
	status = mh_profile_catalog_read_from(
		profile_source, source, root, catalog, error_out);

	if (status != MH_ERROR_NONE)
		goto done;

	if (catalog->count == 0 || catalog->count > MH_PROFILE_CATALOG_LIMIT) {
		status = mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid decoded profile catalog count");
		goto done;
	}

	status = mh_profile_catalog_advance_rng(
		catalog->count, &seed, error_out);

	if (status != MH_ERROR_NONE)
		goto done;

	for (i = 0; i < catalog->count; i++) {
		status = mh_profile_initialize(&catalog->profiles[i], horns,
			&seed, &catalog->profiles[i], error_out);

		if (status != MH_ERROR_NONE)
			goto done;

		for (j = 0; j < i; j++)
			if (mh_ascii_compare(catalog->profiles[j].nick,
				catalog->profiles[i].nick) == 0) {
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"ambiguous profile nickname");
				goto done;
			}
	}

	*output = *catalog;
	*random = seed;

done:
	free(catalog);

	return status;
}
