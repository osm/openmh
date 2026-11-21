#include <string.h>

#include "menu_tables.h"

static enum mh_error_code
load_layout(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_menu_layout *output,
	struct mh_error *error_out)
{
	struct mh_test_menu_tables *data;

	(void)resources;
	(void)root;
	(void)reference;
	data = context;
	data->layout_reads++;

	if (data->fail)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"fixture layout not found");

	*output = data->layout;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_palette(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference,
	unsigned long output[MH_PALETTE_COLORS], struct mh_error *error_out)
{
	struct mh_test_menu_tables *data;

	(void)resources;
	(void)root;
	(void)reference;
	data = context;
	data->palette_reads++;

	if (data->fail)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"fixture palette not found");

	memcpy(output, data->colors, sizeof(data->colors));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_menu_tables_init(struct mh_test_menu_tables *data,
	struct mh_menu_layout_source *layouts,
	struct mh_palette_source *palettes)
{
	layouts->context = data;

	layouts->load = load_layout;
	palettes->context = data;
	palettes->load = load_palette;
}
