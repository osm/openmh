#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/render/scene_material_face.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#c);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_material_face_source	source;
	struct mh_projection		projection;
	struct mh_mesh			mesh;
	struct mh_draw			draw;
	struct mh_image			image;
	struct mh_vertex		vertices[6];
	struct mh_glenz_coordinates	glenz[6];
	float				positions[18], rgb[18], colors[18];
	unsigned char			group;
};

static const float camera[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static int
close_float(float value, float expected)
{
	return fabs((double)value - expected) < 0.00001;
}

static int
setup(struct fixture *fixture)
{
	static const float corners[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
	static const unsigned int indices[6] = {0, 1, 2, 0, 2, 3};
	unsigned int i, corner;

	memset(fixture, 0, sizeof(*fixture));
	fixture->mesh.vertices = fixture->vertices;
	fixture->mesh.vertex_count = 6;
	fixture->mesh.draws = &fixture->draw;
	fixture->mesh.draw_count = 1;
	fixture->mesh.texture_count = 1;
	fixture->mesh.draw_groups = &fixture->group;
	fixture->group = 1;
	fixture->draw.count = 6;
	fixture->source.mesh = &fixture->mesh;
	fixture->source.positions = fixture->positions;
	fixture->source.colors = fixture->colors;
	fixture->source.material_rgb = fixture->rgb;
	fixture->source.glenz_coordinates = fixture->glenz;
	fixture->source.image = &fixture->image;
	fixture->source.texture_width = fixture->source.texture_height = 64;
	fixture->image.width = 64;
	fixture->image.height = 32;

	for (i = 0; i < 6; i++) {
		corner = indices[i];
		fixture->positions[i * 3] = corners[corner][0];
		fixture->positions[i * 3 + 1] = corners[corner][1];
		fixture->positions[i * 3 + 2] = -4;
		fixture->vertices[i].uv[0] = (corners[corner][0] + 1) * 0.5f;
		fixture->vertices[i].uv[1] = (1 - corners[corner][1]) * 0.5f;
		fixture->rgb[i * 3] = 128;
		fixture->rgb[i * 3 + 1] = 64;
		fixture->rgb[i * 3 + 2] = 32;
		fixture->colors[i * 3] = 0.5f;
		fixture->glenz[i].reflection[0] = 0.25f;
		fixture->glenz[i].reflection[1] = 0.75f;
		fixture->glenz[i].phong[0] = 0.125f;
		fixture->glenz[i].phong[1] = 0.875f;
	}

	CHECK(mh_projection_setup(64, 48, 4.0f / 3, 1, 1, 100,
		&fixture->projection, NULL) == MH_ERROR_NONE);

	return 0;
}

static int
prepare(struct fixture *fixture, struct mh_material_faces *faces)
{
	struct mh_error error;

	if (mh_material_face_prepare(&fixture->source, 0, 0, 6, 4, camera,
		&fixture->projection, 1, faces, &error) == MH_ERROR_NONE)
		return 1;

	fprintf(stderr, "%s\n", error.message);

	return 0;
}

static int
ordinary_and_padded_texture(void)
{
	struct fixture fixture;
	struct mh_material_faces faces;
	struct mh_vertex vertices[6];
	struct mh_glenz_coordinates coordinates[6];

	CHECK(setup(&fixture) == 0);
	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	CHECK(faces.lists[0] == MH_LIST_TEXTURE && !faces.near_clipped);
	CHECK(mh_material_face_vertices(&fixture.source, &faces, 0,
		&fixture.projection, vertices, coordinates) == 6);
	CHECK(close_float(vertices[0].position[0], -0.25f));
	CHECK(close_float(vertices[0].position[1], 1.0f / 3));

	/*
	 * Device depth is 1 - near/z; far-normalized depth selects fog lists.
	 */
	CHECK(close_float(vertices[0].position[2], 0.75f));
	CHECK(vertices[0].projection_w == 4);
	CHECK(close_float(vertices[0].color[0], 128.0f / 255));
	CHECK(vertices[0].normal[0] == 1 && vertices[0].normal[1] == 0);
	CHECK(memcmp(&vertices[0], &vertices[3], sizeof(vertices[0])) == 0);
	CHECK(memcmp(&vertices[2], &vertices[4], sizeof(vertices[0])) == 0);
	CHECK(vertices[5].position[0] < 0 && vertices[5].position[1] < 0);
	CHECK(vertices[2].uv[0] == 1 && vertices[2].uv[1] == 1);

	if (!MH_RENDER_BUILD_POWERVR)
		return 0;

	fixture.source.powervr = 1;
	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	mh_material_face_vertices(&fixture.source, &faces, 0,
		&fixture.projection, vertices, coordinates);
	CHECK(vertices[2].uv[0] == 1 && vertices[2].uv[1] == 0.5f);

	return 0;
}

static int
material_classification(void)
{
	struct fixture fixture;
	struct mh_material_faces faces;
	unsigned int i;

	CHECK(setup(&fixture) == 0);
	fixture.draw.alpha_test = 1;
	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	CHECK(faces.lists[0] == MH_LIST_ALPHA + 191);
	fixture.source.fog_enabled = 1;

	for (i = 0; i < 6; i++)
		fixture.positions[i * 3 + 2] = -80;

	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	CHECK(faces.lists[0] == MH_LIST_DISTANT_ALPHA);
	fixture.draw.alpha_test = 0;
	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	CHECK(faces.lists[0] == MH_LIST_DISTANT_SOLID);

	if (!MH_RENDER_BUILD_POWERVR)
		return 0;

	fixture.source.powervr = 1;
	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	CHECK(faces.lists[0] == MH_LIST_DISTANT_ALPHA);
	fixture.source.effect = MH_SCENE_EFFECT_UNAVAILABLE;
	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	CHECK(faces.lists[0] == MH_LIST_UNAVAILABLE);
	fixture.positions[2] = -100;
	CHECK(prepare(&fixture, &faces) && faces.count == 0);

	return 0;
}

static int
glenz_near_split(void)
{
	struct fixture fixture;
	struct mh_material_faces faces;
	struct mh_vertex vertices[6];
	struct mh_glenz_coordinates coordinates[6];
	unsigned int face, count, i;

	CHECK(setup(&fixture) == 0);
	fixture.source.glenz = 1;
	fixture.draw.environment = 1;
	fixture.mesh.environment_texture = 1;
	CHECK(prepare(&fixture, &faces) && faces.count == 1 && faces.glenz);
	CHECK(faces.lists[0] == MH_LIST_ENVIRONMENT);
	mh_material_face_vertices(&fixture.source, &faces, 0,
		&fixture.projection, vertices, coordinates);
	CHECK(coordinates[0].reflection[0] == 0.25f &&
		coordinates[0].reflection[1] == 0.75f);
	CHECK(coordinates[0].phong[0] == 0.125f &&
		coordinates[0].phong[1] == 0.875f);
	fixture.positions[2] = fixture.positions[11] = -0.5f;
	CHECK(prepare(&fixture, &faces) && faces.count == 2 &&
		faces.near_clipped);

	for (face = 0; face < faces.count; face++) {
		CHECK(faces.lists[face] == MH_LIST_ENVIRONMENT);
		count = mh_material_face_vertices(&fixture.source, &faces, face,
			&fixture.projection, vertices, coordinates);

		for (i = 0; i < count; i++) {
			CHECK(vertices[i].projection_w >= 1);
			CHECK(coordinates[i].phong[0] == 0 &&
				coordinates[i].phong[1] == 0);
		}
	}

	return 0;
}

static int
ground_effects_and_invalid_sources(void)
{
	struct fixture fixture;
	struct mh_material_faces faces, saved;

	CHECK(setup(&fixture) == 0);
	fixture.source.effect = MH_SCENE_EFFECT_SKIDS;
	fixture.source.material_rgb = NULL;
	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	CHECK(faces.lists[0] == MH_LIST_SKID);

	if (!MH_RENDER_BUILD_POWERVR)
		return 0;

	fixture.source.powervr = 1;
	fixture.source.effect = MH_SCENE_EFFECT_SHADOWS;
	CHECK(prepare(&fixture, &faces) && faces.count == 1);
	CHECK(faces.lists[0] == MH_LIST_SHADOW);
	saved = faces;
	CHECK(mh_material_face_prepare(&fixture.source, 0, 0, 3, 4, camera,
		&fixture.projection, 1, &faces,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&faces, &saved, sizeof(faces)) == 0);
	fixture.source.effect = MH_SCENE_EFFECT_OPAQUE;
	fixture.source.material_rgb = fixture.rgb;
	fixture.source.texture_height = 0;
	CHECK(mh_material_face_prepare(&fixture.source, 0, 0, 6, 4, camera,
		&fixture.projection, 1, &faces,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&faces, &saved, sizeof(faces)) == 0);
	fixture.source.glenz = 1;
	CHECK(mh_material_face_prepare(&fixture.source, 0, 0, 6, 4, camera,
		&fixture.projection, 1, &faces,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&faces, &saved, sizeof(faces)) == 0);

	return 0;
}

int
main(void)
{
	if (ordinary_and_padded_texture() || material_classification() ||
		glenz_near_split() || ground_effects_and_invalid_sources())
		return 1;

	puts("material projection, classification, encoding and clipping passed");

	return 0;
}
