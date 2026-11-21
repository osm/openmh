#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/geometry/world.h"
#include "engine/render/renderer.h"

#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "line %d: %s\n", __LINE__, #test);     \
			return 1;                                              \
		}                                                              \
	} while (0)

struct test_renderer {
	struct mh_renderer			base;
	unsigned int	width, height, frames, frame_queue, blur_history, flags;
	size_t					count, updated;
	const struct mh_mesh			*const *meshes;
	const float				*matrix, *view, *models;
	const struct mh_projection		*projection;
	const struct mh_ui_list			*ui;
	const struct mh_retro_draw		*retro;
	const struct mh_renderer_effects	*effects;
	int					reflections, vsync;
};

static char backend_error[64] = "test renderer failure";

static unsigned int opens, destroys, software_destroys;

static enum mh_error_code
open_renderer(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	struct test_renderer *renderer;

	(void)window;
	(void)context;
	opens++;

	if (width == 0)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "test width failure");

	renderer = (struct test_renderer *)calloc(1, sizeof(*renderer));

	if (renderer == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test allocation failure");

	renderer->flags = flags;
	renderer->width = width;
	renderer->height = height;
	renderer->meshes = meshes;
	renderer->count = count;
	*output = &renderer->base;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
scene_order(struct mh_renderer *renderer,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;

	(void)parts;
	(void)count;

	return 0;
}

static int
update_mesh(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	struct test_renderer *test;

	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	test = (struct test_renderer *)renderer;
	test->updated = index;

	return 1;
}

static int
mesh_visible(struct mh_renderer *renderer, size_t index, int visible,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)index;
	(void)visible;

	return 0;
}

static int
mesh_lit(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;

	(void)index;
	(void)vertices;

	return 0;
}

static int
mesh_unavailable(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;

	(void)index;
	(void)vertices;

	return 0;
}

static int
mesh_skids(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)index;

	return 0;
}

static int
mesh_shadows(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)index;

	return 0;
}

static int
mesh_particles(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)index;

	return 0;
}

static int
mesh_sky(struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)index;

	return 0;
}

static int
mesh_halos(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)index;

	return 0;
}

static int
depth_queue(struct mh_renderer *renderer, const struct mh_depth_probe *probes,
	size_t count, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;

	(void)probes;
	(void)count;

	return 0;
}

static int
depth_read(struct mh_renderer *renderer, unsigned char *visible, size_t count,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)visible;
	(void)count;

	return 0;
}

static int
set_fog(struct mh_renderer *renderer, const struct mh_fog *fog,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)fog;

	return 0;
}

static int
set_motion_blur(
	struct mh_renderer *renderer, int enabled, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)enabled;

	return 0;
}

static int
set_water(struct mh_renderer *renderer, const struct mh_water_frame *frame,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)frame;

	return 0;
}

static int
ui_upload(struct mh_renderer *renderer, const struct mh_ui_atlas *atlas,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)atlas;

	return 0;
}

static int
scene_frame_retro(struct mh_renderer *renderer, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
	struct mh_error *error_out)
{
	struct test_renderer *test;

	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	test = (struct test_renderer *)renderer;
	test->matrix = matrix;
	test->view = view;
	test->models = models;
	test->reflections = reflections;
	test->ui = ui;
	test->retro = retro;
	test->frames++;

	return (int)test->width;
}

static int
tron_frame_ui(struct mh_renderer *renderer,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;

	(void)faces;
	(void)count;
	(void)ui;

	return 0;
}

static int
tron_scene_ui(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;

	(void)projection;
	(void)view;
	(void)models;
	(void)reflections;
	(void)ui;

	return 0;
}

static void
destroy(struct mh_renderer *renderer)
{
	destroys++;
	free(renderer);
}

static int
capture(struct mh_renderer *renderer, const char *path,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)path;

	return 0;
}

static int
tga_picture(struct mh_renderer *renderer, struct mh_file *output,
	struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;
	(void)output;

	return 0;
}

/* Different operations ensure dispatch follows the instance, not a global. */
static int
software_blur(
	struct mh_renderer *renderer, int enabled, struct mh_error *error_out)
{
	mh_error_set(error_out, MH_ERROR_BACKEND, backend_error);

	(void)renderer;

	return enabled;
}

static void
software_destroy(struct mh_renderer *renderer)
{
	software_destroys++;
	destroy(renderer);
}

static enum mh_error_code
set_depth_cue(struct mh_renderer *renderer, const struct mh_depth_cue *cue,
	struct mh_error *error_out)
{
	(void)cue;

	return mh_error_set(error_out,
		renderer->backend->profile == MH_RENDER_SOFTWARE
			? MH_ERROR_NONE
			: MH_ERROR_UNSUPPORTED,
		renderer->backend->profile == MH_RENDER_SOFTWARE
			? NULL
			: "test depth cue");
}

static enum mh_error_code
set_frame_queue(struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out)
{
	struct test_renderer *test;

	test = (struct test_renderer *)renderer;
	test->frame_queue = frames;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_blur_history(struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out)
{
	struct test_renderer *test;

	test = (struct test_renderer *)renderer;
	test->blur_history = frames;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_texture_filter(struct mh_renderer *renderer, enum mh_texture_filter filter,
	struct mh_error *error_out)
{
	(void)renderer;

	(void)filter;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"software retains its original texture sampling");
}

static enum mh_error_code
set_texture_mapping(struct mh_renderer *renderer,
	enum mh_texture_mapping mapping, unsigned int block_pixels,
	struct mh_error *error_out)
{
	(void)mapping;

	(void)block_pixels;

	if (renderer->backend->profile != MH_RENDER_SOFTWARE)
		return mh_error_set(
			error_out, MH_ERROR_UNSUPPORTED, "test texture policy");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_panorama(struct mh_renderer *renderer, const struct mh_panorama *panorama,
	struct mh_error *error_out)
{
	(void)panorama;

	return set_depth_cue(renderer, NULL, error_out);
}

static enum mh_error_code
mesh_glenz(struct mh_renderer *renderer, size_t index,
	const struct mh_renderer_glenz *glenz, struct mh_error *error_out)
{
	(void)renderer;

	(void)index;
	(void)glenz;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"Glenz materials require the GPU renderer");
}

static enum mh_error_code
world_lighting(struct mh_renderer *renderer, size_t index, size_t root,
	const struct mh_world *world, const struct mh_model_lighting *lighting,
	struct mh_error *error_out)
{
	(void)index;

	(void)root;

	(void)world;
	(void)lighting;

	return set_depth_cue(renderer, NULL, error_out);
}

static enum mh_error_code
set_environment(struct mh_renderer *renderer,
	const struct mh_indexed_image *const maps[3],
	struct mh_error *error_out)
{
	(void)maps;

	return set_depth_cue(renderer, NULL, error_out);
}

static enum mh_error_code
model_lighting(struct mh_renderer *renderer, size_t index,
	const struct mh_model_lighting *lighting, struct mh_error *error_out)
{
	(void)index;

	(void)lighting;

	return set_depth_cue(renderer, NULL, error_out);
}

static enum mh_error_code
scene_frame_lens(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out)
{
	struct test_renderer *test;

	test = (struct test_renderer *)renderer;
	test->projection = projection;
	test->view = view;
	test->models = models;
	test->reflections = reflections;
	test->ui = ui;
	test->retro = retro;
	test->effects = effects;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_profiles(const struct mh_render_backend *glide,
	const struct mh_render_backend *software,
	const struct mh_render_backend *direct3d)
{
	struct mh_render_set renderers;
	struct mh_render_backend powervr;

	renderers.powervr = NULL;
	renderers.glide = glide;
	renderers.software = software;
	renderers.ui = software;
	renderers.direct3d = direct3d;
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_DIRECT3D) ==
		direct3d);
	renderers.direct3d = glide;
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_DIRECT3D) == NULL);
	renderers.direct3d = NULL;
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_DIRECT3D) == NULL);
	renderers.direct3d = direct3d;
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_GLIDE) == glide);
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_SOFTWARE) ==
		software);
	renderers.software = glide;
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_SOFTWARE) == NULL);
	renderers.software = NULL;
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_SOFTWARE) == NULL);
	CHECK(mh_render_set_profile(NULL, MH_RENDER_GLIDE) == NULL);
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_POWERVR) == NULL);
	powervr = *glide;
	powervr.profile = MH_RENDER_POWERVR;
	renderers.powervr = &powervr;
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_POWERVR) == &powervr);
	renderers.powervr = glide;
	CHECK(mh_render_set_profile(&renderers, MH_RENDER_POWERVR) == NULL);
	renderers.powervr = &powervr;
	CHECK(mh_render_set_profile(&renderers, (enum mh_render_profile)99) ==
		NULL);
	renderers.software = software;

	return 0;
}

static enum mh_error_code
set_vsync(struct mh_renderer *renderer, int enabled, struct mh_error *error_out)
{
	((struct test_renderer *)renderer)->vsync = enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(void)
{
	static const struct mh_render_ops ops = {scene_order, update_mesh,
		mesh_visible, mesh_lit, mesh_unavailable, mesh_skids,
		mesh_shadows, mesh_particles, mesh_sky, mesh_halos, depth_queue,
		depth_read, set_fog, set_motion_blur, set_water, ui_upload,
		scene_frame_retro, tron_frame_ui, tron_scene_ui, destroy,
		capture, tga_picture, set_texture_mapping, scene_frame_lens,
		set_depth_cue, set_panorama, model_lighting, set_environment,
		world_lighting, mesh_glenz, set_texture_filter, set_frame_queue,
		set_blur_history, NULL, set_vsync};

	static const struct mh_render_backend glide = {
		"test glide", MH_RENDER_GLIDE, open_renderer, &ops, NULL};

	struct mh_render_backend software, direct3d, incomplete;
	struct mh_render_ops software_ops, missing;
	struct mh_renderer *first, *second, *output;
	struct test_renderer *glide_state, *software_state;
	struct mh_error error;
	struct mh_mesh mesh;
	struct mh_projection projection;
	struct mh_renderer_effects effects;
	const struct mh_mesh *meshes[1];
	struct mh_ui_list ui;
	struct mh_retro_draw retro;
	float matrix[16], view[16], models[16];

	software_ops = ops;
	software_ops.set_frame_queue = NULL;
	software_ops.set_vsync = NULL;
	software_ops.set_blur_history = NULL;
	software_ops.set_motion_blur = software_blur;
	software_ops.destroy = software_destroy;
	software = glide;
	software.name = "test software";
	software.profile = MH_RENDER_SOFTWARE;
	software.ops = &software_ops;
	direct3d = glide;
	direct3d.name = "test Direct3D";
	direct3d.profile = MH_RENDER_DIRECT3D;

	if (check_profiles(&glide, &software, &direct3d))
		return 1;

	memset(&mesh, 0, sizeof(mesh));
	memset(&ui, 0, sizeof(ui));
	memset(&retro, 0, sizeof(retro));
	memset(matrix, 0, sizeof(matrix));
	memset(view, 0, sizeof(view));
	memset(models, 0, sizeof(models));
	meshes[0] = &mesh;
	first = second = NULL;
	mh_error_set(&error, MH_ERROR_FAILED, "stale error");
	CHECK(mh_renderer_open(&glide, NULL, meshes, 1, 320, 200,
		MH_RENDERER_DIAGNOSTICS, &first,
		&error) == MH_ERROR_NONE);
	CHECK(error.code == MH_ERROR_NONE && error.message == NULL);
	CHECK(mh_renderer_open(&software, NULL, NULL, 0, 640, 480, 0, &second,
		NULL) == MH_ERROR_NONE);
	glide_state = (struct test_renderer *)first;
	software_state = (struct test_renderer *)second;
	CHECK(opens == 2 && first != second);
	CHECK(mh_renderer_set_vsync(first, 1, NULL) == MH_ERROR_NONE);
	CHECK(glide_state->vsync == 1 && software_state->vsync == 0);
	CHECK(mh_renderer_set_vsync(first, 0, NULL) == MH_ERROR_NONE);
	CHECK(glide_state->vsync == 0);
	CHECK(mh_renderer_set_vsync(first, 2, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_vsync(second, 1, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(glide_state->meshes == meshes && glide_state->count == 1 &&
		glide_state->height == 200);
	CHECK(software_state->meshes == NULL && software_state->count == 0 &&
		software_state->height == 480);
	CHECK(mh_renderer_profile(first) == MH_RENDER_GLIDE);
	CHECK(mh_renderer_profile(second) == MH_RENDER_SOFTWARE);
	CHECK(strcmp(mh_renderer_name(first), "test glide") == 0);
	CHECK(strcmp(mh_renderer_name(second), "test software") == 0);

	/* Interleaved instances never share dispatch or frame state. */
	CHECK(mh_renderer_scene_frame_retro(
		first, matrix, view, models, 1, &ui, &retro) == 320);
	CHECK(glide_state->matrix == matrix && glide_state->view == view &&
		glide_state->models == models);
	CHECK(glide_state->ui == &ui && glide_state->retro == &retro &&
		glide_state->reflections == 1);
	CHECK(software_state->frames == 0);
	CHECK(mh_renderer_ui_frame(second, &ui) == 640);
	CHECK(software_state->matrix == NULL && software_state->view == NULL &&
		software_state->models == NULL);
	CHECK(software_state->ui == &ui && software_state->retro == NULL &&
		software_state->reflections == 0);
	CHECK(glide_state->frames == 1 && software_state->frames == 1);
	CHECK(mh_renderer_frame(first, matrix, view, 1) == 320);
	CHECK(glide_state->models == NULL && glide_state->ui == NULL &&
		glide_state->retro == NULL);
	CHECK(glide_state->matrix == matrix && glide_state->view == view &&
		glide_state->reflections == 1);
	CHECK(mh_renderer_update_mesh(second, 5));
	CHECK(software_state->updated == 5);
	CHECK(mh_renderer_update_vertices(second));
	CHECK(software_state->updated == 0);
	CHECK(!mh_renderer_set_motion_blur(first, 1));
	CHECK(mh_renderer_error(first, &error) == MH_ERROR_BACKEND);
	CHECK(strcmp(error.message, "test renderer failure") == 0);
	strcpy(backend_error, "another renderer failure");
	CHECK(!mh_renderer_set_fog(second, NULL));
	CHECK(mh_renderer_error(second, &error) == MH_ERROR_BACKEND);
	CHECK(strcmp(error.message, "another renderer failure") == 0);
	CHECK(mh_renderer_error(first, &error) == MH_ERROR_BACKEND);
	CHECK(strcmp(error.message, "test renderer failure") == 0);
	CHECK(mh_renderer_update_mesh(second, 0));
	CHECK(mh_renderer_error(second, &error) == MH_ERROR_NONE);
	CHECK(error.message == NULL);
	CHECK(mh_renderer_error(NULL, &error) == MH_ERROR_INVALID);

	CHECK(mh_renderer_set_motion_blur(second, 1));
	CHECK(!mh_renderer_set_motion_blur(first, 1));

	CHECK(mh_renderer_set_texture_mapping(first, MH_TEXTURE_SELECTIVE, 16,
		&error) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_set_texture_mapping(
		second, MH_TEXTURE_SELECTIVE, 16, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_set_texture_mapping(NULL, MH_TEXTURE_SELECTIVE, 16,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_depth_cue(first, NULL, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_set_depth_cue(second, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_set_depth_cue(NULL, NULL, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_panorama(first, NULL, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_set_panorama(second, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_set_panorama(NULL, NULL, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_frame_queue(NULL, 2, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_frame_queue(first, 2, NULL) == MH_ERROR_NONE &&
		glide_state->frame_queue == 2);
	CHECK(mh_renderer_set_frame_queue(first, 0, NULL) == MH_ERROR_INVALID &&
		glide_state->frame_queue == 2);
	CHECK(mh_renderer_set_frame_queue(first, 4, NULL) == MH_ERROR_INVALID &&
		glide_state->frame_queue == 2);
	CHECK(mh_renderer_set_frame_queue(first, 3, NULL) == MH_ERROR_NONE &&
		glide_state->frame_queue == 3);
	CHECK(mh_renderer_set_frame_queue(second, 2, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_set_blur_history(NULL, 2, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_blur_history(first, 3, NULL) == MH_ERROR_NONE &&
		glide_state->blur_history == 3 &&
		glide_state->frame_queue == 3);
	CHECK(mh_renderer_set_blur_history(first, 2, NULL) == MH_ERROR_NONE &&
		glide_state->blur_history == 2 &&
		glide_state->frame_queue == 3);
	CHECK(mh_renderer_set_blur_history(first, 1, NULL) ==
			MH_ERROR_INVALID &&
		glide_state->blur_history == 2);
	CHECK(mh_renderer_set_blur_history(first, 4, NULL) ==
			MH_ERROR_INVALID &&
		glide_state->blur_history == 2);
	CHECK(mh_renderer_set_blur_history(second, 3, NULL) ==
		MH_ERROR_UNSUPPORTED);

	CHECK(mh_renderer_set_texture_filter(NULL, MH_TEXTURE_FILTER_ORIGINAL,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_texture_filter(first, (enum mh_texture_filter)99,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_texture_filter(first, MH_TEXTURE_FILTER_TRILINEAR,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_mesh_glenz(NULL, 0, NULL, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_mesh_glenz(first, 0, NULL, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_mesh_glenz(second, 0, NULL, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_set_environment(first, NULL, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_set_environment(second, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_set_environment(NULL, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_renderer_world_lighting(first, 0, 0, NULL, NULL, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_world_lighting(second, 0, 0, NULL, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_world_lighting(NULL, 0, 0, NULL, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_renderer_model_lighting(first, 0, NULL, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_model_lighting(second, 0, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_model_lighting(NULL, 0, NULL, NULL) ==
		MH_ERROR_INVALID);

	memset(&effects, 0, sizeof(effects));
	memset(&projection, 0, sizeof(projection));
	CHECK(mh_renderer_scene_frame_lens(first, &projection, view, models, 1,
		&ui, &retro, &effects, &error) == MH_ERROR_NONE);
	CHECK(glide_state->projection == &projection &&
		glide_state->view == view && glide_state->models == models);
	CHECK(glide_state->reflections == 1 && glide_state->ui == &ui &&
		glide_state->retro == &retro);
	CHECK(glide_state->effects == &effects &&
		software_state->effects == NULL);
	CHECK(software_state->projection == NULL &&
		error.code == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(first, NULL, view, models, 0, NULL,
		NULL, NULL, NULL) == MH_ERROR_INVALID);
	CHECK(glide_state->projection == &projection);

	/* Failure preserves the caller's handle and original diagnostic. */
	output = first;
	CHECK(glide_state->flags == MH_RENDERER_DIAGNOSTICS &&
		software_state->flags == 0);
	CHECK(mh_renderer_open(&software, NULL, NULL, 0, 640, 480,
		MH_RENDERER_DIAGNOSTICS << 1, &output,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_open(&software, NULL, NULL, 0, 0, 480, 0, &output,
		&error) == MH_ERROR_RANGE);
	CHECK(output == first && error.code == MH_ERROR_RANGE);
	CHECK(strcmp(error.message, "test width failure") == 0);
	CHECK(opens == 3 && destroys == 0);
	missing = ops;
	missing.tga = NULL;
	incomplete = software;
	incomplete.ops = &missing;
	CHECK(mh_renderer_open(&incomplete, NULL, NULL, 0, 640, 480, 0, &output,
		&error) == MH_ERROR_UNSUPPORTED);
	CHECK(opens == 3 && output == first);
	CHECK(mh_renderer_open(NULL, NULL, NULL, 0, 640, 480, 0, &output,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_renderer_open(&glide, NULL, NULL, 0, 640, 480, 0, NULL,
		NULL) == MH_ERROR_INVALID);
	mh_renderer_destroy(first);
	CHECK(destroys == 1 && software_destroys == 0);
	CHECK(mh_renderer_ui_frame(second, NULL) == 640);
	CHECK(software_state->frames == 2);
	mh_renderer_destroy(second);
	mh_renderer_destroy(NULL);
	CHECK(destroys == 2 && software_destroys == 1);
	output = NULL;
	CHECK(mh_renderer_open(&direct3d, NULL, NULL, 0, 640, 480, 0, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_profile(output) == MH_RENDER_DIRECT3D);
	CHECK(mh_renderer_scene_frame_lens(output, &projection, view, models, 1,
		&ui, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(((struct test_renderer *)output)->projection == &projection);
	mh_renderer_destroy(output);
	CHECK(destroys == 3 && software_destroys == 1);
	puts("Renderer selection, instance isolation and failure ownership passed");

	return 0;
}
