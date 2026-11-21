#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_fog.h"
#include "backends/render/software_panorama.h"
#include "backends/render/software_provider.h"
#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "interfaces/render/backend.h"

/* Captured TGA payload starts after the fixed header. */
#define PIXEL_OFFSET 18

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_replacement(struct mh_panorama *settings)
{
	struct mh_software_panorama sky;
	float retained[3];

	memset(&sky, 0, sizeof(sky));
	CHECK(mh_software_panorama_set(&sky, settings, 320, 200, 6, NULL) ==
		MH_ERROR_NONE);
	CHECK(sky.image.width == 320 && sky.image.height == 80);
	sky.state.angle = 1;
	sky.state.phase = 17;
	sky.state.top = sky.gradient.top = 23;
	memcpy(retained, sky.gradient.rows[60], sizeof(retained));
	CHECK(mh_software_panorama_set(&sky, settings, 320, 100, 6, NULL) ==
		MH_ERROR_NONE);
	CHECK(sky.gradient.height == 40 && sky.state.angle == 0 &&
		sky.state.phase == 0 && sky.state.top == 23 &&
		sky.gradient.top == 23);
	CHECK(memcmp(retained, sky.gradient.rows[60], sizeof(retained)) == 0);
	CHECK(mh_software_panorama_set(&sky, NULL, 320, 100, 6, NULL) ==
		MH_ERROR_NONE);
	CHECK(!sky.enabled && sky.image.pixels == NULL &&
		sky.gradient.height == 40);
	CHECK(memcmp(retained, sky.gradient.rows[60], sizeof(retained)) == 0);

	return 0;
}

static int
check_retained_frame(struct mh_renderer *renderer, const struct mh_file *saved)
{
	struct mh_file current;
	int equal;

	CHECK(mh_renderer_tga_capture(renderer, &current));
	equal = current.size == saved->size &&
		memcmp(current.data, saved->data, saved->size) == 0;
	free(current.data);
	CHECK(equal);

	return 0;
}

static int
check_frames(int blur)
{
	struct mh_renderer *actual, *reference;
	struct mh_mesh mesh;
	struct mh_vertex vertices[3];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	const struct mh_mesh *meshes[1];
	struct mh_panorama settings, off;
	struct mh_image source;
	struct mh_software_sky_image prepared;
	struct mh_software_sky_state state;
	struct mh_software_sky_camera camera;
	struct mh_software_surface surface;
	struct mh_picture picture;
	struct mh_projection projection;
	struct mh_depth_cue cue;
	struct mh_depth_cue_gradient gradient;
	struct mh_file actual_image, reference_image, background, mask,
		ui_image;
	unsigned char rgba[48], bgr[36], *history[2];
	float view[16], bad[16], bad_model[16], co, si, cp, sp;
	unsigned int frame, i, current, previous, pixel, covered;
	size_t offset;
	size_t n;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&draw, 0, sizeof(draw));
	memset(&primitive, 0, sizeof(primitive));
	memset(&settings, 0, sizeof(settings));
	memset(&state, 0, sizeof(state));
	memset(&gradient, 0, sizeof(gradient));
	memset(&surface, 0, sizeof(surface));
	memset(&picture, 0, sizeof(picture));
	memset(&cue, 0, sizeof(cue));
	memset(view, 0, sizeof(view));
	source.width = 4;
	source.height = 3;
	source.rgba = rgba;

	for (i = 0; i < 12; i++) {
		rgba[i * 4] = bgr[i * 3 + 2] = (unsigned char)(i * 13 + 32);
		rgba[i * 4 + 1] = bgr[i * 3 + 1] =
			blur ? 0 : (unsigned char)(i * 7 + 64);
		rgba[i * 4 + 2] = bgr[i * 3] = (unsigned char)(i * 17 + 16);
		rgba[i * 4 + 3] = 255;
	}

	settings.image = &source;
	settings.horizon = .5f;
	settings.height_cue = 1;
	settings.above[0] = 64;
	settings.below[2] = 128;
	settings.clear[1] = 255;
	CHECK(check_replacement(&settings) == 0);
	CHECK(mh_software_sky_resize(bgr, sizeof(bgr), 4, 3, 320, 80, 6,
		&prepared, NULL) == MH_ERROR_NONE);
	CHECK(mh_software_fog_rows(prepared.pixels, prepared.size, 320, 80, 6,
		&gradient, NULL) == MH_ERROR_NONE);
	gradient.above[0] = 64;
	gradient.below[2] = 128;
	vertices[0].position[0] = 15;
	vertices[1].position[0] = -15;
	vertices[0].position[1] = vertices[1].position[1] = -10;
	vertices[2].position[1] = 12;

	for (i = 0; i < 3; i++) {
		vertices[i].position[2] = 70;
		vertices[i].color[0] = .8f;
		vertices[i].color[1] = blur ? 0 : .4f;
		vertices[i].color[2] = .2f;
	}

	draw.prelit = 1;
	draw.count = primitive.count = mesh.vertex_count = 3;
	mesh.vertices = vertices;
	mesh.draws = &draw;
	mesh.draw_count = mesh.primitive_count = 1;
	mesh.primitives = &primitive;
	meshes[0] = &mesh;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &actual, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &reference, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_set_panorama(actual, &settings, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_set_motion_blur(actual, blur));

	/* Source data is borrowed only during upload. */
	memset(rgba, 0, sizeof(rgba));
	cue.start = .25f;
	CHECK(mh_renderer_set_depth_cue(actual, &cue, NULL) == MH_ERROR_NONE);
	CHECK(mh_projection_setup(320, 200, 1.6f, 1, 1, 100, &projection,
		NULL) == MH_ERROR_NONE);
	surface.width = 320;
	surface.height = 200;
	surface.pitch = 640;
	surface.size = 128000;
	surface.pixels = calloc(1, surface.size);
	CHECK(surface.pixels != NULL);
	history[0] = calloc(1, surface.size);
	history[1] = calloc(1, surface.size);
	CHECK(history[0] != NULL && history[1] != NULL);
	picture.format = MH_PICTURE_RGB565;
	picture.width = 320;
	picture.height = 200;
	picture.pitch = 640;
	picture.pixels = surface.pixels;
	picture.pixel_size = surface.size;
	camera.focal = 1;
	camera.horizon_offset = 0;
	view[0] = view[15] = 1;

	for (frame = 0; frame < 24; frame++) {
		/*
		 * UI-only results must neither draw nor discard the resident
		 * sky.
		 */
		CHECK(mh_renderer_ui_frame(actual, NULL));
		CHECK(mh_renderer_tga_capture(actual, &ui_image));

		for (n = PIXEL_OFFSET; n < ui_image.size; n++)
			CHECK(ui_image.data[n] == 0);

		free(ui_image.data);
		co = (float)cos(frame * .005);
		si = (float)sin(frame * .005);
		cp = (float)cos(frame * .02);
		sp = (float)sin(frame * .02);
		view[0] = co;
		view[1] = si * sp;
		view[2] = -si * cp;
		view[5] = cp;
		view[6] = sp;
		view[8] = -si;
		view[9] = co * sp;
		view[10] = -co * cp;

		for (i = 0; i < 3; i++)
			camera.forward[i] = -view[i * 4 + 2];

		if (blur)
			memcpy(surface.pixels, history[frame % 2],
				surface.size);

		CHECK(mh_software_sky_draw(&surface, &prepared, &camera, &state,
			blur ? MH_SOFTWARE_SKY_HISTORY
				: MH_SOFTWARE_SKY_DIRECT,
			0x4000, 16, 0x7e0, NULL) == MH_ERROR_NONE);
		CHECK(mh_image_tga_encode(&picture, &background, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_renderer_set_depth_cue(reference, NULL, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(reference, &projection, view,
			NULL, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(reference, &mask));
		covered = 0;

		for (n = PIXEL_OFFSET; n < mask.size; n += 2)
			if (mask.data[n] || mask.data[n + 1])
				covered++;

		CHECK(covered != 0);

		if (!blur)
			gradient.top = state.top;

		cue.gradient = &gradient;
		CHECK(mh_renderer_set_depth_cue(reference, &cue, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(reference, &projection, view,
			NULL, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(reference, &reference_image));
		gradient.top = state.top;

		if (blur) {
			/*
			 * Green is zero: the capture's RGB555 conversion loses
			 * no data.
			 */
			for (n = PIXEL_OFFSET; n < reference_image.size;
				n += 2) {
				offset =
					(199 - (n - PIXEL_OFFSET) / 640) * 640 +
					(n - PIXEL_OFFSET) % 640;

				if (mask.data[n] || mask.data[n + 1]) {
					pixel = mh_read_u16le(
						reference_image.data + n);
					pixel = ((pixel & 0x7c00U) << 1) |
						(pixel & 31U);
					surface.pixels[offset] =
						(unsigned char)pixel;
					surface.pixels[offset + 1] =
						(unsigned char)(pixel >> 8);
				}
			}

			for (n = 0; n < surface.size; n += 2) {
				current = mh_read_u16le(surface.pixels + n);
				previous = mh_read_u16le(
					history[(frame + 1) % 2] + n);
				pixel = (((current >> 11) + (previous >> 11)) /
						2)
					<< 11;

				pixel |= ((current & 31U) + (previous & 31U)) /
					2;
				surface.pixels[n] = (unsigned char)pixel;
				surface.pixels[n + 1] =
					(unsigned char)(pixel >> 8);
			}

			memcpy(history[frame % 2], surface.pixels,
				surface.size);
			free(background.data);
			CHECK(mh_image_tga_encode(&picture, &background,
				NULL) == MH_ERROR_NONE);
		}

		CHECK(mh_renderer_scene_frame_lens(actual, &projection, view,
			NULL, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(actual, &actual_image));
		CHECK(actual_image.size == reference_image.size &&
			actual_image.size == background.size);

		for (n = PIXEL_OFFSET; n < actual_image.size; n += 2) {
			if (!blur && (mask.data[n] || mask.data[n + 1])) {
				CHECK(actual_image.data[n] ==
						reference_image.data[n] &&
					actual_image.data[n + 1] ==
						reference_image.data[n + 1]);
			} else {
				CHECK(actual_image.data[n] ==
						background.data[n] &&
					actual_image.data[n + 1] ==
						background.data[n + 1]);
			}
		}

		/*
		 * Failure after sky placement must not advance its state or
		 * picture.
		 */
		memcpy(bad, view, sizeof(bad));
		bad[2] = .3f;
		memcpy(bad_model, view, sizeof(bad_model));
		bad_model[0] = 1e10f;
		CHECK(mh_renderer_scene_frame_lens(actual, &projection, bad,
			bad_model, 0, NULL, NULL, NULL,
			NULL) != MH_ERROR_NONE);
		CHECK(check_retained_frame(actual, &actual_image) == 0);
		CHECK(mh_renderer_set_depth_cue(actual, &cue, NULL) ==
			MH_ERROR_INVALID);
		CHECK(check_retained_frame(actual, &actual_image) == 0);
		free(actual_image.data);
		free(reference_image.data);
		free(mask.data);
		free(background.data);
	}

	/*
	 * Re-enabling starts with black history; repeating enable keeps
	 * history.
	 */
	CHECK(mh_renderer_set_motion_blur(actual, 0));
	CHECK(mh_renderer_set_motion_blur(actual, blur));
	off = settings;
	off.image = NULL;
	off.height_cue = 0;
	CHECK(mh_renderer_set_panorama(actual, &off, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_mesh_visible(actual, 0, 0));
	CHECK(mh_renderer_scene_frame_lens(actual, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(actual, &actual_image));

	for (n = PIXEL_OFFSET; n < actual_image.size; n += 2)
		CHECK(actual_image.data[n] == (blur ? 0 : 0xe0) &&
			actual_image.data[n + 1] == (blur ? 0 : 3));

	CHECK(!mh_renderer_scene_frame(actual, view, view, NULL, 0));
	CHECK(check_retained_frame(actual, &actual_image) == 0);
	off.horizon = -1;
	CHECK(mh_renderer_set_panorama(actual, &off, NULL) == MH_ERROR_INVALID);
	CHECK(check_retained_frame(actual, &actual_image) == 0);
	CHECK(mh_renderer_set_panorama(actual, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_set_depth_cue(actual, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame(actual, view, view, NULL, 0));
	free(actual_image.data);
	free(surface.pixels);
	free(history[0]);
	free(history[1]);
	free(prepared.pixels);
	mh_renderer_destroy(actual);
	mh_renderer_destroy(reference);

	return 0;
}

int
main(void)
{
	CHECK(check_frames(0) == 0);
	CHECK(check_frames(1) == 0);

	return 0;
}
