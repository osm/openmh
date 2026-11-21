#include <stdio.h>
#include <string.h>

#include "backends/render/webgl_frame.h"
#include "backends/render/webgl_target.h"
#include "engine/render/matrix.h"

struct fixture {
	struct mh_webgl_scene		*scene;
	struct mh_webgl_drawing		drawing;
	struct mh_webgl_materials	materials;
	struct mh_webgl_target		target;
	struct mh_webgl_frame		frame;
	struct mh_mesh			source;
	struct mh_draw			draw;
	struct mh_vertex		vertices[12];
	float				raw[36];
	struct mh_image			image;
	unsigned char			white[4];
	unsigned int			failures;
};

static int
operation(struct fixture *fixture, enum mh_error_code status,
	const struct mh_error *error)
{
	if (status == MH_ERROR_NONE)
		return 1;

	fprintf(stderr, "PowerVR order: %s\n", error->message);
	fixture->failures++;

	return 0;
}

static void
check(struct fixture *fixture, int success, const char *label)
{
	if (success)
		puts(label);
	else {
		fprintf(stderr, "%s failed\n", label);
		fixture->failures++;
	}
}

static void
quads(struct fixture *fixture, float red_w, float green_w, int rows)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct mh_vertex *vertex;
	unsigned int quad, i;

	memset(fixture->vertices, 0, sizeof(fixture->vertices));

	for (quad = 0; quad < 2; quad++)
		for (i = 0; i < 6; i++) {
			vertex = &fixture->vertices[quad * 6 + i];
			vertex->position[0] = xy[i][0];
			vertex->position[1] = rows
				? xy[i][1] * 0.5f + (quad == 0 ? 0.5f : -0.5f)
				: xy[i][1];

			vertex->position[2] = 0.5f;
			vertex->projection_w = quad == 0 ? red_w : green_w;
			fixture->raw[(quad * 6 + i) * 3 + quad] = 112;
		}
}

static int
open_fixture(struct fixture *fixture)
{
	const struct mh_mesh *sources[1];
	struct mh_error error;

	memset(fixture, 0, sizeof(*fixture));
	memset(fixture->white, 255, sizeof(fixture->white));
	fixture->image.width = fixture->image.height = 1;
	fixture->image.rgba = fixture->white;
	fixture->source.vertices = fixture->vertices;
	fixture->source.vertex_count = 12;
	fixture->source.material_rgb = fixture->raw;
	fixture->source.material_rgb_count = 12;
	fixture->source.draws = &fixture->draw;
	fixture->source.draw_count = 1;
	fixture->source.textures = &fixture->image;
	fixture->source.texture_count = 1;
	fixture->draw.count = 12;
	quads(fixture, 8, 16, 0);
	sources[0] = &fixture->source;
	fixture->frame.matrix = fixture->frame.view = mh_render_identity;
	fixture->frame.width = 65;
	fixture->frame.height = 70;
	fixture->frame.direct3d = 1;

	return operation(fixture,
		mh_webgl_scene_open(
			sources, 1, 1, &fixture->scene, &error),
		&error) &&
		operation(fixture,
			mh_webgl_drawing_create(&fixture->drawing, &error),
			&error) &&
		operation(fixture,
			mh_webgl_target_create(
				&fixture->target, 65, 70, 1, &error),
			&error);
}

static int
render(struct fixture *fixture)
{
	struct mh_error error;

	if (!operation(fixture,
		mh_webgl_mesh_update(&fixture->scene->meshes[0], NULL,
			MH_SCENE_EFFECT_HALOS, 1, &error),
		&error) ||
		!operation(fixture,
			mh_webgl_materials_prepare(&fixture->materials,
				fixture->scene, mh_render_identity,
				mh_render_identity, NULL, 0, NULL, NULL,
				&error),
			&error))
		return 0;

	glBindFramebuffer(GL_FRAMEBUFFER, fixture->target.framebuffer);
	glViewport(0, 0, 65, 70);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glScissor(7, 9, 13, 15);
	glEnable(GL_SCISSOR_TEST);

	return operation(fixture,
		mh_webgl_scene_draw(&fixture->drawing, fixture->scene,
			&fixture->materials, &fixture->frame, &error),
		&error);
}

static int
pixel(unsigned int x, unsigned int y, unsigned int red, unsigned int green)
{
	unsigned char rgba[4];
	int difference_r, difference_g;

	glReadPixels((GLint)x, (GLint)y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	difference_r = (int)rgba[0] - (int)red;
	difference_g = (int)rgba[1] - (int)green;

	/* One UNORM8 rounding step is allowed after coverage blending. */
	if (difference_r >= -1 && difference_r <= 1 && difference_g >= -1 &&
		difference_g <= 1 && rgba[2] == 0)
		return 1;

	fprintf(stderr, "PowerVR pixel %u,%u: %u %u %u, expected %u %u 0\n", x,
		y, rgba[0], rgba[1], rgba[2], red, green);

	return 0;
}

static int
region_pixels(unsigned int red, unsigned int green)
{
	static const unsigned int x[] = {1, 35, 64}, y[] = {1, 35, 69};
	unsigned int i, j;
	int success;

	success = 1;

	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			if (!pixel(x[i], y[j], red, green))
				success = 0;

	return success;
}

static void
overlap_and_ties(struct fixture *fixture)
{
	struct mh_vertex swap;
	GLint scissor[4];
	unsigned int i;

	if (render(fixture)) {
		check(fixture, region_pixels(128, 64),
			"PowerVR sorts each effect quad independently within one mesh draw");
		glGetIntegerv(GL_SCISSOR_BOX, scissor);
		check(fixture,
			glIsEnabled(GL_SCISSOR_TEST) && scissor[0] == 7 &&
				scissor[1] == 9 && scissor[2] == 13 &&
				scissor[3] == 15,
			"PowerVR regions restore the caller scissor state");
	}

	quads(fixture, 16, 8, 0);

	if (render(fixture))
		check(fixture, region_pixels(64, 128),
			"PowerVR reciprocal depth reverses translucent overlap order");

	quads(fixture, 16, 16, 0);

	for (i = 6; i < 12; i += 3) {
		swap = fixture->vertices[i + 1];
		fixture->vertices[i + 1] = fixture->vertices[i + 2];
		fixture->vertices[i + 2] = swap;
	}

	if (render(fixture))
		check(fixture, region_pixels(128, 64),
			"PowerVR equal-depth front/back sets preserve original tie order");
}

static void
region_orientation_and_water(struct fixture *fixture)
{
	struct mh_water_frame water;

	quads(fixture, 16, 16, 1);

	if (render(fixture))
		check(fixture,
			pixel(1, 69, 128, 0) && pixel(64, 69, 128, 0) &&
				pixel(1, 1, 0, 128) && pixel(64, 1, 0, 128),
			"PowerVR region scissors convert top-down Y and cover partial edge regions");

	quads(fixture, 8, 16, 0);
	memset(&water, 0, sizeof(water));
	water.scale = 0.5f;
	fixture->frame.water = &water;

	if (render(fixture))
		check(fixture,
			pixel(64, 1, 128, 64) && pixel(1, 69, 0, 0) &&
				pixel(64, 69, 0, 0),
			"PowerVR region coverage follows the same water transform as the shader");

	fixture->frame.water = NULL;
	quads(fixture, 0, 16, 0);

	if (render(fixture))
		check(fixture, region_pixels(0, 128),
			"invalid reciprocal depth discards only its translucent triangles");

	fixture->scene->meshes[0].hidden = 1;

	if (render(fixture))
		check(fixture, region_pixels(0, 0),
			"empty PowerVR frames discard the preceding translucent queue");
}

unsigned int
mh_webgl_powervr_order_checks(void)
{
	struct fixture fixture;

	if (open_fixture(&fixture)) {
		overlap_and_ties(&fixture);
		region_orientation_and_water(&fixture);
	}

	glDisable(GL_SCISSOR_TEST);
	mh_webgl_materials_destroy(&fixture.materials);
	mh_webgl_scene_close(fixture.scene);
	mh_webgl_drawing_destroy(&fixture.drawing);
	mh_webgl_target_destroy(&fixture.target);

	return fixture.failures;
}
