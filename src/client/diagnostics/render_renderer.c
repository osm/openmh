#include "backends/render/sdl_provider.h"
#include "client/diagnostics/render_renderer.h"
#include "interfaces/render/backend.h"
#include "interfaces/render/service.h"

const struct mh_render_backend *
mh_diagnostic_renderer(enum mh_render_profile profile)
{
	return mh_render_set_profile(
		mh_sdl_render_service()->renderers, profile);
}

const struct mh_render_set *
mh_diagnostic_renderers(void)
{
	return mh_sdl_render_service()->renderers;
}

/* Diagnostic default; portable dispatch never chooses a backend implicitly. */
struct mh_renderer *
mh_renderer_create_scene_sized(struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height)
{
	struct mh_renderer *renderer;

	renderer = NULL;

	if (mh_renderer_open(mh_diagnostic_renderer(MH_RENDER_GLIDE), window,
		meshes, count, width, height, 0, &renderer,
		NULL) != MH_ERROR_NONE)
		return NULL;

	return renderer;
}

struct mh_renderer *
mh_renderer_create_scene(struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count)
{
	return mh_renderer_create_scene_sized(
		window, meshes, count, MH_VIEW_WIDTH, MH_VIEW_HEIGHT);
}

struct mh_renderer *
mh_renderer_create(struct mh_window *window, const struct mh_mesh *mesh)
{
	return mh_renderer_create_scene(window, &mesh, 1);
}
