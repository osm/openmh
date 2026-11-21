#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_provider.h"
#include "backends/window/sdl_provider.h"
#include "engine/render/matrix.h"
#include "interfaces/render/backend.h"
#include "renderer_depth.h"
#include "retro.h"

struct fixture {
	struct mh_mesh			mesh;
	struct mh_draw			draw;
	struct mh_mesh_primitive	primitive;
	struct mh_vertex		vertices[6], lit[6];
	float				colors[18], matrix[16];
	unsigned char			white[4], group;
	struct mh_image			image;
	struct mh_projection		projection;
	struct mh_ui_atlas		atlas;
};

static unsigned int failures;
static struct mh_ui_list overlay;

static void
check(int success, const char *label)
{
	if (success)
		printf("%s\n", label);
	else {
		fprintf(stderr, "%s failed\n", label);
		failures++;
	}
}

static int
operation(enum mh_error_code status, const struct mh_error *error)
{
	if (status == MH_ERROR_NONE)
		return 1;

	fprintf(stderr, "%s\n", error->message);
	failures++;

	return 0;
}

static int
rendered(struct mh_renderer *renderer, int success)
{
	struct mh_error error;

	if (success)
		return 1;

	mh_renderer_error(renderer, &error);
	operation(error.code, &error);

	return 0;
}

static void
source_color(struct fixture *fixture, unsigned int channel)
{
	unsigned int i;

	memset(fixture->colors, 0, sizeof(fixture->colors));

	for (i = 0; i < 6; i++) {
		memset(fixture->vertices[i].color, 0,
			sizeof(fixture->vertices[i].color));
		fixture->vertices[i].color[channel] = 1;
		fixture->colors[i * 3 + channel] = 255;
	}
}

static int
source_init(struct fixture *fixture)
{
	static const float xy[6][2] = {
		{-4, -4}, {4, -4}, {4, 4}, {-4, -4}, {4, 4}, {-4, 4}};

	static const float uv[6][2] = {
		{0, 1}, {1, 1}, {1, 0}, {0, 1}, {1, 0}, {0, 0}};

	struct mh_error error;
	unsigned int i;

	memset(fixture, 0, sizeof(*fixture));
	memset(&overlay, 0, sizeof(overlay));
	memset(fixture->white, 255, sizeof(fixture->white));
	fixture->image.width = fixture->image.height = 1;
	fixture->image.rgba = fixture->white;
	fixture->mesh.vertex_count = 6;
	fixture->mesh.vertices = fixture->vertices;
	fixture->mesh.draw_count = fixture->mesh.texture_count =
		fixture->mesh.primitive_count = 1;
	fixture->mesh.draws = &fixture->draw;
	fixture->mesh.textures = &fixture->image;
	fixture->mesh.primitives = &fixture->primitive;
	fixture->mesh.material_rgb = fixture->colors;
	fixture->mesh.material_rgb_count = 6;
	fixture->mesh.draw_groups = &fixture->group;
	fixture->draw.count = fixture->primitive.count = 6;
	fixture->primitive.type = 1;
	fixture->matrix[0] = 1;
	fixture->matrix[5] = 1;
	fixture->matrix[10] = fixture->matrix[11] = fixture->matrix[14] = -1;
	fixture->atlas.image = fixture->image;
	overlay.width = 320;
	overlay.height = 200;
	overlay.count = 6;

	for (i = 0; i < 6; i++) {
		fixture->vertices[i].position[0] = xy[i][0];
		fixture->vertices[i].position[1] = xy[i][1];
		fixture->vertices[i].position[2] = -4;
		fixture->vertices[i].opacity = 1;
		fixture->vertices[i].projection_w = 1;
		overlay.vertices[i].position[0] = uv[i][0] * 320;
		overlay.vertices[i].position[1] = uv[i][1] * 200;
		overlay.vertices[i].color[0] = overlay.vertices[i].color[1] =
			overlay.vertices[i].color[3] = 1;
	}

	source_color(fixture, 0);

	return operation(mh_projection_setup(320, 200, 4.0f / 3, 1, 1, 100,
				&fixture->projection, &error),
		&error);
}

static int
frame(struct fixture *fixture, struct mh_renderer *renderer,
	const struct mh_ui_list *ui)
{
	struct mh_error error;

	if (mh_renderer_profile(renderer) == MH_RENDER_GLIDE)
		return rendered(renderer,
			mh_renderer_scene_frame_ui(renderer, fixture->matrix,
				mh_render_identity, NULL, 0, ui));

	return operation(
		mh_renderer_scene_frame_lens(renderer, &fixture->projection,
			mh_render_identity, NULL, 0, ui, NULL, NULL, &error),
		&error);
}

static int
center(struct mh_renderer *renderer, int red, int green, int blue)
{
	struct mh_image image;
	struct mh_error error;
	const unsigned char *pixel;
	int success;

	if (!operation(
		mh_renderer_read_pixels(renderer, &image, &error), &error))
		return 0;

	pixel = image.rgba + ((size_t)100 * image.width + 160) * 4;
	success = image.width == 320 && image.height == 200 &&
		abs(pixel[0] - red) <= 1 && abs(pixel[1] - green) <= 1 &&
		abs(pixel[2] - blue) <= 1;

	if (!success)
		fprintf(stderr, "%s center: %u,%u,%u expected %d,%d,%d\n",
			mh_renderer_name(renderer), pixel[0], pixel[1],
			pixel[2], red, green, blue);

	free(image.rgba);

	return success;
}

static void
profile(struct mh_window *window, const struct mh_render_backend *backend)
{
	struct fixture fixture;
	struct mh_renderer *renderer, *duplicate;
	const struct mh_mesh *mesh;
	struct mh_error error;
	struct mh_depth_probe probe = {1, 160, 100, .5f};
	unsigned char visible;
	struct mh_file tga;

	if (!source_init(&fixture))
		return;

	renderer = duplicate = NULL;
	mesh = &fixture.mesh;

	if (!operation(mh_renderer_open(backend, window, &mesh, 1, 320, 200, 0,
			&renderer, &error),
		&error))
		return;

	printf("Renderer adapter: %s\n", mh_renderer_name(renderer));
	check(mh_renderer_open(backend, window, &mesh, 1, 320, 200, 0,
		&duplicate, &error) == MH_ERROR_STATE &&
			duplicate == NULL,
		"renderer acquisition rejects duplicate window owners");
	rendered(renderer, mh_renderer_mesh_lit(renderer, 0, fixture.vertices));
	check(frame(&fixture, renderer, NULL) && center(renderer, 255, 0, 0),
		"public frame operation draws the selected original profile");
	check(!mh_renderer_mesh_visible(renderer, 1, 0) &&
			mh_renderer_error(renderer, &error) == MH_ERROR_INVALID,
		"invalid mesh operations retain a renderer diagnostic");
	check(mh_renderer_mesh_visible(renderer, 0, 0) &&
			mh_renderer_error(renderer, &error) == MH_ERROR_NONE &&
			frame(&fixture, renderer, NULL) &&
			center(renderer, 14, 18, 24),
		"successful visibility change clears diagnostics and hides the mesh");
	rendered(renderer, mh_renderer_mesh_visible(renderer, 0, 1));
	source_color(&fixture, 1);
	memcpy(fixture.lit, fixture.vertices, sizeof(fixture.lit));
	rendered(renderer, mh_renderer_mesh_lit(renderer, 0, fixture.lit));
	source_color(&fixture, 2);
	check(mh_renderer_mesh_unavailable(renderer, 0, NULL) &&
			frame(&fixture, renderer, NULL) &&
			center(renderer, 0, 255, 0),
		"clearing unavailable state preserves uploaded lit geometry");
	rendered(renderer, mh_renderer_mesh_lit(renderer, 0, fixture.vertices));
	check(frame(&fixture, renderer, NULL) && center(renderer, 0, 0, 255),
		"lit mesh replacement reaches the next completed frame");
	check(mh_renderer_ui_upload(renderer, &fixture.atlas) &&
			frame(&fixture, renderer, &overlay) &&
			center(renderer, 255, 255, 0),
		"scene and UI pass share the completed frame target");
	visible = 0;
	check(mh_renderer_depth_queue(renderer, &probe, 1) &&
			mh_webgl_test_read_depth(renderer, &visible, 1) &&
			visible == 1,
		"UI pass preserves scene depth for queued lamp visibility");
	source_color(&fixture, 0);
	rendered(renderer, mh_renderer_mesh_lit(renderer, 0, fixture.vertices));
	check(frame(&fixture, renderer, NULL) && center(renderer, 255, 0, 0),
		"next scene restores shader state after UI and depth passes");
	check(mh_renderer_set_motion_blur(renderer, 1) &&
			mh_renderer_tron_frame_ui(
				renderer, NULL, 0, &overlay) &&
			center(renderer, 255, 255, 0),
		"TRON preserves the ordinary HUD pass");
	check(mh_renderer_tron_frame_ui(renderer, NULL, 0, NULL) &&
			center(renderer, 0, 0, 0),
		"empty TRON frames clear black even with motion blur enabled");
	check(mh_renderer_set_motion_blur(renderer, 0) &&
			frame(&fixture, renderer, NULL) &&
			center(renderer, 255, 0, 0),
		"ordinary scene drawing resumes after TRON and blur history");
	memset(&tga, 0, sizeof(tga));
	check(mh_renderer_tga_capture(renderer, &tga) &&
			tga.size == 18 + 320 * 200 * 2,
		"hardware TGA capture uses the completed frame");
	free(tga.data);
	failures += mh_webgl_retro_check(renderer);
	check(frame(&fixture, renderer, NULL) && center(renderer, 255, 0, 0),
		"next scene restores shader state after retro rendering");
	check(mh_renderer_set_frame_queue(renderer, 2, &error) ==
				MH_ERROR_UNSUPPORTED &&
			mh_renderer_set_vsync(renderer, 0, &error) ==
				MH_ERROR_UNSUPPORTED,
		"browser presentation controls report unsupported explicitly");
	check(mh_renderer_set_blur_history(renderer, 3, &error) ==
			(backend->profile == MH_RENDER_POWERVR
					? MH_ERROR_UNSUPPORTED
					: MH_ERROR_NONE),
		"profile exposes only its supported blur history");
	check(!mh_renderer_set_motion_blur(renderer, 2) &&
			mh_renderer_error(renderer, &error) == MH_ERROR_INVALID,
		"invalid blur flags are rejected for every profile");
	mh_renderer_destroy(renderer);
}

static struct fixture recovery_fixture;
static struct mh_renderer *recovery_renderer;
static struct mh_window *recovery_window;

static void
recovery_prepare(unsigned int profile_index)
{
	const struct mh_render_backend *backends[3];
	const struct mh_mesh *mesh;
	struct mh_error error;
	unsigned char ui_pixels[4];
	unsigned int i;

	backends[0] = mh_webgl_glide_backend();
	backends[1] = mh_webgl_direct3d_backend();
	backends[2] = mh_webgl_powervr_backend();

	if (recovery_window == NULL) {
		if (!operation(mh_window_system_acquire(
				mh_sdl_window_system(), NULL, &error),
			&error))
			return;

		if (!operation(mh_window_open(mh_sdl_window_system(),
				"Renderer recovery", 320, 200, 0,
				&recovery_window, &error),
			&error))
			return;
	}

	if (!source_init(&recovery_fixture))
		return;

	mesh = &recovery_fixture.mesh;

	if (!operation(
		mh_renderer_open(backends[profile_index], recovery_window,
			&mesh, 1, 320, 200, 0, &recovery_renderer, &error),
		&error))
		return;

	/* Upload green, then change the source to blue without uploading. */
	source_color(&recovery_fixture, 1);
	rendered(recovery_renderer,
		mh_renderer_mesh_lit(
			recovery_renderer, 0, recovery_fixture.vertices));
	source_color(&recovery_fixture, 2);
	memset(ui_pixels, 255, sizeof(ui_pixels));
	recovery_fixture.atlas.image.rgba = ui_pixels;
	rendered(recovery_renderer,
		mh_renderer_ui_upload(
			recovery_renderer, &recovery_fixture.atlas));

	/* UI upload owns its pixels; this caller's temporary atlas expires. */
	memset(ui_pixels, 0, sizeof(ui_pixels));
	recovery_fixture.atlas.image.rgba = NULL;

	if (profile_index != 2)
		operation(mh_renderer_set_blur_history(
				recovery_renderer, 3, &error),
			&error);

	rendered(recovery_renderer,
		mh_renderer_set_motion_blur(recovery_renderer, 1));

	for (i = 0; i < 3; i++)
		check(frame(&recovery_fixture, recovery_renderer, NULL) &&
				center(recovery_renderer, 0, 255, 0),
			"pre-loss uploaded geometry fills frame history");

	rendered(recovery_renderer,
		mh_renderer_mesh_visible(recovery_renderer, 0, 0));
}

static void
recovery_history(void)
{
	int powervr;
	unsigned int i;

	powervr = mh_renderer_profile(recovery_renderer) == MH_RENDER_POWERVR;
	check(frame(&recovery_fixture, recovery_renderer, NULL) &&
			center(recovery_renderer, powervr ? 14 : 0,
				powervr ? 18 : 0, powervr ? 24 : 0),
		"restoration retains hidden state and clears lost history");
	rendered(recovery_renderer,
		mh_renderer_mesh_visible(recovery_renderer, 0, 1));
	check(frame(&recovery_fixture, recovery_renderer, NULL) &&
			center(recovery_renderer, 0, 255, 0),
		"restoration retains uploaded vertices instead of reloading source");
	rendered(recovery_renderer,
		mh_renderer_mesh_visible(recovery_renderer, 0, 0));

	for (i = 0; i < 2; i++)
		check(frame(&recovery_fixture, recovery_renderer, NULL) &&
				center(recovery_renderer, powervr ? 14 : 0,
					powervr ? 18 : 0, powervr ? 24 : 0),
			"restored history keeps the original three-frame cadence");

	check(frame(&recovery_fixture, recovery_renderer, NULL) &&
			center(recovery_renderer, powervr ? 14 : 0,
				powervr ? 18 : 255, powervr ? 24 : 0),
		"blur setting survives loss while PowerVR remains unblurred");
}

static void
recovery_passes(void)
{
	struct mh_depth_probe probe = {1, 160, 100, .5f};
	unsigned char visible;

	rendered(recovery_renderer,
		mh_renderer_mesh_visible(recovery_renderer, 0, 1));
	check(frame(&recovery_fixture, recovery_renderer, &overlay) &&
			center(recovery_renderer, 255, 255, 0),
		"restored scene and HUD draw together");
	visible = 0;
	check(mh_renderer_depth_queue(recovery_renderer, &probe, 1) &&
			mh_webgl_test_read_depth(
				recovery_renderer, &visible, 1) &&
			visible,
		"depth probes rebuild after context loss");
	check(mh_renderer_tron_frame_ui(recovery_renderer, NULL, 0, &overlay) &&
			center(recovery_renderer, 255, 255, 0),
		"TRON and HUD rebuild after context loss");
	failures += mh_webgl_retro_check(recovery_renderer);
}

int
mh_webgl_renderer_recovery(unsigned int step, unsigned int profile_index)
{
	struct mh_error error;

	failures = 0;

	if (step == 0)
		recovery_prepare(profile_index);
	else if (step == 1) {
		check(!mh_renderer_scene_frame_ui(recovery_renderer,
			recovery_fixture.matrix, mh_render_identity, NULL,
			0, NULL) &&
				mh_renderer_error(recovery_renderer, &error) ==
					MH_ERROR_BACKEND,
			"lost renderer rejects drawing safely");
	} else if (step == 2) {
		recovery_history();
		recovery_passes();
	} else if (step == 3) {
		/*
		 * Force a recoverable rebuild failure after textures were
		 * created.
		 */
		recovery_fixture.mesh.vertex_count = 9;
		check(!mh_renderer_scene_frame_ui(recovery_renderer,
			recovery_fixture.matrix, mh_render_identity, NULL,
			0, NULL) &&
				mh_renderer_error(recovery_renderer, &error) ==
					MH_ERROR_INVALID,
			"failed restoration reports invalid source capacity");
		recovery_fixture.mesh.vertex_count = 6;
	} else if (step == 4) {
		rendered(recovery_renderer,
			mh_renderer_mesh_visible(recovery_renderer, 0, 0));
	} else if (step == 5) {
		mh_renderer_destroy(recovery_renderer);
		recovery_renderer = NULL;
	} else {
		mh_renderer_destroy(recovery_renderer);
		recovery_renderer = NULL;
		mh_window_destroy(recovery_window);
		recovery_window = NULL;
		mh_window_system_release(mh_sdl_window_system());
	}

	return failures != 0;
}

int	mh_webgl_renderer_check(void);

int
mh_webgl_renderer_check(void)
{
	const struct mh_window_system *system;
	struct mh_window *window;
	struct mh_error error;

	failures = 0;
	system = mh_sdl_window_system();
	window = NULL;

	if (!operation(mh_window_system_acquire(system, NULL, &error), &error))
		return 1;

	if (operation(mh_window_open(system, "WebGL renderer check", 320, 200,
			0, &window, &error),
		&error)) {
		profile(window, mh_webgl_glide_backend());
		profile(window, mh_webgl_direct3d_backend());
		profile(window, mh_webgl_powervr_backend());
	}

	mh_window_destroy(window);
	mh_window_system_release(system);

	return failures != 0;
}

int
main(void)
{
	return 0;
}
