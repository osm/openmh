#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_catalog.h"

#define CATALOG_LIMIT 4096
#define NAME_BYTES 256

static int
valid_name(const char *name)
{
	unsigned int i;

	if (name == NULL)
		return 0;

	for (i = 0; i < NAME_BYTES; i++)
		if (name[i] == 0)
			return 1;

	return 0;
}

enum mh_error_code
mh_menu_catalog_restore(const char *const *names, unsigned int count,
	const char *saved, unsigned int *selected, struct mh_error *error_out)
{
	unsigned int i, selection;

	if (names == NULL || count == 0 || count > CATALOG_LIMIT ||
		selected == NULL || !valid_name(saved))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid saved catalog selection");

	selection = 0;

	for (i = 0; i < count; i++) {
		if (!valid_name(names[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid catalog selection name");

		if (strcmp(names[i], saved) == 0)
			selection = i;
	}

	*selected = selection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_catalog_order(const unsigned long *divisions, unsigned int count,
	unsigned int *order, unsigned int capacity, struct mh_error *error_out)
{
	unsigned int *sorted, i, end, swap_index;

	if (divisions == NULL || order == NULL || count == 0 ||
		count > CATALOG_LIMIT || capacity < count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu catalog order");

	for (i = 0; i < count; i++)
		if (divisions[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu catalog Division");

	sorted = (unsigned int *)malloc((size_t)count * sizeof(*sorted));

	if (sorted == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory ordering menu catalog");

	for (i = 0; i < count; i++)
		sorted[i] = i;

	for (end = count - 1; end != 0; end--)
		for (i = 0; i < end; i++)
			if (divisions[sorted[i]] > divisions[sorted[i + 1]]) {
				swap_index = sorted[i];
				sorted[i] = sorted[i + 1];
				sorted[i + 1] = swap_index;
			}

	memcpy(order, sorted, (size_t)count * sizeof(*order));
	free(sorted);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
