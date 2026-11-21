#ifndef MH_CLIENT_DIAGNOSTICS_RENDER_RENDERER_H
#define MH_CLIENT_DIAGNOSTICS_RENDER_RENDERER_H

#include "engine/render/profile.h"
#include "interfaces/render/renderer.h"

struct mh_render_backend;
struct mh_render_set;

const struct mh_render_set	*mh_diagnostic_renderers(void);

/* Diagnostic provider choice; public launch selection is separate. */
const struct mh_render_backend	*mh_diagnostic_renderer(
	enum mh_render_profile profile);

/*
 * Native diagnostic constructors select the Glide profile. Other profiles
 * are available through mh_diagnostic_renderer and explicit renderer opening.
 * Mesh must remain alive until destroy. Failed constructors return NULL;
 * operations on an opened renderer use mh_renderer_error().
 */
struct mh_renderer	*mh_renderer_create(struct mh_window *window,
	const struct mh_mesh *mesh);

/*
 * NULL meshes with count zero creates a menu-only renderer.
 * Multiple independently transformed meshes share one depth/render pass.
 * Each mesh and its borrowed atlas must remain alive. Image pixels and atlas
 * bindings are immutable until destroy; shared pixels upload/release once.
 * Arrays and capacities stay fixed; vertex contents, bounded draw ranges
 * and the active lod index may change.
 */
struct mh_renderer	*mh_renderer_create_scene(struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count);

/*
 * Fixed render target for a selected race mode; all projection, overlay,
 * distortion and depth-query coordinates must use these same dimensions.
 * Bounds are MH_VIEW_MIN/MAX_WIDTH/HEIGHT. The unsized constructors retain
 * MH_VIEW_WIDTH/HEIGHT for inspection tools. Dimensions never follow a
 * window resize; presentation scales the rendered frame to the window.
 */
struct mh_renderer	*mh_renderer_create_scene_sized(
	struct mh_window *window, const struct mh_mesh *const *meshes,
	size_t count, unsigned int width, unsigned int height);

#endif
