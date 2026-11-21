#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_canvas_provider.h"
#include "backends/render/software_provider.h"
#include "backends/window/sdl.h"
#include "backends/window/sdl_provider.h"
#include "interfaces/input/backend.h"
#include "interfaces/input/input.h"
#include "interfaces/render/backend.h"
#include "interfaces/render/canvas.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/render/capture.h"
#include "interfaces/window/backend.h"

#define FRAME_WIDTH 320
#define FRAME_HEIGHT 200
#define RESIZED_WIDTH 640
#define RESIZED_HEIGHT 480

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #c,    \
				SDL_GetError());                               \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_ui_list overlay;

static int
check_pixel(SDL_Surface *surface, int x, int y, unsigned int red,
	unsigned int green, unsigned int blue)
{
	Uint8 r, g, b, a;

	CHECK(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
	CHECK(r == red && g == green && b == blue && a == 255);

	return 0;
}

static int
check_capture(struct mh_renderer *renderer, const char *path, int resized,
	unsigned int red, unsigned int blue)
{
	SDL_Surface *surface;

	CHECK(mh_renderer_capture(renderer, path));
	surface = SDL_LoadBMP(path);
	CHECK(surface != NULL);
	CHECK(surface->w == (resized ? RESIZED_WIDTH : FRAME_WIDTH));
	CHECK(surface->h == (resized ? RESIZED_HEIGHT : FRAME_HEIGHT));
	CHECK(check_pixel(surface, surface->w / 2, surface->h / 2, red, 0,
		blue) == 0);
	CHECK(check_pixel(surface, 0, 0, 0, 0, 0) == 0);
	SDL_DestroySurface(surface);

	return 0;
}

static int
check_menu(struct mh_window *window)
{
	struct mh_canvas *canvas;
	unsigned char *pixels;

	pixels = calloc(FRAME_WIDTH * FRAME_HEIGHT, 4);
	CHECK(pixels != NULL);
	CHECK(mh_canvas_open(mh_sdl_canvas_provider(), window, FRAME_WIDTH,
		FRAME_HEIGHT, &canvas, NULL) == MH_ERROR_NONE);
	CHECK(mh_canvas_draw(canvas, pixels, NULL) == MH_ERROR_NONE);
	CHECK(mh_canvas_present(canvas, NULL) == MH_ERROR_NONE);
	mh_canvas_destroy(canvas);
	free(pixels);

	return 0;
}

static int
check_window_session(const struct mh_render_backend *backend, const char *path)
{
	struct mh_window *window;
	struct mh_renderer *renderer, *reference;
	struct mh_mesh mesh;
	const struct mh_mesh *source;
	struct mh_vertex vertices[3];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	struct mh_projection projection;
	struct mh_ui_atlas atlas;
	struct mh_ui_region region;
	struct mh_file actual, expected;
	struct mh_event event;
	SDL_Event native_event;
	float view[16], white[4];
	unsigned char blue[4];
	unsigned int i;
	int close_received;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&draw, 0, sizeof(draw));
	memset(&primitive, 0, sizeof(primitive));
	memset(&atlas, 0, sizeof(atlas));
	memset(&region, 0, sizeof(region));
	memset(view, 0, sizeof(view));
	view[0] = view[5] = view[15] = 1;
	view[10] = -1;
	vertices[0].position[0] = 15;
	vertices[1].position[0] = -15;
	vertices[0].position[1] = vertices[1].position[1] = -10;
	vertices[2].position[1] = 12;

	for (i = 0; i < 3; i++) {
		vertices[i].position[2] = 70;
		vertices[i].color[0] = 1;
	}

	for (i = 0; i < 4; i++)
		white[i] = 1;

	blue[0] = blue[1] = 0;
	blue[2] = blue[3] = 255;
	draw.prelit = 1;
	draw.count = primitive.count = mesh.vertex_count = 3;
	mesh.vertices = vertices;
	mesh.draws = &draw;
	mesh.draw_count = mesh.primitive_count = 1;
	mesh.primitives = &primitive;
	source = &mesh;
	CHECK(mh_projection_setup(FRAME_WIDTH, FRAME_HEIGHT, 1.6f, 1, 1, 100,
		&projection, NULL) == MH_ERROR_NONE);
	CHECK(mh_window_open(mh_sdl_window_system(),
		"Software presentation check", FRAME_WIDTH, FRAME_HEIGHT,
		MH_WINDOW_RESIZABLE, &window, NULL) == MH_ERROR_NONE);
	CHECK(check_menu(window) == 0);
	CHECK(mh_renderer_open(backend, window, &source, 1, FRAME_WIDTH,
		FRAME_HEIGHT, 0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, &source, 1,
		FRAME_WIDTH, FRAME_HEIGHT, 0, &reference,
		NULL) == MH_ERROR_NONE);
	CHECK(!mh_renderer_capture(renderer, path));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(reference, &projection, view, NULL,
		0, NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(renderer, &actual));
	CHECK(mh_renderer_tga_capture(reference, &expected));
	CHECK(actual.size == expected.size &&
		memcmp(actual.data, expected.data, actual.size) == 0);
	free(actual.data);
	free(expected.data);
	mh_renderer_destroy(reference);
	CHECK(check_capture(renderer, path, 0, 255, 0) == 0);
	CHECK(SDL_SetWindowSize(
		mh_sdl_window(window), RESIZED_WIDTH, RESIZED_HEIGHT));
	CHECK(SDL_SyncWindow(mh_sdl_window(window)));
	SDL_PumpEvents();
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_capture(renderer, path, 1, 255, 0) == 0);
	CHECK(mh_renderer_set_motion_blur(renderer, 1));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_capture(renderer, path, 1, 123, 0) == 0);

	/*
	 * UI-only results replace the displayed frame without a history blend.
	 */
	atlas.image.width = atlas.image.height = 1;
	atlas.image.rgba = blue;
	region.width = region.height = 1;
	CHECK(mh_renderer_ui_upload(renderer, &atlas));
	mh_ui_begin(&overlay, FRAME_WIDTH, FRAME_HEIGHT);
	CHECK(mh_ui_quad_scaled(&overlay, &atlas, &region, 0, 0, FRAME_WIDTH,
		FRAME_HEIGHT, white));
	CHECK(mh_renderer_ui_frame(renderer, &overlay));
	CHECK(check_capture(renderer, path, 1, 0, 255) == 0);
	mh_renderer_destroy(renderer);
	CHECK(check_menu(window) == 0);

	memset(&native_event, 0, sizeof(native_event));
	native_event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
	native_event.window.windowID = (SDL_WindowID)mh_window_id(window);
	CHECK(SDL_PushEvent(&native_event));
	close_received = 0;

	while (mh_input_provider_event(mh_sdl_input_provider(), &event))
		if (event.type == MH_EVENT_WINDOW_CLOSE &&
			event.window == mh_window_id(window))
			close_received = 1;

	CHECK(close_received);
	mh_window_destroy(window);

	return 0;
}

static enum mh_error_code
save_image(void *context, const struct mh_image *image, const char *path,
	struct mh_error *error_out)
{
	(void)context;

	return mh_capture_image(mh_sdl_image_capture(), image, path, error_out);
}

int
main(int argc, char *argv[])
{
	struct mh_render_backend backend;
	struct mh_software_output output;
	unsigned int i;

	CHECK(argc == 2);
	memset(&output, 0, sizeof(output));
	output.canvases = mh_sdl_canvas_provider();
	output.capture = save_image;
	CHECK(mh_software_backend_init(&backend, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_window_system_acquire(mh_sdl_window_system(), NULL, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 3; i++)
		CHECK(check_window_session(&backend, argv[1]) == 0);

	mh_window_system_release(mh_sdl_window_system());
	puts("Software windows match offscreen pixels, resize, blur, results and reopen");

	return 0;
}
