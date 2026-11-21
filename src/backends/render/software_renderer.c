#include <stdlib.h>
#include <string.h>

#include "backends/render/software_provider.h"
#include "backends/render/software_renderer_internal.h"
#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "engine/render/submission.h"

#define SOFTWARE_MESH_LIMIT 128
#define PERSPECTIVE_SHIFT_MIN 2
#define PERSPECTIVE_SHIFT_DEFAULT 5

static int
failed(struct mh_error *error_out, enum mh_error_code code, const char *message)
{
	mh_error_set(error_out, code, message);

	return 0;
}

static void
destroy(struct mh_renderer *renderer)
{
	struct mh_software_renderer *software;
	struct mh_software_mesh *mesh;
	size_t i;

	software = (struct mh_software_renderer *)renderer;
	mh_software_presentation_close(software->presentation);
	mh_software_environment_free(&software->environment);
	mh_software_ui_free(&software->ui);
	mh_software_skids_free(&software->skids);
	mh_software_particles_free(&software->particles);
	mh_software_halos_free(&software->halos);

	if (software->meshes != NULL)
		for (i = 0; i < software->mesh_count; i++) {
			mesh = &software->meshes[i];
			free(mesh->vertices);
			mh_software_model_light_free(&mesh->lighting);
			mh_software_world_light_free(&mesh->world_light);
		}

	free(software->meshes);
	free(software->parts);
	free(software->triangles);
	free(software->spans);
	free(software->surface.pixels);
	free(software->work.pixels);
	free(software->history[0]);
	free(software->history[1]);
	free(software->history_pending);
	free(software->sky.image.pixels);
	free(software);
}

static enum mh_error_code
open_renderer(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	struct mh_software_mesh *mesh;
	size_t i, vertex_count;
	enum mh_error_code status;

	(void)flags;

	if (width < MH_VIEW_MIN_WIDTH || width > MH_VIEW_MAX_WIDTH ||
		height < MH_VIEW_MIN_HEIGHT || height > MH_VIEW_MAX_HEIGHT ||
		count > SOFTWARE_MESH_LIMIT || (count != 0 && meshes == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software scene dimensions or meshes");

	vertex_count = 0;

	if (width > MH_SOFTWARE_VIEW_MAX_WIDTH)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"software renderer supports widths up to 1023 pixels");

	for (i = 0; i < count; i++) {
		if (!mh_software_mesh_supported(meshes[i]))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported software mesh material or geometry");

		if (meshes[i]->vertex_count >
			MH_SOFTWARE_VERTEX_MAX - vertex_count)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software scene exceeds vertex limit");

		vertex_count += meshes[i]->vertex_count;
	}

	if (vertex_count > (size_t)-1 / sizeof(struct mh_vertex) ||
		vertex_count / 3 >
			(size_t)-1 / sizeof(struct mh_software_order_span) ||
		vertex_count / 3 > (size_t)-1 / 2 /
				sizeof(struct mh_software_draw_triangle) ||
		(size_t)width > (size_t)-1 / 2 / height)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software scene allocation overflow");

	software = (struct mh_software_renderer *)calloc(1, sizeof(*software));

	if (software == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating software renderer");

	software->mesh_count = count;
	software->mapping = MH_TEXTURE_SELECTIVE;
	software->perspective_shift = PERSPECTIVE_SHIFT_DEFAULT;
	software->surface.width = width;
	software->surface.height = height;
	software->surface.pitch = (size_t)width * 2;
	software->surface.size = software->surface.pitch * height;
	software->surface.bits[0] = software->surface.bits[2] = 5;
	software->surface.bits[1] = 6;
	software->surface.shift[0] = 11;
	software->surface.shift[1] = 5;
	software->surface.pixels =
		(unsigned char *)calloc(1, software->surface.size);
	software->work = software->surface;
	software->work.pixels = malloc(software->surface.size);
	software->meshes = (struct mh_software_mesh *)calloc(
		count ? count : 1, sizeof(*software->meshes));

	/* A near-clipped source triangle can become a quad. */
	software->triangle_capacity = (vertex_count / 3) * 2;
	software->span_capacity = vertex_count / 3;
	software->mesh_triangle_capacity = software->triangle_capacity;
	software->mesh_span_capacity = software->span_capacity;
	software->triangles = (struct mh_software_draw_triangle *)calloc(
		software->triangle_capacity ? software->triangle_capacity : 1,
		sizeof(*software->triangles));

	software->spans = (struct mh_software_order_span *)calloc(
		vertex_count / 3 ? vertex_count / 3 : 1,
		sizeof(*software->spans));

	if (software->surface.pixels == NULL || software->work.pixels == NULL ||
		software->meshes == NULL || software->triangles == NULL ||
		software->spans == NULL)
		goto failure;

	for (i = 0; i < count; i++) {
		mesh = &software->meshes[i];
		mesh->source = meshes[i];
		mesh->vertex_count = meshes[i]->vertex_count;
		mesh->visible = 1;
		mesh->vertices = (struct mh_vertex *)malloc(
			meshes[i]->vertex_count * sizeof(struct mh_vertex));

		if (mesh->vertices == NULL)
			goto failure;

		memcpy(mesh->vertices, meshes[i]->vertices,
			meshes[i]->vertex_count * sizeof(struct mh_vertex));
	}

	status = mh_software_presentation_open(context, window, width, height,
		&software->presentation, error_out);

	if (status != MH_ERROR_NONE) {
		destroy(&software->base);
		return status;
	}

	*output = &software->base;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

failure:
	destroy(&software->base);

	return mh_error_set(error_out, MH_ERROR_MEMORY,
		"out of memory preparing software scene");
}

static int
update_mesh(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	struct mh_software_mesh *mesh;

	software = (struct mh_software_renderer *)renderer;

	if (index >= software->mesh_count)
		return failed(error_out, MH_ERROR_INVALID,
			"invalid software mesh update");

	mesh = &software->meshes[index];

	if (!mh_software_mesh_supported(mesh->source) ||
		mesh->source->vertex_count != mesh->vertex_count)
		return failed(error_out, MH_ERROR_INVALID,
			"invalid software mesh update");

	memcpy(mesh->vertices, mesh->source->vertices,
		mesh->vertex_count * sizeof(*mesh->vertices));

	return 1;
}

static int
mesh_visible(struct mh_renderer *renderer, size_t index, int visible,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;

	software = (struct mh_software_renderer *)renderer;

	if (index >= software->mesh_count)
		return failed(error_out, MH_ERROR_INVALID,
			"invalid software visibility mesh");

	software->meshes[index].visible = visible != 0;

	return 1;
}

static enum mh_error_code
read_pixels(struct mh_renderer *renderer, struct mh_image *output,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	const struct mh_software_surface *surface;
	struct mh_image image;
	unsigned char *rgba;
	const unsigned char *row;
	unsigned int x, y, word, red, green, blue;

	software = (struct mh_software_renderer *)renderer;
	surface = &software->surface;

	if (!software->frame_ready)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"software frame is not ready");

	image.width = surface->width;
	image.height = surface->height;
	image.rgba = malloc((size_t)image.width * image.height * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading software pixels");

	rgba = image.rgba;

	for (y = 0; y < image.height; y++) {
		row = surface->pixels + (size_t)y * surface->pitch;

		for (x = 0; x < image.width; x++) {
			word = mh_read_u16le(row + (size_t)x * 2);
			red = word >> 11;
			green = (word >> 5) & 63U;
			blue = word & 31U;
			rgba[0] = (unsigned char)((red << 3) | (red >> 2));
			rgba[1] = (unsigned char)((green << 2) | (green >> 4));
			rgba[2] = (unsigned char)((blue << 3) | (blue >> 2));
			rgba[3] = 255;
			rgba += 4;
		}
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
tga_picture(struct mh_renderer *renderer, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	struct mh_picture picture;

	software = (struct mh_software_renderer *)renderer;

	if (!software->frame_ready)
		return failed(error_out, MH_ERROR_STATE,
			"software frame is not ready");

	memset(&picture, 0, sizeof(picture));
	picture.format = MH_PICTURE_RGB565;
	picture.width = software->surface.width;
	picture.height = software->surface.height;
	picture.pitch = software->surface.pitch;
	picture.pixels = software->surface.pixels;
	picture.pixel_size = software->surface.size;

	return mh_image_tga_encode(&picture, output, error_out) ==
		MH_ERROR_NONE;
}

static int
scene_order(struct mh_renderer *renderer,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	struct mh_renderer_scene_part *copy;
	size_t i;

	software = (struct mh_software_renderer *)renderer;

	if (mh_scene_submission_check(parts, count, software->mesh_count,
		error_out) != MH_ERROR_NONE)
		return 0;

	if (count == 0) {
		free(software->parts);
		software->parts = NULL;
		software->part_count = 0;
		return 1;
	}

	for (i = 0; i < software->mesh_count; i++)
		if (mh_scene_submission_mesh_check(software->meshes[i].source,
			i, parts, count, error_out) != MH_ERROR_NONE)
			return 0;

	copy = (struct mh_renderer_scene_part *)malloc(count * sizeof(*copy));

	if (copy == NULL)
		return failed(error_out, MH_ERROR_MEMORY,
			"out of memory retaining software scene order");

	memcpy(copy, parts, count * sizeof(*copy));
	free(software->parts);
	software->parts = copy;
	software->part_count = count;

	return 1;
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
	struct mh_software_renderer *software;
	const struct mh_mesh *source;
	enum mh_error_code status;

	software = (struct mh_software_renderer *)renderer;

	if (index >= software->mesh_count || root >= software->mesh_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid world lighting mesh or root");

	source = software->meshes[index].source;

	if (source->scenery != NULL) {
		if (root != index ||
			(lighting != NULL && lighting->mode != MH_LIGHT_OBJECT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid packed scenery lighting");
	} else if (source->world != NULL || source->faces == NULL ||
		source->face_count != source->primitive_count ||
		software->meshes[root].source->world != NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"world lighting requires retained model faces");

	status = mh_software_world_light_set(
		&software->meshes[index].world_light, world, lighting,
		error_out);

	if (status == MH_ERROR_NONE) {
		software->meshes[index].light_root = root;
		mh_software_model_light_free(&software->meshes[index].lighting);
	}

	return status;
}

static enum mh_error_code
set_environment(struct mh_renderer *renderer,
	const struct mh_indexed_image *const maps[3],
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;

	software = (struct mh_software_renderer *)renderer;

	return mh_software_environment_set(
		&software->environment, maps, error_out);
}

static enum mh_error_code
model_lighting(struct mh_renderer *renderer, size_t index,
	const struct mh_model_lighting *lighting, struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	const struct mh_mesh *mesh;
	enum mh_error_code status;

	software = (struct mh_software_renderer *)renderer;

	if (index >= software->mesh_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software lighting mesh index");

	mesh = software->meshes[index].source;

	if (mesh->world != NULL || mesh->scenery != NULL ||
		mesh->faces == NULL ||
		mesh->face_count != mesh->primitive_count)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"software lighting requires retained standalone model faces");

	status = mh_software_model_light_set(
		&software->meshes[index].lighting, lighting, error_out);

	if (status == MH_ERROR_NONE)
		mh_software_world_light_free(
			&software->meshes[index].world_light);

	return status;
}

/* Unsupported paths fail; this provider never falls back to GPU drawing. */

static int
unsupported_vertices(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices, struct mh_error *error_out)
{
	(void)renderer;

	(void)index;
	(void)vertices;

	return failed(error_out, MH_ERROR_UNSUPPORTED,
		"software vertex overrides are unavailable");
}

static int
mesh_shadows(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	struct mh_software_renderer *software;

	software = (struct mh_software_renderer *)renderer;

	if (index >= software->mesh_count ||
		!mh_software_mesh_supported(software->meshes[index].source) ||
		!mh_software_mesh_shadow(software->meshes[index].source))
		return failed(error_out, MH_ERROR_INVALID,
			"invalid software shadow mesh");

	software->meshes[index].shadow = 1;

	return 1;
}

static int
unsupported_effect(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	(void)renderer;

	(void)index;

	return failed(error_out, MH_ERROR_UNSUPPORTED,
		"effect requires software scene submission");
}

static int
depth_queue(struct mh_renderer *renderer, const struct mh_depth_probe *probes,
	size_t count, struct mh_error *error_out)
{
	(void)renderer;

	(void)probes;

	return count == 0 ? 1
			: failed(error_out, MH_ERROR_UNSUPPORTED,
				"software depth queries are unavailable");
}

static int
depth_read(struct mh_renderer *renderer, unsigned char *visible, size_t count,
	struct mh_error *error_out)
{
	(void)renderer;

	(void)visible;

	return count == 0 ? 1
			: failed(error_out, MH_ERROR_UNSUPPORTED,
				"software depth queries are unavailable");
}

static int
set_fog(struct mh_renderer *renderer, const struct mh_fog *fog,
	struct mh_error *error_out)
{
	(void)renderer;

	return fog == NULL ? 1
			: failed(error_out, MH_ERROR_UNSUPPORTED,
				"software renderer uses depth cue");
}

static enum mh_error_code
set_depth_cue(struct mh_renderer *renderer, const struct mh_depth_cue *cue,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;

	software = (struct mh_software_renderer *)renderer;

	if (cue != NULL && cue->gradient != NULL && software->sky.height_cue)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"panorama and explicit cue gradients cannot be combined");

	software->cue_enabled = cue != NULL;

	if (cue != NULL)
		software->cue = *cue;

	if (cue != NULL && cue->gradient != NULL) {
		software->gradient = *cue->gradient;
		software->cue.gradient = &software->gradient;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_panorama(struct mh_renderer *renderer, const struct mh_panorama *panorama,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;

	software = (struct mh_software_renderer *)renderer;

	if (panorama != NULL && panorama->height_cue && software->cue_enabled &&
		software->cue.gradient != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"panorama and explicit cue gradients cannot be combined");

	return mh_software_panorama_set(&software->sky, panorama,
		software->surface.width, software->surface.height,
		software->surface.bits[1], error_out);
}

static int
set_motion_blur(
	struct mh_renderer *renderer, int enabled, struct mh_error *error_out)
{
	struct mh_software_renderer *software;
	unsigned char *first, *second, *pending;

	software = (struct mh_software_renderer *)renderer;
	enabled = enabled != 0;

	if (enabled == software->motion_blur)
		return 1;

	if (enabled) {
		first = calloc(1, software->surface.size);
		second = calloc(1, software->surface.size);
		pending = malloc(software->surface.size);

		if (first == NULL || second == NULL || pending == NULL) {
			free(first);
			free(second);
			free(pending);
			return failed(error_out, MH_ERROR_MEMORY,
				"out of memory preparing software motion blur");
		}

		software->history[0] = first;
		software->history[1] = second;
		software->history_pending = pending;
	} else {
		free(software->history[0]);
		free(software->history[1]);
		free(software->history_pending);
		software->history_pending = NULL;
		software->history[0] = software->history[1] = NULL;
	}

	software->history_next = 0;
	software->motion_blur = enabled;

	return 1;
}

static int
set_water(struct mh_renderer *renderer, const struct mh_water_frame *frame,
	struct mh_error *error_out)
{
	(void)renderer;

	return frame == NULL
		? 1
		: failed(error_out, MH_ERROR_UNSUPPORTED,
			"software water distortion is unavailable");
}

static int
ui_upload(struct mh_renderer *renderer, const struct mh_ui_atlas *atlas,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;

	software = (struct mh_software_renderer *)renderer;

	return mh_software_ui_upload(&software->ui, atlas, error_out) ==
		MH_ERROR_NONE;
}

static int
tron_frame_ui(struct mh_renderer *renderer,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui, struct mh_error *error_out)
{
	(void)renderer;

	(void)faces;
	(void)count;
	(void)ui;

	return failed(error_out, MH_ERROR_UNSUPPORTED,
		"software TRON frames are unavailable");
}

static int
tron_scene_ui(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	struct mh_error *error_out)
{
	(void)renderer;

	(void)projection;
	(void)view;
	(void)models;
	(void)reflections;
	(void)ui;

	return failed(error_out, MH_ERROR_UNSUPPORTED,
		"software TRON scenes are unavailable");
}

static int
capture(struct mh_renderer *renderer, const char *path,
	struct mh_error *error_out)
{
	struct mh_software_renderer *software;

	software = (struct mh_software_renderer *)renderer;

	if (!software->frame_ready)
		return failed(error_out, MH_ERROR_STATE,
			"software frame is not ready");

	return mh_software_capture(
		software->presentation, &software->surface, path, error_out);
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
	struct mh_software_renderer *software;
	unsigned int shift;

	software = (struct mh_software_renderer *)renderer;
	shift = PERSPECTIVE_SHIFT_MIN;

	while ((1U << shift) < block_pixels)
		shift++;

	software->mapping = mapping;
	software->perspective_shift = shift;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_vsync(struct mh_renderer *renderer, int enabled, struct mh_error *error_out)
{
	struct mh_software_renderer *software;

	software = (struct mh_software_renderer *)renderer;

	return mh_software_presentation_set_vsync(
		software->presentation, enabled, error_out);
}

const struct mh_render_backend *
mh_software_backend(void)
{
	static const struct mh_render_ops ops = {scene_order, update_mesh,
		mesh_visible, unsupported_vertices, unsupported_vertices,
		unsupported_effect, mesh_shadows, unsupported_effect,
		unsupported_effect, unsupported_effect, depth_queue, depth_read,
		set_fog, set_motion_blur, set_water, ui_upload,
		mh_software_renderer_scene_frame_matrix, tron_frame_ui,
		tron_scene_ui, destroy, capture, tga_picture,
		set_texture_mapping, mh_software_renderer_scene_frame_lens,
		set_depth_cue, set_panorama, model_lighting, set_environment,
		world_lighting, mesh_glenz, set_texture_filter, NULL, NULL,
		read_pixels, set_vsync};

	static const struct mh_render_backend backend = {
		"Software", MH_RENDER_SOFTWARE, open_renderer, &ops, NULL};

	return &backend;
}

enum mh_error_code
mh_software_backend_init(struct mh_render_backend *backend,
	const struct mh_software_output *output, struct mh_error *error_out)
{
	if (backend == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software backend descriptor");

	*backend = *mh_software_backend();
	backend->context = output;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
