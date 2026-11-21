#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/provider.h"
#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/geometry/world.h"
#include "engine/render/atmosphere_lamps.h"
#include "engine/render/atmosphere_sky.h"
#include "engine/render/powervr_halo.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
put_u32be(unsigned char *output, unsigned long value)
{
	output[0] = (unsigned char)(value >> 24);
	output[1] = (unsigned char)(value >> 16);
	output[2] = (unsigned char)(value >> 8);
	output[3] = (unsigned char)value;
}

static void
projection_edges(struct mh_lamps *lamps, const struct mh_world *world)
{
	static const float identity[16] = {
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	static const float depths[7] = {.249f, .25f, .251f, .5f, 1, 180, 181};
	struct mh_geometry_halo halo;
	struct mh_attached_halo style;
	float matrix[16];
	unsigned int axis, side, depth, vertex;

	memset(&halo, 0, sizeof(halo));
	memset(&style, 0, sizeof(style));
	halo.endpoints[0][2] = halo.endpoints[1][2] = 32;
	memset(halo.color, 255, 3);
	style.flags = 2;
	style.intensity = 1;
	style.size = .5f;
	CHECK(mh_lamps_reserve_attached(lamps, 1, NULL) == MH_ERROR_NONE);

	/*
	 * Finite world points beside the near plane can project millions of
	 * pixels off screen. They must be culled without ending the race.
	 */
	for (axis = 0; axis < 2; axis++)
		for (side = 0; side < 2; side++)
			for (depth = 0; depth < 7; depth++) {
				memcpy(matrix, identity, sizeof(matrix));
				matrix[11] = 1;
				matrix[15] = depths[depth] - 32;
				matrix[12 + axis] = side ? -4096 : 4096;
				CHECK(mh_lamps_update(lamps, world, matrix, 640,
					480, .25f, 180, .22f,
					NULL) == MH_ERROR_NONE);
				CHECK(mh_lamps_attached_update(lamps, 1, &halo,
					&style, NULL, matrix, identity,
					640, 480, .25f, 180, .22f,
					NULL) == MH_ERROR_NONE);
				CHECK(!lamps->probes[0].active &&
					!lamps->probes[1].active);

				for (vertex = 0; vertex < 108; vertex++)
					CHECK(lamps->mesh.vertices[vertex]
							.color[0] == 0);
			}

	memcpy(matrix, identity, sizeof(matrix));
	matrix[11] = 1;
	matrix[15] = 0;
	CHECK(mh_lamps_update(lamps, world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_lamps_attached_update(lamps, 1, &halo, &style, NULL, matrix,
		identity, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(lamps->probes[0].active && lamps->probes[1].active);
}

static void
check_assets(void)
{
	static const float identity[16] = {
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	unsigned char tga[26], iff[2110], copy[2110], hex[574];
	struct mh_mesh mesh, saved;
	struct mh_vertex original[18];
	struct mh_track track;
	struct mh_sky_state state;
	struct mh_world world;
	struct mh_lamps lamps, lamps_saved;
	struct mh_geometry_halo lamp_source, world_halo, saved_world_halo;
	struct mh_world_cell cell;
	struct mh_attached_halo lamp_style;
	struct mh_world_view world_view;
	struct mh_vertex lamp_vertices[54];
	float matrix[16], view[16];
	enum mh_error_code status;
	size_t i;

	memset(tga, 0, sizeof(tga));
	tga[2] = 3;
	tga[12] = 2;
	tga[14] = 4;
	tga[16] = 8;
	tga[17] = 32;

	for (i = 18; i < sizeof(tga); i++)
		tga[i] = (unsigned char)i;

	memset(&mesh, 0, sizeof(mesh));
	saved = mesh;

	for (i = 0; i < sizeof(tga); i++) {
		CHECK(mh_sky_mesh_create(mh_image_fallback_decoder(), tga, i,
			&mesh, NULL) != MH_ERROR_NONE &&
			memcmp(&mesh, &saved, sizeof(mesh)) == 0);
	}

	status = mh_sky_mesh_create(
		mh_image_fallback_decoder(), tga, sizeof(tga), &mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		memset(&track, 0, sizeof(track));
		track.horizon = .25;
		track.sky_top[0] = 10;
		track.sky_bottom[1] = 20;
		memset(&state, 0, sizeof(state));
		CHECK(mh_sky_mesh_update(&mesh, &state, &track, identity,
			identity, 640, 480, NULL) == MH_ERROR_NONE);

		for (i = 0; i < mesh.vertex_count; i++) {
			CHECK(mesh.vertices[i].opacity == 127.0f / 255);
			CHECK(mesh.vertices[i].projection_w == 10000);
		}

		memcpy(original, mesh.vertices, sizeof(original));
		memcpy(view, identity, sizeof(view));
		memcpy(matrix, identity, sizeof(matrix));
		view[12] = 1234;
		view[13] = -300;
		matrix[12] = 999;
		memset(&state, 0, sizeof(state));
		CHECK(mh_sky_mesh_update(&mesh, &state, &track, matrix, view,
			640, 480, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(original, mesh.vertices, sizeof(original)) == 0);
		CHECK(mesh.draws[0].texture == 1 && mesh.draws[1].texture == 0);
		CHECK(mesh.vertices[6].color[0] == 8.0f / 255 &&
			mesh.vertices[12].color[1] == 20.0f / 255);
		CHECK(mh_sky_mesh_black(&mesh, NULL) == MH_ERROR_NONE);
		CHECK(mesh.draws[0].texture == 0 && mesh.draws[0].count == 6);
		CHECK(mesh.draws[1].count == 0 && mesh.draws[2].count == 0);
		CHECK(mesh.vertices[0].position[0] == -1 &&
			mesh.vertices[0].position[1] == 1);
		CHECK(mesh.vertices[2].position[0] == 1 &&
			mesh.vertices[2].position[1] == -1);
		CHECK(mesh.vertices[0].color[0] == 0 &&
			mesh.vertices[0].color[1] == 0 &&
			mesh.vertices[0].color[2] == 0);
		CHECK(mesh.vertices[0].position[2] == .9999f);

		for (i = 0; i < 6; i++) {
			CHECK(mesh.vertices[i].opacity == 0);
			CHECK(mesh.vertices[i].projection_w == 10000);
		}

		CHECK(mh_sky_mesh_plain(&mesh, &state, &track, identity,
			identity, 641, 481, NULL) == MH_ERROR_NONE);
		CHECK(mesh.draws[0].count == 0 && mesh.draws[1].count == 6 &&
			mesh.draws[2].count == 6);
		CHECK(mesh.vertices[8].position[1] == 0 &&
			mesh.vertices[12].position[1] == 0);
		CHECK(mesh.vertices[6].color[0] == 8.0f / 255 &&
			mesh.vertices[12].color[1] == 20.0f / 255);

		for (i = 0; i < 18; i++)
			CHECK(mesh.vertices[i].uv[1] == 0);

		memset(&state, 0, sizeof(state));
		CHECK(mh_sky_mesh_update(&mesh, &state, &track, identity,
			identity, 640, 480, NULL) == MH_ERROR_NONE);
		CHECK(memcmp(original, mesh.vertices, sizeof(original)) == 0);
		CHECK(mesh.draws[0].texture == 1 && mesh.draws[1].count == 6 &&
			mesh.draws[2].count == 6);
		mh_mesh_free(&mesh);
	}

	/* Synthetic original-sized ILBM. No redistributed game resources. */
	memset(iff, 0, sizeof(iff));
	memcpy(iff, "FORM", 4);
	put_u32be(iff + 4, sizeof(iff) - 8);
	memcpy(iff + 8, "ILBMBMHD", 8);
	put_u32be(iff + 16, 20);
	iff[21] = iff[23] = 128;
	iff[28] = 1;
	memcpy(iff + 40, "CMAP", 4);
	put_u32be(iff + 44, 6);
	memset(iff + 51, 255, 3);
	memcpy(iff + 54, "BODY", 4);
	put_u32be(iff + 58, 2048);
	memset(iff + 62, 255, 2048);
	memcpy(copy, iff, sizeof(copy));
	memset(&world, 0, sizeof(world));
	status = mh_halo_mesh_create(mh_image_fallback_decoder(), &world, iff,
		sizeof(iff), &mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.vertex_count == 12 &&
			mesh.textures[0].rgba[0] == 255);
		CHECK(mh_halo_mesh_update(&mesh, &world, identity, 640, 480,
			.25f, 180, .22f, NULL) == MH_ERROR_NONE);

		for (i = 0; i < mesh.vertex_count; i++)
			CHECK(mesh.vertices[i].color[0] == 0);

		mh_mesh_free(&mesh);
	}

	CHECK(memcmp(copy, iff, sizeof(copy)) == 0);
	saved = mesh;

	for (i = 0; i < sizeof(iff); i += 17)
		CHECK(mh_halo_mesh_create(mh_image_fallback_decoder(), &world,
			iff, i, &mesh, NULL) != MH_ERROR_NONE &&
			memcmp(&mesh, &saved, sizeof(mesh)) == 0);

	memcpy(hex, iff, sizeof(hex));
	put_u32be(hex + 4, sizeof(hex) - 8);
	hex[21] = hex[23] = 64;
	put_u32be(hex + 58, 512);
	memset(&lamps, 0, sizeof(lamps));
	lamps_saved = lamps;
	CHECK(mh_lamps_create(mh_image_fallback_decoder(), &world, iff,
		sizeof(iff), iff, sizeof(iff), &lamps,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&lamps, &lamps_saved, sizeof(lamps)) == 0);

	for (i = 0; i < sizeof(hex); i += 17) {
		CHECK(mh_lamps_create(mh_image_fallback_decoder(), &world, iff,
			sizeof(iff), hex, i, &lamps,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&lamps, &lamps_saved, sizeof(lamps)) == 0);
	}

	CHECK(mh_lamps_create(mh_image_fallback_decoder(), &world, iff,
		sizeof(iff), hex, sizeof(hex), &lamps,
		NULL) == MH_ERROR_NONE);
	CHECK(lamps.count == 0 && lamps.mesh.vertex_count == 54);
	CHECK(mh_lamps_update(&lamps, &world, identity, 640, 480, .25f, 180,
		.22f, NULL) == MH_ERROR_NONE);
	mh_lamps_free(&lamps);
	memset(&world_halo, 0, sizeof(world_halo));
	memset(&cell, 0, sizeof(cell));
	world_halo.endpoints[0][2] = world_halo.endpoints[1][2] = 32;
	memset(world_halo.color, 255, sizeof(world_halo.color));
	saved_world_halo = world_halo;
	cell.model = MH_WORLD_NO_MODEL;
	cell.halo_count = 1;
	world.halos = &world_halo;
	world.halo_count = 1;
	world.width = world.height = world.cell_count = 1;
	world.cells = &cell;
	CHECK(mh_lamps_create(mh_image_fallback_decoder(), &world, iff,
		sizeof(iff), hex, sizeof(hex), &lamps,
		NULL) == MH_ERROR_NONE);
	CHECK(lamps.visible[0] == 1);
	lamps.visible[0] = 0;
	memcpy(matrix, identity, sizeof(matrix));
	matrix[11] = 1;
	matrix[15] = 0;
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(lamps.probes[0].active && lamps.probes[0].x == 320 &&
		lamps.probes[0].y == 240);
	CHECK(lamps.visible[0] == 0);

	for (i = 0; i < 54; i++)
		CHECK(lamps.mesh.vertices[i].color[0] == 0);

	lamps.visible[0] = 1;
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < 54; i++)
		CHECK(lamps.mesh.vertices[i].color[0] > 0);

	CHECK(lamps.mesh.draws[0].texture == 1 &&
		lamps.mesh.draws[0].count == 42 &&
		lamps.mesh.draws[1].texture == 0 &&
		lamps.mesh.draws[1].count == 12);
	CHECK(lamps.mesh.vertices[0].uv[0] == 0 &&
		lamps.mesh.vertices[2].uv[0] == 1 &&
		lamps.mesh.vertices[2].uv[1] == 1);
	memcpy(lamp_vertices, lamps.mesh.vertices, sizeof(lamp_vertices));
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, 1,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(lamp_vertices, lamps.mesh.vertices,
		sizeof(lamp_vertices)) == 0);
	memset(&world_view, 0, sizeof(world_view));
	CHECK(mh_lamps_world_cull(&lamps, &world, &world_view, NULL) ==
			MH_ERROR_NONE &&
		lamps.world_hidden[0]);
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(!lamps.probes[0].active && lamps.visible[0] == 1);

	for (i = 0; i < 54; i++)
		CHECK(lamps.mesh.vertices[i].color[0] == 0);

	world_view.end[0] = world_view.end[1] = 1;
	CHECK(mh_lamps_world_cull(&lamps, &world, &world_view, NULL) ==
			MH_ERROR_NONE &&
		!lamps.world_hidden[0]);
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(lamps.probes[0].active && lamps.visible[0] == 1);
	CHECK(memcmp(lamp_vertices, lamps.mesh.vertices,
		sizeof(lamp_vertices)) == 0);
	world_view.end[0] = 2;
	CHECK(mh_lamps_world_cull(&lamps, &world, &world_view, NULL) !=
			MH_ERROR_NONE &&
		!lamps.world_hidden[0]);
	lamps.visible[0] = 0;
	world_view.end[0] = 0;
	CHECK(mh_lamps_world_cull(&lamps, &world, &world_view, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(!lamps.probes[0].active && lamps.visible[0] == 0);
	CHECK(mh_lamps_world_cull(&lamps, &world, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(lamps.probes[0].active && lamps.visible[0] == 0);
	CHECK(memcmp(&world_halo, &saved_world_halo, sizeof(world_halo)) == 0 &&
		memcmp(iff, copy, sizeof(copy)) == 0);
	projection_edges(&lamps, &world);

	if (!MH_RENDER_BUILD_POWERVR) {
		mh_lamps_free(&lamps);
		return;
	}

	lamps.profile = MH_RENDER_POWERVR;

	/*
	 * Reservation owns and resizes raw colors together with the vertices.
	 */
	CHECK(mh_lamps_reserve_attached(&lamps, 2, NULL) == MH_ERROR_NONE);
	CHECK(lamps.mesh.material_rgb_count == lamps.mesh.vertex_count);
	lamps.visible[0] = 0;
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(!lamps.probes[0].active && lamps.visible[0] == 0);
	CHECK(fabs(lamps.mesh.vertices[42].projection_w - 30) < .0001);
	CHECK(lamps.mesh.material_rgb[42 * 3] > 0);
	CHECK(lamps.mesh.vertices[0].uv[0] == .5f &&
		lamps.mesh.vertices[2].uv[0] == .75f &&
		lamps.mesh.vertices[2].uv[1] == .25f);
	memset(&lamp_source, 0, sizeof(lamp_source));
	memset(&lamp_style, 0, sizeof(lamp_style));
	lamp_source.endpoints[0][2] = lamp_source.endpoints[1][2] = 32;
	lamp_source.color[0] = 128;
	lamp_source.color[1] = 64;
	lamp_source.color[2] = 32;
	lamp_style.flags = MH_HALO_GLOW_FLARES;
	lamp_style.intensity = .7f;
	lamp_style.size = .6f;
	lamp_style.flare = .5f;
	lamps.visible[1] = 0;
	CHECK(mh_lamps_attached_update(&lamps, 1, &lamp_source, &lamp_style,
		NULL, matrix, identity, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(!lamps.probes[1].active && lamps.visible[1] == 0);
	CHECK(fabs(lamps.mesh.vertices[96].projection_w - 31.6) < .0001);
	CHECK(lamps.mesh.material_rgb[96 * 3] > 0);
	CHECK(lamps.mesh.vertices[54].uv[0] == .5f);

	/*
	 * A near source can bias behind the SGL depth plane; its flare remains.
	 */
	lamp_source.endpoints[0][2] = lamp_source.endpoints[1][2] = .3f;
	CHECK(mh_lamps_attached_update(&lamps, 1, &lamp_source, &lamp_style,
		NULL, matrix, identity, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);
	CHECK(lamps.mesh.vertices[96].projection_w == 0 &&
		lamps.mesh.material_rgb[96 * 3] == 0);
	CHECK(lamps.mesh.vertices[54].projection_w > 0);
	lamps.halos = 0;
	CHECK(mh_lamps_update(&lamps, &world, matrix, 640, 480, .25f, 180, .22f,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < lamps.mesh.vertex_count * 3; i++)
		CHECK(lamps.mesh.material_rgb[i] == 0);

	CHECK(mh_lamps_reserve_attached(&lamps, 0, NULL) == MH_ERROR_NONE);
	CHECK(lamps.mesh.vertex_count == 54 &&
		lamps.mesh.material_rgb_count == 54);

	mh_lamps_free(&lamps);
	mh_lamps_free(&lamps);
}

static void
check_attached(void)
{
	struct mh_halo_projection projection;
	struct mh_attached_halo style;
	struct mh_effect_quad quads[8], saved[8];
	struct mh_depth_probe probe, saved_probe;
	unsigned int endpoint, quad, scenario;

	memset(&projection, 0, sizeof(projection));
	memset(&style, 0, sizeof(style));
	projection.width = 640;
	projection.height = 480;
	projection.pixel_aspect = 1;
	projection.near_depth = .25f;
	projection.far_depth = 180;
	projection.cue_start = .2f;

	for (endpoint = 0; endpoint < 2; endpoint++) {
		projection.endpoint[endpoint][0] = 320;
		projection.endpoint[endpoint][1] = 240;
		projection.endpoint[endpoint][2] = .1f;
		projection.endpoint[endpoint][3] = 1.0f / 18;
	}

	projection.color[0] = projection.color[1] = projection.color[2] = 150;
	style.flags = 14;
	style.intensity = .7f;
	style.size = .6f;
	style.flare = .5f;
	style.direction[2] = -1;
	style.inverse_focal = 1;
	CHECK(mh_attached_halo_quads(&projection, &style, 1, 1, &probe, quads,
		NULL) == MH_ERROR_NONE &&
		probe.active && quads[0].color[0] > 0 && quads[7].color[0] > 0);
	CHECK(mh_attached_halo_quads(&projection, &style, 0, 1, &probe, quads,
		NULL) == MH_ERROR_NONE &&
		probe.active);

	for (quad = 0; quad < 8; quad++)
		CHECK(quads[quad].depth == 0 && quads[quad].color[0] == 0);

	CHECK(mh_attached_halo_quads(&projection, &style, 1, 0, &probe, quads,
		NULL) == MH_ERROR_NONE &&
		quads[0].color[0] > 0 && quads[1].depth == 0);
	style.direction[2] = 1;
	CHECK(mh_attached_halo_quads(&projection, &style, 1, 1, &probe, quads,
		NULL) == MH_ERROR_NONE &&
		!probe.active && quads[0].depth == 0);
	style.direction[2] = -1;
	style.scenery = style.tinted = 1;
	style.tint[0] = -1;
	style.tint[1] = 2;
	style.tint[2] = 0;
	CHECK(mh_attached_halo_quads(&projection, &style, 1, 0, &probe, quads,
		NULL) == MH_ERROR_NONE &&
		quads[0].color[0] == 0 && quads[0].color[1] == 255 &&
		quads[0].color[2] == 0 && quads[1].depth != 0);
	memcpy(saved, quads, sizeof(quads));
	saved_probe = probe;

	for (scenario = 0; scenario < 6; scenario++) {
		if (scenario == 0)
			style.flags = 38;

		if (scenario == 1) {
			style.flags = 14;
			style.tint[0] = FLT_MAX;
		}

		if (scenario == 2) {
			style.tint[0] = 1;
			style.inverse_focal = 0;
		}

		if (scenario == 3) {
			style.inverse_focal = 1;
			projection.endpoint[0][0] = FLT_MAX;
		}

		if (scenario == 4) {
			projection.endpoint[0][0] = 320;
			style.scenery = 2;
		}

		if (scenario == 5) {
			style.scenery = 1;
			style.intensity = -1;
		}

		CHECK(mh_attached_halo_quads(&projection, &style, 1, 1, &probe,
			quads, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(quads, saved, sizeof(quads)) == 0 &&
			memcmp(&probe, &saved_probe, sizeof(probe)) == 0);
	}
}

int
main(void)
{
	struct mh_halo_projection projection;
	struct mh_effect_quad quads[3], saved[3];
	struct mh_sky_camera camera;
	struct mh_sky_state state, before;
	unsigned int endpoint;

	check_assets();
	check_attached();
	memset(&projection, 0, sizeof(projection));
	projection.width = 640;
	projection.height = 480;
	projection.pixel_aspect = 1;
	projection.near_depth = .25f;
	projection.far_depth = 180;
	projection.cue_start = .22f;
	projection.endpoint[0][0] = 310;
	projection.endpoint[1][0] = 330;

	for (endpoint = 0; endpoint < 2; endpoint++) {
		projection.endpoint[endpoint][1] = 100;
		projection.endpoint[endpoint][2] = .25f;
		projection.endpoint[endpoint][3] = 1.0f / 45;
	}

	projection.color[0] = 255;
	CHECK(mh_world_halo_quads(&projection, quads, NULL) == MH_ERROR_NONE);
	CHECK(fabs(quads[0].xy[0][0] - 246.68) < .001);
	CHECK(quads[0].color[0] == 255 && quads[0].color[2] == 0);
	CHECK(fabs(quads[1].color[1] - 172.1643) < .001);
	CHECK(fabs(quads[0].depth - .25 / 37) < 1e-8);
	memcpy(saved, quads, sizeof(quads));
	projection.cue_start = 1;
	CHECK(mh_world_halo_quads(&projection, quads, NULL) != MH_ERROR_NONE &&
		memcmp(quads, saved, sizeof(quads)) == 0);
	projection.cue_start = .22f;
	projection.endpoint[0][2] = -1;
	CHECK(mh_world_halo_quads(&projection, quads, NULL) == MH_ERROR_NONE &&
		quads[0].color[0] == 0);
	memset(&camera, 0, sizeof(camera));
	memset(&state, 0, sizeof(state));
	camera.width = 640;
	camera.height = 480;
	camera.focal = 1;
	camera.forward[0] = 1;
	camera.image_height = 196;
	camera.image_scale = 1;
	camera.horizon = .24f;
	CHECK(mh_sky_quads(&state, &camera, quads, NULL) == MH_ERROR_NONE);
	CHECK(quads[0].xy[0][0] == -640 && quads[0].xy[0][1] == 17 &&
		quads[0].xy[2][1] == 213);
	camera.image_scale = 480.0f / 512;
	CHECK(mh_sky_quads(&state, &camera, quads, NULL) == MH_ERROR_NONE);
	CHECK(quads[0].xy[0][1] == 23.125f && quads[0].xy[2][1] == 206.875f);
	CHECK(quads[0].uv[0][1] == 1.0f / 196);
	camera.image_scale = 1;
	CHECK(fabs(quads[0].uv[0][0] + .1) < 1e-6);
	before = state;
	CHECK(mh_sky_quads(&state, &camera, quads, NULL) == MH_ERROR_NONE &&
		fabs(state.phase - before.phase) < 1e-6);
	memcpy(saved, quads, sizeof(quads));
	before = state;
	camera.roll_sine = 2;
	CHECK(mh_sky_quads(&state, &camera, quads, NULL) != MH_ERROR_NONE &&
		memcmp(saved, quads, sizeof(quads)) == 0 &&
		memcmp(&state, &before, sizeof(state)) == 0);
	camera.roll_sine = 0;
	camera.image_height = 0;
	CHECK(mh_sky_quads(&state, &camera, quads, NULL) == MH_ERROR_NONE &&
		quads[0].xy[0][1] == 240 && quads[0].xy[2][1] == 240);
	CHECK(quads[1].xy[2][1] == quads[2].xy[0][1] &&
		quads[1].uv[0][1] == 0 && quads[2].uv[2][1] == 0);
	memcpy(saved, quads, sizeof(quads));
	before = state;
	camera.image_height = 1;
	CHECK(mh_sky_quads(&state, &camera, quads, NULL) != MH_ERROR_NONE &&
		memcmp(saved, quads, sizeof(quads)) == 0 &&
		memcmp(&state, &before, sizeof(state)) == 0);
	printf("atmosphere: %s\n", failures ? "FAILED" : "passed");

	return failures != 0;
}
