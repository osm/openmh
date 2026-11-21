#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/provider.h"
#include "engine/assets/myo_decode.h"
#include "engine/assets/myw.h"
#include "engine/assets/remap.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/rotation.h"
#include "engine/geometry/model.h"
#include "engine/geometry/world.h"
#include "engine/render/mesh.h"

#define MUTATION_MULTIPLIER 1664525UL
#define MUTATION_INCREMENT 1013904223UL

static int failures;

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "line %d: %s\n", __LINE__,             \
				#expression);                                  \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_physical_rotation(struct mh_mesh *mesh)
{
	struct mh_scene_transform pose, saved_pose;
	struct mh_vertex saved[15];
	unsigned int i;

	/* A physical unit-X vertex must retain small contact rounding drift. */
	memset(&pose, 0, sizeof(pose));
	pose.scene_index = 1;
	pose.position[0] = 10;
	pose.rotation[0] = 1.00001;
	pose.rotation[4] = pose.rotation[8] = 1;
	saved_pose = pose;
	CHECK(!mh_rotation_valid(pose.rotation));
	CHECK(mh_rotation_valid_accumulated(pose.rotation));
	CHECK(mh_mesh_animate_objects(mesh, 1, &pose, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(mesh->vertices[13].position[0] == (float)11.00001);
	CHECK(memcmp(&pose, &saved_pose, sizeof(pose)) == 0);
	memcpy(saved, mesh->vertices, sizeof(saved));

	/* Singular, reflected, skewed and nonfinite bases fail atomically. */
	for (i = 0; i < 4; i++) {
		pose = saved_pose;

		if (i == 0)
			pose.rotation[0] = 0;
		else if (i == 1)
			pose.rotation[0] = -1;
		else if (i == 2)
			pose.rotation[1] = .1;
		else
			pose.rotation[0] = HUGE_VAL;
		CHECK(mh_mesh_animate_objects(mesh, 2, &pose, 1, NULL) ==
			MH_ERROR_INVALID);
		CHECK(memcmp(mesh->vertices, saved, sizeof(saved)) == 0);
	}
}

static void
write_word(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

/*
 * Two geometry slices separated by an empty descriptor. Their global
 * positions are deliberately far apart; no descriptor translation applies.
 */
static void
prepare_world(unsigned char data[1240])
{
	static const unsigned long counts[12] = {
		3, 7, 3, 2, 2, 2, 0, 0, 0, 0, 0, 0};

	static const unsigned long lengths[12] = {
		684, 84, 72, 56, 32, 24, 0, 0, 0, 0, 0, 0};

	static const unsigned long offsets[12] = {240, 924, 1008, 1080, 1136,
		1168, 1192, 1192, 1192, 1192, 1192, 1192};

	static const unsigned char colors[12] = {
		255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

	unsigned char *record;
	unsigned int i;
	unsigned int j;
	unsigned int corners;

	memset(data, 0, 1240);
	memcpy(data, "!WYM", 4);
	write_word(data + 4, 1240);
	write_word(data + 8, 2);
	write_word(data + 12, 1);
	write_word(data + 0x14,
		0x40330000UL); /* binary64 cell size 19, X and Z. */
	write_word(data + 0x1c, 0x40330000UL);
	write_word(data + 0x58, 1192);

	for (i = 0; i < 12; i++) {
		write_word(data + 0x5c + i * 12, counts[i]);
		write_word(data + 0x60 + i * 12, lengths[i]);
		write_word(data + 0x64 + i * 12, offsets[i]);
	}

	for (i = 0; i < 3; i++) {
		record = data + 240 + i * 228;
		memset(record + 4, 0xcd, 108); /* Untouched runtime scratch. */
		write_word(record + 0x70,
			i == 2 ? 0x41280000UL
			: 0x3f000000UL); /* x=10.5 or .5 */
		write_word(record + 0x74, 0x3f000000UL);
		write_word(record + 0x7c, 0x3f800000UL);
		record[0x80] = (unsigned char)(i == 0 ? 3 : i == 1 ? 0 : 4);
		record[0x82] = record[0x84] = record[0x86] =
			(unsigned char)(i != 1);
		write_word(record + 0x8c, i == 0 ? 0 : 36);
		write_word(record + 0x90, i == 0 ? 0 : 16);
		write_word(record + 0x94, i == 0 ? 0 : 12);
		write_word(record + 0x9c, i == 0 ? 0 : 24);
	}

	write_word(data + 936, 0x3f800000UL); /* x=1 */
	write_word(data + 952, 0x3f800000UL); /* y=1 */

	for (i = 0; i < 4; i++) {
		write_word(data + 960 + i * 12,
			(i == 1 || i == 2) ? 0x41300000UL : 0x41200000UL);

		if (i >= 2)
			write_word(data + 964 + i * 12, 0x3f800000UL);
	}

	write_word(data + 1176, 0x3f800000UL);
	write_word(data + 1188, 0x3f800000UL);

	for (i = 0; i < 2; i++) {
		record = data + 1080 + i * 24;
		record[0] = (unsigned char)(4 + i);
		corners = 3 + i;

		for (j = 0; j < corners; j++) {
			memcpy(record + 4 + j * 4, colors + j * 3, 3);
			record[7 + j * 4] = 0xcd; /* Not alpha. */
			record[4 + corners * 4 + j * 2] = (unsigned char)j;
		}
	}

	write_word(data + 1192, 0);
	write_word(data + 1200, MH_U32_MAX);
	write_word(data + 1216, 456);
	write_word(data + 1224, MH_U32_MAX);
}

static void
check_decoded_world(void)
{
	unsigned char data[1249], original[1249];
	struct mh_world world, saved;
	size_t size;
	enum mh_error_code status;

	prepare_world(data);
	write_word(data + 4, sizeof(data));
	write_word(data + 0xd4, 2); /* Shared texture names. */
	write_word(data + 0xd8, 9);
	write_word(data + 0xdc, 1240);
	memcpy(data + 1240, "road\0sky\0", 9);
	memcpy(original, data, sizeof(data));
	status = mh_world_myw_decode(data, sizeof(data), &world, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return;

	memset(data, 0, sizeof(data));
	CHECK(world.texture_count == 2);
	CHECK(strcmp(world.textures[0], "road") == 0);
	CHECK(strcmp(world.textures[1], "sky") == 0);
	CHECK(world.models[0].geometry.textures == world.textures);
	CHECK(world.models[2].geometry.textures == world.textures);
	CHECK(world.width == 2 && world.height == 1);
	CHECK(world.cell_size[0] == 19 && world.cell_size[1] == 19);
	CHECK(world.model_count == 3 && world.cell_count == 2);
	CHECK(world.cells[0].model == 0 && world.cells[1].model == 2);
	CHECK(world.cells[0].light_head == MH_WORLD_NO_LIGHT);
	CHECK(world.models[0].geometry.face_count == 1);
	CHECK(world.models[1].geometry.face_count == 0);
	CHECK(world.models[2].geometry.face_count == 1);
	CHECK(world.models[0].face_ids[0] == 1080);
	CHECK(world.models[2].face_ids[0] == 1104);
	CHECK(world.models[0].geometry.faces[0].count == 3);
	CHECK(world.models[2].geometry.faces[0].count == 4);
	CHECK(world.models[2].geometry.faces[0].corners[1].position[0] == 11);
	CHECK(world.models[2].center[0] == 10.5f);
	CHECK(world.models[2].radius == 1);
	CHECK(world.minimum[0] == 0 && world.maximum[0] == 11);
	mh_world_free(&world);
	CHECK(world.models == NULL && world.model_count == 0);
	mh_world_free(&world);
	mh_world_free(NULL);

	memcpy(data, original, sizeof(data));
	memset(&saved, 0x5a, sizeof(saved));

	for (size = 0; size < sizeof(data); size++) {
		memcpy(&world, &saved, sizeof(world));
		CHECK(mh_world_myw_decode(data, size, &world, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&world, &saved, sizeof(world)) == 0);
	}

	/* Malformed final geometry must not publish a partial owner. */
	data[1104] = 255;
	CHECK(mh_world_myw_decode(data, sizeof(data), &world, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&world, &saved, sizeof(world)) == 0);
}

static void
check_software_visibility(void)
{
	const double brightness[3] = {1, 1, 1};
	unsigned char data[1240];
	struct mh_world world;
	struct mh_mesh mesh;
	struct mh_world_view view;
	enum mh_error_code status;

	prepare_world(data);
	write_word(data + 1144, 0x3f800000UL); /* First plane: Z. */
	write_word(data + 1160, 0xbf800000UL); /* Second plane: 1-Z. */
	write_word(data + 1164, 0x3f800000UL);
	CHECK(mh_world_myw_decode(data, sizeof(data), &world, NULL) ==
		MH_ERROR_NONE);
	status = mh_mesh_build_world_native(mh_image_fallback_decoder(), &world,
		NULL, brightness, NULL, 0, &mesh, NULL);
	mh_world_free(&world);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return;

	CHECK(!mh_mesh_world_software_ready(&mesh));
	memset(data, 0, sizeof(data));
	memset(&view, 0, sizeof(view));
	view.end[0] = 2;
	view.end[1] = 1;
	view.profile = MH_RENDER_SOFTWARE;
	CHECK(mh_mesh_world_cull(&mesh, &view, NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_world_software_ready(&mesh));
	CHECK(mesh.primitive_hidden[0] == 1 && mesh.primitive_hidden[1] == 0);
	view.camera[2] = 2;
	CHECK(mh_mesh_world_cull(&mesh, &view, NULL) == MH_ERROR_NONE);
	CHECK(mesh.primitive_hidden[0] == 0 && mesh.primitive_hidden[1] == 1);
	view.profile = (enum mh_render_profile) - 1;
	CHECK(mh_mesh_world_cull(&mesh, &view, NULL) != MH_ERROR_NONE);
	CHECK(mesh.primitive_hidden[0] == 0 && mesh.primitive_hidden[1] == 1);
	CHECK(mh_mesh_world_software_ready(&mesh));
	view.profile = MH_RENDER_SOFTWARE;
	view.camera[2] = 1e30f;
	CHECK(mh_mesh_world_cull(&mesh, &view, NULL) != MH_ERROR_NONE);
	CHECK(mesh.primitive_hidden[0] == 0 && mesh.primitive_hidden[1] == 1);
	view.camera[2] = -2;
	CHECK(mh_mesh_world_cull(&mesh, &view, NULL) == MH_ERROR_NONE);
	CHECK(mesh.primitive_hidden[0] == 1 && mesh.primitive_hidden[1] == 0);
	view.profile = MH_RENDER_GLIDE;
	CHECK(mh_mesh_world_cull(&mesh, &view, NULL) == MH_ERROR_NONE);
	CHECK(!mh_mesh_world_software_ready(&mesh));
	CHECK(mesh.primitive_hidden[0] == 0 && mesh.primitive_hidden[1] == 0);
	CHECK(mh_mesh_world_cull(&mesh, NULL, NULL) == MH_ERROR_NONE);
	mh_mesh_free(&mesh);

	/* Repeated/reversed cells need a separate producer list, not a mask. */
	prepare_world(data);
	write_word(data + 1192, 456);
	write_word(data + 1216, 0);
	CHECK(mh_world_myw_decode(data, sizeof(data), &world, NULL) ==
		MH_ERROR_NONE);
	status = mh_mesh_build_world_native(mh_image_fallback_decoder(), &world,
		NULL, brightness, NULL, 0, &mesh, NULL);
	mh_world_free(&world);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		view.profile = MH_RENDER_SOFTWARE;
		CHECK(mh_mesh_world_cull(&mesh, &view, NULL) == MH_ERROR_NONE);
		CHECK(!mh_mesh_world_software_ready(&mesh));
		mh_mesh_free(&mesh);
	}
}

static void
check_direct3d_brightness(void)
{
	struct mh_mesh mesh;
	struct mh_vertex vertices[3], saved[3];
	unsigned char colors[6] = {101, 80, 60, 41, 123, 255};
	double brightness[3] = {.5, 2, .25};

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	vertices[0].position[1] = 12;
	vertices[2].color[0] = .75f;
	memcpy(saved, vertices, sizeof(vertices));
	mesh.vertices = vertices;
	mesh.vertex_count = 3;
	mesh.world_colors = colors;
	mesh.world_vertex_count = 2;

	CHECK(mh_mesh_material_world_brightness(&mesh, brightness, NULL) ==
		MH_ERROR_NONE);
	CHECK(vertices[0].color[0] == 50 / 255.0f);
	CHECK(vertices[0].color[1] == 160 / 255.0f);
	CHECK(vertices[0].color[2] == 15 / 255.0f);
	CHECK(vertices[1].color[0] == 20 / 255.0f);
	CHECK(vertices[1].color[1] == 246 / 255.0f);
	CHECK(vertices[1].color[2] == 63 / 255.0f);
	CHECK(vertices[0].position[1] == saved[0].position[1]);
	CHECK(memcmp(&vertices[2], &saved[2], sizeof(vertices[2])) == 0);
	memcpy(saved, vertices, sizeof(vertices));
	CHECK(mh_mesh_material_world_brightness(&mesh, brightness, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(vertices, saved, sizeof(vertices)) == 0);
	CHECK(mesh.material_rgb[0] == 50.5f && mesh.material_rgb[3] == 20.5f);
	free(mesh.material_rgb);
}

static void
check_scene_registration(const struct mh_world *world,
	const struct mh_model *model, const struct mh_scene_object *pose,
	const double brightness[3], struct mh_mesh *mesh)
{
	struct mh_model copies[2];
	struct mh_world reordered;
	struct mh_world_cell cells[2];
	struct mh_geometry_halo halo;
	float tint[3];
	struct mh_scene_object ordered_poses[3];
	struct mh_scene_instance ordered[3];
	struct mh_mesh_span span, before;
	struct mh_atlas_map remap;
	struct mh_texture_groups groups, saved_groups;
	unsigned char remap_data[28 + 5 * 49], saved_indices[5], *old_indices,
		*record;
	static const size_t storage[3] = {2, 4, 3};
	static const size_t halo_order[3] = {2, 0, 1};
	static const unsigned char expected_groups[5] = {2, 1, 3, 5, 4};
	size_t i, saved_count, source_first, source_count;

	copies[0] = copies[1] = *model;

	for (i = 0; i < 3; i++) {
		ordered_poses[i] = *pose;
		ordered_poses[i].position[0] = 10 * (i + 1);
		ordered[i].model = &copies[i == 1 ? 1 : 0];
		ordered[i].pose = &ordered_poses[i];
	}

	CHECK(mh_mesh_scene_span_count(NULL) == 0);

	/*
	 * Registration must follow grid references, even when the grid
	 * and model storage orders differ (the shipped worlds are
	 * monotonic).
	 */
	reordered = *world;
	cells[0] = world->cells[1];
	cells[1] = world->cells[0];
	reordered.cells = cells;
	CHECK(mh_mesh_build_world_native(mh_image_fallback_decoder(),
		&reordered, NULL, brightness, ordered, 3, mesh,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_scene_span_count(mesh) == 4);
	CHECK(mh_mesh_scene_span(mesh, 0, &span) && span.draw_first == 0 &&
		span.draw_count == 2 && span.primitive_first == 0 &&
		span.primitive_count == 2);
	memset(copies, 0, sizeof(copies));
	memset(ordered_poses, 0, sizeof(ordered_poses));

	for (i = 0; i < 3; i++) {
		CHECK(mh_mesh_scene_span(mesh, i + 1, &span));
		CHECK(span.draw_first == storage[i] &&
			span.primitive_first == storage[i] &&
			span.draw_count == 1 && span.primitive_count == 1);
		CHECK(mesh->vertices[mesh->draws[span.draw_first].first]
				.position[0] == 10 * (i + 1));
		CHECK(mh_mesh_scene_halo_span(
			mesh, i, &source_first, &source_count));
		CHECK(source_first == halo_order[i] && source_count == 1);
		CHECK(mh_mesh_scene_halo(mesh, source_first, &halo, tint) &&
			halo.endpoints[0][0] == 10 * (i + 1));
	}

	source_first = 123;
	source_count = 456;
	CHECK(!mh_mesh_scene_halo_span(mesh, 3, &source_first, &source_count) &&
		source_first == 123 && source_count == 456);
	memset(remap_data, 0, sizeof(remap_data));
	memset(&groups, 0, sizeof(groups));
	write_word(remap_data, 5);
	write_word(remap_data + 4, 5);
	write_word(remap_data + 12, 256);
	write_word(remap_data + 16, 256);
	write_word(remap_data + 20, 0x3f800000UL);
	write_word(remap_data + 24, 0x3f800000UL);

	for (i = 0; i < 5; i++) {
		record = remap_data + 28 + i * 49;
		record[0] = (unsigned char)('a' + i);
		memcpy(record + 1, ".iff", 5);
		write_word(record + 13, (unsigned long)i);
		write_word(record + 33, 1);
		write_word(record + 37, 1);
		write_word(record + 41, 1);
		write_word(record + 45, 1);
		strcpy(mesh->draws[i].texture_name, (const char *)record);
		mesh->draw_hidden[i] = 1;
	}

	CHECK(mh_atlas_remap_decode(remap_data, sizeof(remap_data), &remap,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_register_textures(mesh, &remap, &groups, NULL) ==
			MH_ERROR_NONE &&
		groups.count == 5);
	CHECK(memcmp(mesh->draw_groups, expected_groups, 5) == 0);
	CHECK(groups.pages[0] == 1 && groups.pages[1] == 0 &&
		groups.pages[2] == 2 && groups.pages[3] == 4 &&
		groups.pages[4] == 3);
	memset(&groups, 0, sizeof(groups));
	saved_groups = groups;
	old_indices = mesh->draw_groups;
	memcpy(saved_indices, old_indices, 5);
	mesh->draws[3].texture_name[0] = 'z';
	CHECK(mh_mesh_register_textures(mesh, &remap, &groups, NULL) !=
		MH_ERROR_NONE);
	CHECK(mesh->draw_groups == old_indices &&
		memcmp(old_indices, saved_indices, 5) == 0 &&
		memcmp(&groups, &saved_groups, sizeof(groups)) == 0);
	mesh->draws[3].texture_name[0] = 'd';
	CHECK(mh_mesh_register_textures(mesh, &remap, &groups, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(mesh->draw_groups, saved_indices, 5) == 0 &&
		groups.count == 5);
	before = span;
	CHECK(!mh_mesh_scene_span(mesh, 4, &span) &&
		memcmp(&span, &before, sizeof(span)) == 0);
	saved_count = mesh->draw_count;
	mesh->draw_count = 0;
	CHECK(!mh_mesh_scene_span(mesh, 1, &span) &&
		memcmp(&span, &before, sizeof(span)) == 0);
	mesh->draw_count = saved_count;
	mh_atlas_map_free(&remap);
	mh_mesh_free(mesh);
	CHECK(mh_mesh_scene_span_count(mesh) == 0);
}

int
main(void)
{
	static const double brightness[3] = {2.0, 0.5, 1.0};
	static const double dim[3] = {.5, .25, .125},
			bad_brightness[3] = {1, 1, -1};

	unsigned char data[1240];
	unsigned char saved_data[1240];
	unsigned char model_data[1360];
	struct mh_myw world;
	struct mh_world geometry;
	struct mh_myw saved;
	struct mh_myo model;
	struct mh_scene_transform physical[2];
	struct mh_myo untouched;
	struct mh_model decoded;
	struct mh_geometry_face face;
	struct mh_geometry_halo halo, saved_halo;
	float tint[3], saved_tint[3];
	float center[3], radius, saved_center[3], saved_radius;
	struct mh_mesh mesh;
	struct mh_frustum frustum;
	struct mh_world_view world_view;
	struct mh_myw_cell cell;
	float camera[12];
	struct mh_scene_object poses[2];
	struct mh_scene_instance instances[2];
	struct mh_scene_key keys[2];
	struct mh_vertex initial[15];
	enum mh_error_code status;
	size_t size;
	size_t cursor;
	unsigned long seed;
	unsigned long index;
	int i;

	check_software_visibility();

	prepare_world(data);
	memcpy(saved_data, data, sizeof(data));
	status = mh_myw_parse(data, sizeof(data), &world, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status != MH_ERROR_NONE)
		return 1;

	CHECK(memcmp(data, saved_data, sizeof(data)) == 0);
	CHECK(world.width == 2 && world.height == 1 && world.cell_count == 2);
	CHECK(world.primitive_types[4] == 1 && world.primitive_types[5] == 1);
	CHECK(world.minimum[0] == 0.0 && world.maximum[0] == 11.0);
	CHECK(world.minimum[1] == 0.0 && world.maximum[1] == 1.0);
	CHECK(mh_myw_cell(&world, 1, &cell) && cell.model == 2 &&
		cell.halo_count == 0);
	CHECK(!mh_myw_cell(&world, 2, &cell) && cell.model == 2);
	CHECK(mh_myw_model_bounds(&world, 2, center, &radius) &&
		center[0] == 10.5f && radius == 1);
	CHECK(!mh_myw_model_bounds(&world, 3, center, &radius) &&
		center[0] == 10.5f && radius == 1);
	CHECK(mh_myw_model(&world, 1, &model, NULL) == MH_ERROR_NONE);
	CHECK(model.blocks[MH_MYO_PRIMITIVES].count == 0);
	cursor = 0;
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) == MH_ERROR_NONE &&
		face.count == 0);
	CHECK(mh_myw_model(&world, 2, &model, NULL) == MH_ERROR_NONE);
	cursor = 0;
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) == MH_ERROR_NONE);
	CHECK(face.count == 4 && face.corners[1].position[0] == 11.0);
	CHECK(face.corners[1].color[1] == 255 &&
		face.corners[2].color[2] == 255);
	untouched = model;
	CHECK(mh_myw_model(&world, 3, &model, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&model, &untouched, sizeof(model)) == 0);
	CHECK(mh_world_myw_decode(data, sizeof(data), &geometry, NULL) ==
		MH_ERROR_NONE);
	status = mh_mesh_build_world(mh_image_fallback_decoder(), &geometry,
		NULL, brightness, &mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.vertex_count == 9 && mesh.draw_count == 2);
		CHECK(mesh.primitive_count == 2 &&
			mesh.primitives[0].first == 0 &&
			mesh.primitives[0].count == 3 &&
			mesh.primitives[1].first == 3 &&
			mesh.primitives[1].count == 6 &&
			mesh.primitives[1].draw == 1);
		CHECK(mesh.texture_count == 1 && mesh.draws[0].prelit == 1);
		CHECK(mesh.vertices[0].color[0] == 1.0f);
		CHECK(mesh.vertices[1].color[1] == 0.5f);
		CHECK(mesh.vertices[2].color[2] == 1.0f);
		CHECK(fabs(mesh.vertices[0].position[0] + 11.0 / sqrt(122.0)) <
			1e-6);
		memcpy(initial, mesh.vertices, 9 * sizeof(*initial));
		CHECK(mesh.world_vertex_count == 9 &&
			mesh.world_colors[0] == 255);
		CHECK(mesh.world_brightness[0] == (float)brightness[0] &&
			mesh.world_brightness[1] == (float)brightness[1] &&
			mesh.world_brightness[2] == (float)brightness[2]);
		CHECK(mh_mesh_world_brightness(&mesh, dim, NULL) ==
				MH_ERROR_NONE &&
			mesh.vertices[0].color[0] == .5f);
		CHECK(mesh.world_brightness[0] == (float)dim[0]);
		CHECK(mh_mesh_world_brightness(&mesh, brightness, NULL) ==
			MH_ERROR_NONE);
		CHECK(memcmp(initial, mesh.vertices, 9 * sizeof(*initial)) ==
			0);
		CHECK(mesh.world_brightness[0] == (float)brightness[0] &&
			mesh.world_brightness[1] == (float)brightness[1] &&
			mesh.world_brightness[2] == (float)brightness[2]);
		CHECK(mh_mesh_world_brightness(&mesh, bad_brightness, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(initial, mesh.vertices, 9 * sizeof(*initial)) ==
			0);
		CHECK(mesh.world_brightness[0] == (float)brightness[0] &&
			mesh.world_brightness[1] == (float)brightness[1] &&
			mesh.world_brightness[2] == (float)brightness[2]);

		/*
		 * Same-material models retain independent visibility. The
		 * metadata owns its grid/bounds; fitting the inspection
		 * geometry does not change the original-world camera
		 * coordinates used here.
		 */
		memset(&world_view, 0, sizeof(world_view));
		world_view.end[0] = 1;
		world_view.end[1] = 1;
		memset(data, 0, sizeof(data));
		CHECK(mh_mesh_world_cull(&mesh, &world_view, NULL) ==
			MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 0 && mesh.draw_hidden[1] == 1);
		world_view.first[0] = 1;
		world_view.end[0] = 2;
		CHECK(mh_mesh_world_cull(&mesh, &world_view, NULL) ==
			MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 1 && mesh.draw_hidden[1] == 0);
		world_view.end[0] = 3;
		CHECK(mh_mesh_world_cull(&mesh, &world_view, NULL) !=
			MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 1 && mesh.draw_hidden[1] == 0);
		world_view.first[0] = 0;
		world_view.end[0] = 2;
		memset(camera, 0, sizeof(camera));
		camera[0] = camera[4] = camera[8] = 1;
		camera[11] = -20;
		CHECK(mh_frustum_setup(640, 480, 10, .5f, 120, camera,
			&world_view.frustum, NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_world_cull(&mesh, &world_view, NULL) ==
			MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 0 && mesh.draw_hidden[1] == 1);
		CHECK(mh_mesh_world_cull(&mesh, NULL, NULL) == MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 0 && mesh.draw_hidden[1] == 0);
		memcpy(data, saved_data, sizeof(data));
		mh_mesh_free(&mesh);
	}

	CHECK(mh_myw_model(&world, 0, &model, NULL) == MH_ERROR_NONE);
	memcpy(model_data, data, sizeof(data));
	memset(model_data + 1240, 0xcd, 120);
	write_word(model_data + 1240, 2);
	write_word(model_data + 1244, 1);

	for (i = 0; i < 6; i++)
		write_word(model_data + 1248 + i * 4,
			i == 0 || i == 4 ? 0x3f800000UL : 0);

	model.data = model_data;
	model.auxiliary_count = 1;
	model.auxiliary_offset = 1240;
	model.radius = 2.5;
	CHECK(mh_model_myo_expand(&model, &decoded, NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_build_native(mh_image_fallback_decoder(), &decoded, NULL,
		&mesh, NULL) == MH_ERROR_NONE);
	CHECK(mesh.vertices[0].position[0] == 0 &&
		mesh.vertices[1].position[0] == 1);
	CHECK(mesh.halo_count == 1 && mesh.halos[0].endpoints[0][0] == 1 &&
		mesh.halos[0].resource == 1);
	mh_mesh_free(&mesh);
	memset(poses, 0, sizeof(poses));

	for (i = 0; i < 2; i++) {
		poses[i].position[0] = 2;
		poses[i].rotation[2] = -1;
		poses[i].rotation[4] = 1;
		poses[i].rotation[6] = 1;
		poses[i].color[1] = 0.5;
		poses[i].color[2] = 1;
		instances[i].model = &decoded;
		instances[i].pose = &poses[i];
	}

	/* The profile reaches both baked world faces and retained scenery. */
	status = mh_mesh_build_world_native_profile(mh_image_fallback_decoder(),
		&geometry, NULL, brightness, instances, 2, MH_RENDER_SOFTWARE,
		&mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.primitive_count == 4 && mesh.vertex_count == 15);
		CHECK(mesh.primitives[0].type == 0 &&
			mesh.primitives[1].type == 1 &&
			mesh.primitives[2].type == 0 &&
			mesh.primitives[3].type == 0);
		CHECK(mesh.draws[0].prelit && mesh.draws[1].prelit &&
			!mesh.draws[2].prelit && !mesh.draws[3].prelit);

		for (cursor = 0; cursor < mesh.world_vertex_count; cursor++)
			CHECK(mesh.world_colors[cursor * 3] == 255 &&
				mesh.world_colors[cursor * 3 + 1] == 0 &&
				mesh.world_colors[cursor * 3 + 2] == 0);

		CHECK(mh_mesh_animate(&mesh, 0, NULL) == MH_ERROR_NONE);
		mh_mesh_free(&mesh);
	}

	check_scene_registration(
		&geometry, &decoded, &poses[0], brightness, &mesh);

	status = mh_mesh_build_world_scene(mh_image_fallback_decoder(),
		&geometry, NULL, brightness, instances, 2, &mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.vertex_count == 15 && mesh.draw_count == 4);
		CHECK(mesh.primitive_count == 4 &&
			mesh.primitives[2].first == 9 &&
			mesh.primitives[2].draw == 2 &&
			mesh.primitives[2].count == 3 &&
			mesh.primitives[3].first == 12 &&
			mesh.primitives[3].draw == 3);
		CHECK(mesh.texture_count ==
			2); /* White image shared by both instances. */
		CHECK(mesh.draws[2].texture == mesh.draws[3].texture);
		CHECK(mesh.draws[2].first == 9 && mesh.draws[3].first == 12);
		CHECK(fabs(mesh.vertices[10].position[2] + 2.0 / sqrt(122.0)) <
			1e-6);
		CHECK(fabs(mesh.vertices[10].position[0] + 7.0 / sqrt(122.0)) <
			1e-6);
		CHECK(mesh.vertices[9].normal[0] == 1 &&
			mesh.vertices[9].normal[2] == 0);
		CHECK(mesh.vertices[9].color[0] == 0 &&
			mesh.vertices[10].color[1] == 0.5f);
		CHECK(mh_mesh_scene_halo_bounds(&mesh, 0, center, &radius));
		CHECK(fabs(center[0] + 7 / sqrt(122.0)) < 1e-6 &&
			fabs(radius - 5 / sqrt(122.0)) < 1e-6);
		mh_mesh_free(&mesh);
	}

	/* A zero RGB channel must not collapse the triangle's geometry. */
	CHECK(memcmp(data, saved_data, sizeof(data)) == 0);
	memset(keys, 0, sizeof(keys));
	keys[0].value[0] = 2;
	keys[0].value[3] = 90;
	keys[0].value[7] = 0.5;
	keys[0].value[8] = 1;
	keys[1] = keys[0];
	keys[1].time = 2;
	keys[1].linear = 1;
	keys[1].value[0] = 4;
	keys[1].value[3] = 270;
	keys[1].value[6] = 1;
	poses[0].keys = keys;
	poses[0].key_count = 2;
	poses[0].end_behavior = 2;
	CHECK(mh_scene_sample(&poses[0], 0, &poses[0], NULL) == MH_ERROR_NONE);
	poses[1].has_shadow_options =
		1; /* A static body also retains its local vertices. */
	status = mh_mesh_build_world_native(mh_image_fallback_decoder(),
		&geometry, NULL, brightness, instances, 2, &mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.vertex_count == 15 &&
			mesh.vertices[0].position[0] == 0);
		CHECK(mesh.primitive_count == 4 &&
			mesh.primitives[3].first == 12 &&
			mesh.primitives[3].draw == 3 &&
			mesh.primitives[3].count == 3);
		CHECK(mesh.vertices[9].position[0] == 2 &&
			mesh.vertices[10].position[2] == -1);
		memcpy(initial, mesh.vertices, sizeof(initial));
		CHECK(mh_mesh_animate(&mesh, 1, NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_scene_halo_count(&mesh) == 2);
		CHECK(mh_mesh_scene_halo(&mesh, 1, &halo, tint));
		CHECK(mh_mesh_scene_halo_bounds(&mesh, 1, center, &radius));
		CHECK(center[0] == 3 && center[1] == 0 && center[2] == 0 &&
			radius == 2.5f);
		CHECK(fabs(halo.endpoints[0][0] -
			mesh.vertices[10].position[0]) < 1e-6 &&
			fabs(halo.endpoints[0][2] -
				mesh.vertices[10].position[2]) < 1e-6 &&
			tint[0] == .5f);
		CHECK(memcmp(mesh.vertices, initial, 9 * sizeof(*initial)) ==
			0);
		CHECK(memcmp(mesh.vertices + 12, initial + 12,
			3 * sizeof(*initial)) == 0);
		CHECK(fabs(mesh.vertices[9].position[0] - 3) < 1e-6);
		CHECK(fabs(mesh.vertices[10].position[0] - 2) < 1e-6);
		CHECK(fabs(mesh.vertices[9].normal[2] + 1) < 1e-6);
		CHECK(mh_mesh_animate(&mesh, 2, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(mesh.vertices, initial, sizeof(initial)) == 0);
		memset(physical, 0, sizeof(physical));
		physical[0].scene_index = 1;
		physical[0].position[0] = 10;
		physical[0].position[1] = 20;
		physical[0].position[2] = 30;
		physical[0].rotation[0] = physical[0].rotation[4] =
			physical[0].rotation[8] = 1;
		CHECK(mh_mesh_animate_objects(&mesh, 1, physical, 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mesh_scene_halo(&mesh, 0, &halo, tint));
		CHECK(halo.endpoints[0][0] == 11 &&
			halo.endpoints[0][1] == 20 &&
			halo.endpoints[0][2] == 30);
		CHECK(mh_mesh_scene_halo_bounds(&mesh, 0, center, &radius));
		CHECK(center[0] == 10 && center[1] == 20 && center[2] == 30 &&
			radius == (float)sqrt(2.0));
		memcpy(saved_center, center, sizeof(center));
		saved_radius = radius;
		CHECK(!mh_mesh_scene_halo_bounds(&mesh, 2, center, &radius));
		CHECK(!mh_mesh_scene_halo_bounds(NULL, 0, center, &radius));
		CHECK(memcmp(center, saved_center, sizeof(center)) == 0 &&
			radius == saved_radius);
		saved_halo = halo;
		memcpy(saved_tint, tint, sizeof(tint));
		CHECK(!mh_mesh_scene_halo(&mesh, 2, &halo, tint) &&
			memcmp(&halo, &saved_halo, sizeof(halo)) == 0 &&
			memcmp(tint, saved_tint, sizeof(tint)) == 0);
		CHECK(mesh.vertices[12].position[0] == 10 &&
			mesh.vertices[12].position[1] == 20 &&
			mesh.vertices[12].position[2] == 30);
		CHECK(mesh.vertices[13].position[0] == 11);
		CHECK(mesh.vertices[9].position[0] == 3 &&
			mesh.vertices[9].color[0] == .5f);
		memset(camera, 0, sizeof(camera));
		camera[0] = camera[4] = camera[8] = 1;
		camera[11] = -20;
		CHECK(mh_frustum_setup(640, 480, 1, .5f, 120, camera, &frustum,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_scene_cull(&mesh, &frustum, NULL) ==
			MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 0 && mesh.draw_hidden[1] == 0 &&
			mesh.draw_hidden[2] == 0 && mesh.draw_hidden[3] == 0);
		camera[9] = 1000;
		CHECK(mh_frustum_setup(640, 480, 1, .5f, 120, camera, &frustum,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_scene_cull(&mesh, &frustum, NULL) ==
			MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 0 && mesh.draw_hidden[1] == 0 &&
			mesh.draw_hidden[2] == 1 && mesh.draw_hidden[3] == 1);
		CHECK(mh_mesh_scene_cull(&mesh, NULL, NULL) == MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 0 && mesh.draw_hidden[1] == 0 &&
			mesh.draw_hidden[2] == 0 && mesh.draw_hidden[3] == 0);

		/*
		 * Narrow camera sees the physical instance, not the animated
		 * one. Shared model/texture storage must not merge their
		 * visibility.
		 */
		camera[9] = 10;
		camera[10] = 20;
		camera[11] = 10;
		CHECK(mh_frustum_setup(640, 480, 10, .5f, 120, camera, &frustum,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_scene_cull(&mesh, &frustum, NULL) ==
			MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 0 && mesh.draw_hidden[1] == 0 &&
			mesh.draw_hidden[2] == 1 && mesh.draw_hidden[3] == 0);

		/* Binary32 sphere products can change a boundary decision. */
		memset(&frustum, 0, sizeof(frustum));
		frustum.planes[0][0] = -.7481428384780884f;
		frustum.planes[0][1] = .07437640428543091f;
		frustum.planes[0][2] = .02672875113785267f;
		frustum.planes[0][3] = 6.606251239776611f;
		CHECK(mh_mesh_scene_cull_profile(MH_RENDER_SOFTWARE, &mesh,
			&frustum, NULL) == MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[3] == 1);
		CHECK(mh_mesh_scene_cull(&mesh, &frustum, NULL) ==
			MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[3] == 0);
		CHECK(mh_mesh_scene_cull_profile((enum mh_render_profile) - 1,
			&mesh, &frustum, NULL) == MH_ERROR_INVALID);
		CHECK(mesh.draw_hidden[3] == 0);

		/*
		 * Restore the narrow camera masks for the failed-range check.
		 */
		CHECK(mh_frustum_setup(640, 480, 10, .5f, 120, camera, &frustum,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_scene_cull(&mesh, &frustum, NULL) ==
			MH_ERROR_NONE);
		size = mesh.draw_count;
		mesh.draw_count = 1;
		CHECK(mh_mesh_scene_cull(&mesh, NULL, NULL) != MH_ERROR_NONE);
		CHECK(mesh.draw_hidden[0] == 0 && mesh.draw_hidden[1] == 0 &&
			mesh.draw_hidden[2] == 1 && mesh.draw_hidden[3] == 0);
		mesh.draw_count = size;
		CHECK(mh_mesh_scene_cull(&mesh, NULL, NULL) == MH_ERROR_NONE);
		memcpy(initial, mesh.vertices, sizeof(initial));
		physical[1] = physical[0];
		CHECK(mh_mesh_animate_objects(&mesh, 2, physical, 2, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(mesh.vertices, initial, sizeof(initial)) == 0);
		CHECK(mh_mesh_scene_halo(&mesh, 0, &halo, tint) &&
			memcmp(&halo, &saved_halo, sizeof(halo)) == 0);
		CHECK(mh_mesh_scene_halo_bounds(&mesh, 0, center, &radius));
		CHECK(memcmp(center, saved_center, sizeof(center)) == 0 &&
			radius == saved_radius);
		physical[0].scene_index = 99;
		CHECK(mh_mesh_animate_objects(&mesh, 2, physical, 1, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(mesh.vertices, initial, sizeof(initial)) == 0);
		check_physical_rotation(&mesh);
		mh_mesh_free(&mesh);
	}

	/*
	 * UnseenByRays skips the physics radius override, even when the
	 * finalization marker is present. Scale belongs to the pose only.
	 */
	poses[1].unseen_by_rays = 1;
	poses[1].rotation[6] = .5;
	status = mh_mesh_build_world_native(mh_image_fallback_decoder(),
		&geometry, NULL, brightness, instances, 2, &mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mh_mesh_scene_halo_bounds(&mesh, 0, center, &radius) &&
			radius == 2.5f);
		mh_mesh_free(&mesh);
	}

	poses[1].unseen_by_rays = 0;
	poses[1].rotation[6] = 1;
	status = mh_mesh_build_world_scene(mh_image_fallback_decoder(),
		&geometry, NULL, brightness, instances, 2, &mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.scenery != NULL && mesh.vertex_count == 15 &&
			mesh.texture_count == 2);
		memcpy(initial, mesh.vertices, sizeof(initial));

		/*
		 * Motion and original geometry may disappear after mesh
		 * construction.
		 */
		memset(keys, 0, sizeof(keys));
		memset(data, 0, sizeof(data));
		memset(model_data, 0, sizeof(model_data));
		model.radius = 99;
		CHECK(mh_mesh_animate(&mesh, 1, NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_scene_halo(&mesh, 1, &halo, tint));
		CHECK(mh_mesh_scene_halo_bounds(&mesh, 1, center, &radius));
		CHECK(fabs(center[0] + 5 / sqrt(122.0)) < 1e-6 &&
			fabs(radius - 5 / sqrt(122.0)) < 1e-6);
		CHECK(fabs(halo.endpoints[0][0] -
			mesh.vertices[10].position[0]) < 1e-6 &&
			tint[0] == .5f);
		CHECK(memcmp(mesh.vertices, initial, 9 * sizeof(*initial)) ==
			0);
		CHECK(memcmp(mesh.vertices + 12, initial + 12,
			3 * sizeof(*initial)) == 0);
		CHECK(fabs(mesh.vertices[9].position[0] + 5.0 / sqrt(122.0)) <
			1e-6);
		CHECK(fabs(mesh.vertices[10].position[0] + 7.0 / sqrt(122.0)) <
			1e-6);
		CHECK(fabs(mesh.vertices[9].normal[2] + 1) < 1e-6);
		CHECK(mesh.vertices[9].color[0] == 0.5f);
		CHECK(mh_mesh_animate(&mesh, 2, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(mesh.vertices, initial, sizeof(initial)) == 0);
		CHECK(mh_mesh_animate(&mesh, -1, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(mesh.vertices, initial, sizeof(initial)) == 0);
		CHECK(mesh.draw_count == 4 && mesh.texture_count == 2);
		mh_mesh_free(&mesh);
		memcpy(data, saved_data, sizeof(data));
	}

	poses[0].keys = NULL;
	poses[0].key_count = 0;
	poses[0].color[0] = -1;
	memset(&mesh, 0, sizeof(mesh));
	CHECK(mh_mesh_build_world_scene(mh_image_fallback_decoder(), &geometry,
		NULL, brightness, instances, 2, &mesh,
		NULL) != MH_ERROR_NONE);
	CHECK(mesh.vertices == NULL && mesh.textures == NULL);
	saved = world;

	for (size = 0; size < sizeof(data); size++) {
		write_word(data + 4, (unsigned long)size);
		CHECK(mh_myw_parse(data, size, &world, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&world, &saved, sizeof(world)) == 0);
	}

	CHECK(mh_myw_parse(NULL, sizeof(data), &world, NULL) != MH_ERROR_NONE);
	CHECK(mh_myw_parse(data, sizeof(data), NULL, NULL) != MH_ERROR_NONE);

	for (i = 0; i < 14; i++) {
		prepare_world(data);

		if (i == 0)
			write_word(data + 8, MH_U32_MAX);

		if (i == 1)
			write_word(data + 0x64, 0xfffffff0UL);

		if (i == 2)
			write_word(data + 0x60, 683);

		if (i == 3)
			write_word(data + 1192, 1);

		if (i == 4)
			write_word(data + 1200, 0);

		if (i == 5)
			write_word(data + 1208, 1);

		if (i == 6)
			write_word(data + 468 + 0x8c,
				0); /* Overlapping model slice. */

		if (i == 7)
			data[1080] = 10;

		if (i == 8)
			data[1096] =
				3; /* First face vertex past local range. */

		if (i == 9)
			data[1102] = 1; /* Normal past local range. */

		if (i == 10)
			write_word(data + 924, 0x7f800000UL);

		if (i == 11)
			write_word(data + 0x80, 1);

		if (i == 12)
			write_word(data + 240 + 0x70, 0x7fc00000UL);

		if (i == 13)
			write_word(data + 240 + 0x7c, 0xbf800000UL);

		CHECK(mh_myw_parse(data, sizeof(data), &world, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&world, &saved, sizeof(world)) == 0);
	}

	seed = 107;

	for (i = 0; i < 4000; i++) {
		prepare_world(data);
		seed = (seed * MUTATION_MULTIPLIER + MUTATION_INCREMENT) &
			MH_U32_MAX;
		write_word(data + seed % (sizeof(data) - 3), seed);
		memcpy(&world, &saved, sizeof(world));
		status = mh_myw_parse(data, sizeof(data), &world, NULL);

		if (status != MH_ERROR_NONE) {
			CHECK(memcmp(&world, &saved, sizeof(world)) == 0);
		} else {
			for (index = 0;
				index < world.blocks[MH_MYW_MODELS].count;
				index++) {
				CHECK(mh_myw_model(&world, index, &model,
					NULL) == MH_ERROR_NONE);
			}
		}
	}

	mh_model_free(&decoded);
	mh_world_free(&geometry);
	check_direct3d_brightness();
	check_decoded_world();

	if (failures != 0)
		return 1;

	puts("MYW geometry, baked colors, shared mesh and malformed-input checks passed");

	return 0;
}
