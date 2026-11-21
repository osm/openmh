#ifndef MH_RENDER_SUBMISSION_H
#define MH_RENDER_SUBMISSION_H

#include "interfaces/render/renderer.h"

/* Validate the shared order envelope, mesh indices and root/child roles. */
enum mh_error_code	mh_scene_submission_check(
	const struct mh_renderer_scene_part *parts, size_t count,
	size_t mesh_count, struct mh_error *error_out);

/*
 * After the envelope check, validate each mesh's complete, disjoint coverage.
 * A mesh occurs once as active_lod, or as fixed spans without a LOD chain.
 * NULL/zero is the default order and needs no explicit coverage.
 * These checks borrow inputs and change no submission or mesh state.
 */
enum mh_error_code	mh_scene_submission_mesh_check(
	const struct mh_mesh *mesh, size_t index,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out);

/* Recheck a retained fixed span against live ranges and primitive draws. */
enum mh_error_code	mh_scene_span_check(const struct mh_mesh *mesh,
	const struct mh_mesh_span *span, struct mh_error *error_out);

#endif
