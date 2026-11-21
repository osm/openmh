#ifndef MH_GEOMETRY_WIREFRAME_SOURCE_H
#define MH_GEOMETRY_WIREFRAME_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/geometry/wireframe.h"

struct mh_wireframe_source;
struct mh_resource_source;

/*
 * Load a logical wireframe reference into independently owned points and edges.
 * The source resolves physical names and encodings. Input resources and source
 * state are borrowed during loading; output retains neither. Failure preserves
 * output. Release successful output with mh_wireframe_free before replacing it.
 * Diagnostics survive until the next source operation.
 */
enum mh_error_code	mh_wireframe_load_from(
	const struct mh_wireframe_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_wireframe *output,
	struct mh_error *error_out);

#endif
