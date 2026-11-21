#include <emscripten/html5.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/webgl_frame.h"
#include "backends/render/webgl_target.h"
#include "engine/render/matrix.h"

struct fixture {
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE	context;
	struct mh_webgl_scene		*scene;
	struct mh_webgl_materials	materials;
	struct mh_webgl_drawing		drawing;
	struct mh_webgl_target		target;
	struct mh_projection		projection;
	struct mh_webgl_frame		frame;
	struct mh_mesh			meshes[2];
	struct mh_draw			draws[2];
	struct mh_mesh_primitive	primitives[2];
	struct mh_vertex		vertices[2][6];
	float				colors[2][18], matrix[16], models[32];
	unsigned char			groups[2], white[4];
	struct mh_image			images[2];
};

unsigned int	mh_webgl_powervr_order_checks(void);

static unsigned int failures;

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

static void
pixel(unsigned int r, unsigned int g, unsigned int b, const char *label)
{
	unsigned char rgba[4];

	glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	if (rgba[0] != r || rgba[1] != g || rgba[2] != b)
		fprintf(stderr, "pixel %u %u %u; expected %u %u %u\n", rgba[0],
			rgba[1], rgba[2], r, g, b);

	check(rgba[0] == r && rgba[1] == g && rgba[2] == b, label);
}

static void
source_meshes(struct fixture *fixture)
{
	static const float xy[6][2] = {
		{-4, 4}, {4, 4}, {4, -4}, {-4, 4}, {4, -4}, {-4, -4}};

	unsigned int model, i;
	struct mh_mesh *mesh;

	memset(fixture->white, 255, sizeof(fixture->white));
	fixture->images[0].rgba = fixture->white;
	fixture->images[0].width = fixture->images[0].height = 1;
	fixture->images[1] = fixture->images[0];

	for (model = 0; model < 2; model++) {
		mesh = &fixture->meshes[model];
		mesh->vertices = fixture->vertices[model];
		mesh->vertex_count = 6;
		mesh->draws = &fixture->draws[model];
		mesh->draw_count = 1;
		mesh->primitives = &fixture->primitives[model];
		mesh->primitive_count = 1;
		mesh->textures = fixture->images;
		mesh->texture_count = 2;
		mesh->draw_groups = &fixture->groups[model];
		mesh->material_rgb = fixture->colors[model];
		mesh->material_rgb_count = 6;
		fixture->groups[model] = 1;
		fixture->draws[model].count = 6;
		fixture->primitives[model].count = 6;
		fixture->primitives[model].type = 4;

		for (i = 0; i < 6; i++) {
			fixture->vertices[model][i].position[0] = xy[i][0];
			fixture->vertices[model][i].position[1] = xy[i][1];
			fixture->vertices[model][i].position[2] = -4;
			fixture->vertices[model][i].color[model] = 1;
			fixture->colors[model][i * 3 + model] = 255;
		}

		memcpy(fixture->models + model * 16, mh_render_identity,
			16 * sizeof(float));
	}
}

static int
open_fixture(struct fixture *fixture)
{
	EmscriptenWebGLContextAttributes attributes;
	const struct mh_mesh *sources[2];
	struct mh_error error;

	memset(fixture, 0, sizeof(*fixture));
	emscripten_webgl_init_context_attributes(&attributes);
	attributes.majorVersion = 2;
	attributes.antialias = EM_FALSE;
	fixture->context =
		emscripten_webgl_create_context("#canvas", &attributes);

	if (fixture->context <= 0 ||
		emscripten_webgl_make_context_current(fixture->context) !=
			EMSCRIPTEN_RESULT_SUCCESS)
		return 0;

	source_meshes(fixture);
	sources[0] = &fixture->meshes[0];
	sources[1] = &fixture->meshes[1];

	if (!operation(
		mh_webgl_scene_open(sources, 2, 0, &fixture->scene, &error),
		&error) ||
		!operation(mh_webgl_drawing_create(&fixture->drawing, &error),
			&error) ||
		!operation(mh_webgl_target_create(
				&fixture->target, 4, 4, 1, &error),
			&error) ||
		!operation(mh_projection_setup(4, 4, 4.0f / 3, 1, 1, 100,
				&fixture->projection, &error),
			&error))
		return 0;

	fixture->matrix[0] = fixture->matrix[5] = 1;
	fixture->matrix[10] = fixture->matrix[11] = fixture->matrix[14] = -1;
	fixture->frame.matrix = fixture->matrix;
	fixture->frame.view = mh_render_identity;
	fixture->frame.models = fixture->models;
	fixture->frame.width = fixture->frame.height = 4;
	fixture->frame.direct3d = 1;
	glBindFramebuffer(GL_FRAMEBUFFER, fixture->target.framebuffer);
	glViewport(0, 0, 4, 4);
	glDisable(GL_DITHER);

	return 1;
}

static int
prepare(struct fixture *fixture)
{
	struct mh_error error;

	return operation(
		mh_webgl_materials_prepare(&fixture->materials, fixture->scene,
			fixture->frame.matrix, fixture->frame.view,
			fixture->frame.models, fixture->frame.reflections,
			&fixture->projection, fixture->frame.fog, &error),
		&error);
}

static int
render(struct fixture *fixture)
{
	struct mh_error error;

	if (!prepare(fixture))
		return 0;

	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	return operation(mh_webgl_scene_draw(&fixture->drawing, fixture->scene,
				&fixture->materials, &fixture->frame, &error),
		&error);
}

static void
submission_order(struct fixture *fixture)
{
	struct mh_renderer_scene_part parts[2];
	struct mh_error error;

	if (!render(fixture))
		return;

	pixel(255, 0, 0,
		"registered material lists prepend whole source quads");
	check(fixture->materials.count == 2 &&
			fixture->materials.vertex_count == 12,
		"scene preparation expands both original quads");
	memset(parts, 0, sizeof(parts));
	parts[0].mesh = 1;
	parts[0].active_lod = parts[1].active_lod = 1;

	if (!operation(mh_webgl_scene_order(fixture->scene, parts, 2, &error),
		&error))
		return;

	if (render(fixture))
		pixel(0, 255, 0,
			"explicit scene order changes equal-depth material precedence");

	parts[0].mesh = 7;
	check(mh_webgl_scene_order(fixture->scene, parts, 2, &error) ==
			MH_ERROR_INVALID,
		"invalid scene order is rejected");

	if (render(fixture))
		pixel(0, 255, 0,
			"rejected scene order preserves the previous picture order");

	operation(
		mh_webgl_scene_order(fixture->scene, NULL, 0, &error), &error);
}

static void
visibility_and_transform(struct fixture *fixture)
{
	unsigned char hidden;

	fixture->scene->meshes[0].hidden = 1;

	if (render(fixture))
		pixel(0, 255, 0,
			"hidden meshes are absent from material lists");

	fixture->scene->meshes[0].hidden = 0;
	hidden = 1;
	fixture->meshes[0].draw_hidden = &hidden;

	if (render(fixture))
		pixel(0, 255, 0, "hidden draws are absent from material lists");

	fixture->meshes[0].draw_hidden = NULL;
	fixture->models[12] = 20;

	if (render(fixture))
		pixel(0, 255, 0,
			"CPU face preparation applies each model transform");

	fixture->models[12] = 0;
	fixture->frame.direct3d = 0;

	if (render(fixture))
		pixel(255, 0, 0,
			"prepared geometry also reaches the Glide shader path");

	fixture->frame.direct3d = 1;
}

static void
live_ranges(struct fixture *fixture)
{
	struct mh_renderer_scene_part parts[2];
	struct mh_error error;
	struct mh_mesh *mesh;

	mesh = &fixture->meshes[0];
	mesh->lod_count = 2;
	mesh->lods[0].vertex_count = mesh->lods[1].vertex_count = 6;
	mesh->lods[0].draw_count = mesh->lods[0].primitive_count = 1;
	mesh->lod = 1;

	if (render(fixture))
		pixel(0, 255, 0,
			"active LOD changes are consumed without recreating GPU meshes");

	mesh->lod = 0;
	mesh->lod_count = 0;
	memset(parts, 0, sizeof(parts));
	parts[0].span.draw_count = parts[0].span.primitive_count = 1;
	parts[1].mesh = 1;
	parts[1].active_lod = 1;

	if (!operation(mh_webgl_scene_order(fixture->scene, parts, 2, &error),
		&error))
		return;

	mesh->draw_count = 0;
	check(mh_webgl_materials_prepare(&fixture->materials, fixture->scene,
		fixture->matrix, mh_render_identity, fixture->models, 0,
		&fixture->projection, NULL, &error) == MH_ERROR_INVALID,
		"live fixed spans reject removed draws before drawing");
	mesh->draw_count = 1;

	if (render(fixture))
		pixel(255, 0, 0,
			"restoring valid spans recovers scene preparation");

	operation(
		mh_webgl_scene_order(fixture->scene, NULL, 0, &error), &error);
	mesh->primitives = NULL;
	check(mh_webgl_scene_ranges(fixture->scene, &error) == MH_ERROR_INVALID,
		"live ranges reject missing primitive storage");
	mesh->primitives = &fixture->primitives[0];
}

static void
reflection_clipping(struct fixture *fixture)
{
	struct mh_glenz_coordinates coordinates[6];
	struct mh_renderer_glenz glenz;
	struct mh_error error;
	unsigned int i;

	fixture->scene->meshes[1].hidden = 1;
	fixture->meshes[0].environment_texture = 1;
	fixture->draws[0].environment = 1;
	fixture->frame.reflections = 1;
	memset(coordinates, 0, sizeof(coordinates));
	glenz.coordinates = coordinates;
	glenz.count = 6;
	glenz.reflection_texture = glenz.phong_texture = 1;

	for (i = 0; i < 6; i++)
		fixture->colors[0][i * 3] = 64;

	if (!operation(mh_webgl_mesh_update(&fixture->scene->meshes[0], NULL,
			MH_SCENE_EFFECT_OPAQUE, 1, &error),
		&error) ||
		!operation(mh_webgl_mesh_glenz(
				&fixture->scene->meshes[0], &glenz, &error),
			&error))
		return;

	if (render(fixture))
		pixel(128, 0, 0,
			"environment list draws reflection then additive Phong");

	fixture->vertices[0][0].position[2] =
		fixture->vertices[0][3].position[2] = -0.5f;

	if (!operation(mh_webgl_mesh_update(&fixture->scene->meshes[0], NULL,
			MH_SCENE_EFFECT_OPAQUE, 1, &error),
		&error))
		return;

	if (render(fixture)) {
		pixel(64, 0, 0,
			"near-clipped environment faces suppress the Phong pass");
		check(fixture->materials.count == 2 &&
				fixture->materials.items[0].near_clipped &&
				fixture->materials.items[1].near_clipped,
			"near clipping retains both original face halves");
	}

	for (i = 0; i < 6; i++)
		fixture->vertices[0][i].position[2] = -100;

	operation(mh_webgl_mesh_update(&fixture->scene->meshes[0], NULL,
			MH_SCENE_EFFECT_OPAQUE, 1, &error),
		&error);

	if (render(fixture))
		pixel(0, 0, 0,
			"faces at the original far boundary are culled before upload");

	for (i = 0; i < 6; i++) {
		fixture->vertices[0][i].position[2] = -4;
		fixture->colors[0][i * 3] = 255;
	}

	fixture->meshes[0].environment_texture = 0;
	fixture->draws[0].environment = 0;
	fixture->frame.reflections = 0;
	fixture->scene->meshes[1].hidden = 0;
	operation(mh_webgl_mesh_glenz(&fixture->scene->meshes[0], NULL, &error),
		&error);
	operation(mh_webgl_mesh_update(&fixture->scene->meshes[0], NULL,
			MH_SCENE_EFFECT_OPAQUE, 0, &error),
		&error);
}

static void
effect_order(struct fixture *fixture)
{
	struct mh_vertex halo[6], shadow[6], sky[6];
	struct mh_error error;
	unsigned int i, channel;

	memcpy(halo, fixture->vertices[0], sizeof(halo));
	memcpy(shadow, fixture->vertices[1], sizeof(shadow));

	for (i = 0; i < 6; i++) {
		halo[i].position[0] *= 0.25f;
		halo[i].position[1] *= 0.25f;
		halo[i].position[2] = 0.25f;
		halo[i].color[0] = 64.0f / 255;

		for (channel = 0; channel < 3; channel++)
			shadow[i].color[channel] = 0.5f;
	}

	if (!operation(mh_webgl_mesh_update(&fixture->scene->meshes[0], halo,
			MH_SCENE_EFFECT_HALOS, 1, &error),
		&error) ||
		!operation(mh_webgl_mesh_update(&fixture->scene->meshes[1],
				shadow, MH_SCENE_EFFECT_SHADOWS, 1, &error),
			&error))
		return;

	if (render(fixture))
		pixel(32, 0, 0,
			"Direct3D draws halos before multiplying ground shadows");

	fixture->frame.direct3d = 0;

	if (render(fixture))
		pixel(64, 0, 0,
			"Glide draws halos after multiplying ground shadows");

	fixture->frame.direct3d = 1;
	memcpy(sky, halo, sizeof(sky));

	for (i = 0; i < 6; i++) {
		sky[i].position[2] = 0.9f;
		sky[i].color[0] = 1;
		sky[i].opacity = 127.0f / 255;
	}

	operation(mh_webgl_mesh_update(&fixture->scene->meshes[0], sky,
			MH_SCENE_EFFECT_SKY, 0, &error),
		&error);
	fixture->scene->meshes[1].hidden = 1;
	fixture->frame.blur = 1;

	if (render(fixture))
		pixel(127, 0, 0,
			"Direct3D blur sky uses supplied vertex opacity");

	fixture->frame.direct3d = 0;

	if (render(fixture))
		pixel(128, 0, 0,
			"Glide blur sky uses its original constant blend weight");

	fixture->frame.blur = 0;
	fixture->frame.direct3d = 1;
	fixture->scene->meshes[1].hidden = 0;
	operation(mh_webgl_mesh_update(&fixture->scene->meshes[1], NULL,
			MH_SCENE_EFFECT_OPAQUE, 0, &error),
		&error);

	if (render(fixture))
		pixel(0, 255, 0,
			"ordinary geometry overwrites the earlier sky at nearer depth");

	operation(mh_webgl_mesh_update(&fixture->scene->meshes[0], NULL,
			MH_SCENE_EFFECT_OPAQUE, 0, &error),
		&error);
}

static void
powervr_opaque(struct fixture *fixture)
{
	const struct mh_mesh *sources[2];
	struct mh_webgl_scene *ordinary, *powervr;
	struct mh_error error;

	sources[0] = &fixture->meshes[0];
	sources[1] = &fixture->meshes[1];
	powervr = NULL;

	if (!operation(mh_webgl_scene_open(sources, 2, 1, &powervr, &error),
		&error))
		return;

	ordinary = fixture->scene;
	fixture->scene = powervr;

	if (render(fixture))
		pixel(255, 0, 0,
			"PowerVR opaque materials use prepared geometry and reciprocal depth");

	fixture->draws[0].alpha_test = fixture->draws[1].alpha_test = 1;
	fixture->models[14] = -4;

	if (render(fixture))
		pixel(0, 255, 0,
			"PowerVR prepared alpha materials reach the deferred region pass");

	fixture->draws[1].alpha_test = 0;

	if (render(fixture))
		pixel(0, 255, 0,
			"PowerVR deferred materials respect opaque scene depth");

	fixture->draws[0].alpha_test = 0;
	fixture->models[14] = 0;
	fixture->scene = ordinary;
	mh_webgl_scene_close(powervr);
}

int
mh_webgl_scene_check(void)
{
	struct fixture fixture;

	if (!open_fixture(&fixture))
		return 1;

	submission_order(&fixture);
	visibility_and_transform(&fixture);
	live_ranges(&fixture);
	reflection_clipping(&fixture);
	effect_order(&fixture);
	powervr_opaque(&fixture);
	failures += mh_webgl_powervr_order_checks();
	check(glGetError() == GL_NO_ERROR,
		"scene fixtures leave no WebGL errors");
	mh_webgl_materials_destroy(&fixture.materials);
	mh_webgl_scene_close(fixture.scene);
	mh_webgl_drawing_destroy(&fixture.drawing);
	mh_webgl_target_destroy(&fixture.target);
	emscripten_webgl_destroy_context(fixture.context);

	return failures != 0;
}

int
main(void)
{
	return 0;
}
