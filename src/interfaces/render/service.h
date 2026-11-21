#ifndef MH_RENDER_SERVICE_H
#define MH_RENDER_SERVICE_H

struct mh_window_system;
struct mh_canvas_provider;
struct mh_render_set;
struct mh_image_capture;

/*
 * Host-selected rendering family. Its windows must be compatible with its
 * canvases and scene renderers; backends check native bridges before use.
 * Scene rendering and canvas drawing/presentation remain separate contracts:
 * the CPU renderer can use a canvas provider without a GPU scene backend.
 *
 * The application copies these selections during open. Descriptors, tables,
 * configurations and contexts remain borrowed unchanged through child teardown.
 * Capture is optional. A render set may omit unsupported original profiles.
 */
struct mh_render_service {
	const struct mh_window_system	*windows;
	const struct mh_canvas_provider	*canvases;
	const struct mh_render_set	*renderers;
	const struct mh_image_capture	*capture;
};

#endif
