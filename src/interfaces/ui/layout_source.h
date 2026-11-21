#ifndef MH_UI_LAYOUT_SOURCE_H
#define MH_UI_LAYOUT_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/ui/layout.h"

struct mh_menu_layout_source;
struct mh_resource_source;

/*
 * Return validated pages/slots in original order, retaining all eight fields.
 * Reference is a logical resource key, resolved only by the provider. Borrow
 * sources/resources during loading only. Output owns inline values and is
 * unchanged on failure.
 */
enum mh_error_code	mh_menu_layout_load_from(
	const struct mh_menu_layout_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_menu_layout *output,
	struct mh_error *error_out);

#endif
