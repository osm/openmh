#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/font.h"
#include "engine/assets/name_atlas.h"
#include "engine/assets/name_mesh.h"
#include "engine/core/error.h"
#include "engine/render/name_label.h"
#include "engine/render/name_mesh.h"
#include "engine/render/powervr_lamp_atlas.h"

#define PATCHED_PLAYERS 13
#define PATCHED_ACTIVE_MASK 8191

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
atlas_mesh_contracts(void)
{
	struct mh_font font;
	struct mh_name_atlas atlas, saved;
	struct mh_atlas shared;
	struct mh_image page;
	unsigned char palette[768];
	float colors[6 * MH_NAME_LABELS_MAX * 3];
	struct mh_profile profiles[MH_NAME_LABELS_MAX];
	unsigned int i;
	struct mh_name_mesh names;
	struct mh_projection projection;
	struct mh_frustum frustum;
	struct mh_vertex vertices[6 * MH_NAME_LABELS_MAX];
	float pose[12], positions[3 * MH_NAME_LABELS_MAX],
		radii[MH_NAME_LABELS_MAX];

	memset(&font, 0, sizeof(font));
	memset(&atlas, 0, sizeof(atlas));
	memset(profiles, 0, sizeof(profiles));
	font.cell_width = 64;
	font.height = 19;
	font.image.width = 1024;
	font.image.height = 304;
	font.image.rgba = (unsigned char *)calloc(1024U * 304U, 4);
	CHECK(font.image.rgba != NULL);
	font.advance['W'] = 64;
	strcpy(profiles[0].nick, "WWWW");
	saved = atlas;
	CHECK(mh_name_atlas_create(&font, profiles, 8, 1, &atlas, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&atlas, &saved, sizeof(atlas)) == 0);

	/*
	 * Exactly 256px image width, last row of the eight-entry packed atlas.
	 */
	font.advance['W'] = 63;
	font.advance['A'] = 2;

	for (i = 0; i < 8; i++)
		strcpy(profiles[i].nick, "WWWWA");

	font.image.rgba[((('A' / 16) * 19 + 14) * 1024 + ('A' % 16) * 64 + 1) *
			4 +
		3] = 255;
	CHECK(mh_name_atlas_create(&font, profiles, 8, 255, &atlas, NULL) ==
		MH_ERROR_NONE);
	CHECK(atlas.image.rgba[(255U * 256U + 254) * 4] == 255);
	CHECK(atlas.labels[7].uv[3] == 255 / 256.0f);
	mh_name_atlas_free(&atlas);
	memset(profiles[0].nick, 'A', 16);
	CHECK(mh_name_atlas_create(&font, profiles, 8, 1, &atlas, NULL) !=
		MH_ERROR_NONE);

	/* Inactive profiles are not read or packed. */
	CHECK(mh_name_atlas_create(&font, profiles, 8, 254, &atlas, NULL) ==
		MH_ERROR_NONE);
	CHECK(atlas.labels[0].size[0] == 0 &&
		atlas.labels[1].uv[1] == 129 / 256.0f);
	mh_name_atlas_free(&atlas);

	for (i = 0; i < MH_NAME_LABELS_MAX; i++) {
		strcpy(profiles[i].nick, "AA");
		profiles[i].colors[0] = 0xffffffUL;
		positions[i * 3] = positions[i * 3 + 1] = 0;
		positions[i * 3 + 2] = 10;
		radii[i] = 1;
	}

	CHECK(mh_name_atlas_create(&font, profiles, PATCHED_PLAYERS,
		PATCHED_ACTIVE_MASK, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(atlas.labels[12].uv[3] <= 1 &&
		atlas.labels[0].uv[1] == 1 / 256.0f);
	CHECK(atlas.labels[12].uv[1] > atlas.labels[11].uv[3]);
	mh_name_atlas_free(&atlas);

	memset(pose, 0, sizeof(pose));
	pose[0] = pose[4] = pose[8] = 1;
	CHECK(mh_name_mesh_create(&font, profiles, 8, 255, &names, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_projection_setup(640, 480, 640 / 480.0f, 1, .5f, 120,
		&projection, NULL) == MH_ERROR_NONE);
	CHECK(mh_frustum_setup(640, 480, 1, .5f, 120, pose, &frustum, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_name_mesh_update(&names, 254, positions, radii, pose,
		&projection, &frustum, NULL) == MH_ERROR_NONE);
	CHECK(names.visible_mask == 254 &&
		names.mesh.vertices[0].projection_w == 10 &&
		names.mesh.vertices[41].projection_w == 10 &&
		names.mesh.vertices[42].projection_w == 0);
	CHECK(names.mesh.draw_count == MH_NAME_LABELS_MAX);

	for (i = 0; i < MH_NAME_LABELS_MAX; i++)
		CHECK(names.mesh.draws[i].first == i * 6 &&
			names.mesh.draws[i].count == 6);

	CHECK(mh_name_mesh_update(&names, 2, positions, radii, pose,
		&projection, &frustum, NULL) == MH_ERROR_NONE);
	CHECK(names.visible_mask == 2 &&
		names.mesh.vertices[0].projection_w == 0 &&
		names.mesh.vertices[36].projection_w == 10 &&
		names.mesh.vertices[42].projection_w == 0);
	CHECK(mh_name_mesh_update(&names, 254, positions, radii, pose,
		&projection, &frustum, NULL) == MH_ERROR_NONE);
	memcpy(vertices, names.mesh.vertices, sizeof(vertices));
	names.labels[1].uv[2] = -1;
	CHECK(mh_name_mesh_update(&names, 254, positions, radii, pose,
		&projection, &frustum, NULL) != MH_ERROR_NONE);
	CHECK(names.visible_mask == 254 &&
		memcmp(vertices, names.mesh.vertices, sizeof(vertices)) == 0);
	CHECK(mh_name_mesh_update(&names, 0, positions, radii, pose,
		&projection, &frustum, NULL) == MH_ERROR_NONE);
	memset(vertices, 0, sizeof(vertices));
	CHECK(names.visible_mask == 0 &&
		memcmp(vertices, names.mesh.vertices, sizeof(vertices)) == 0);
	mh_name_mesh_free(&names);
	memset(&shared, 0, sizeof(shared));
	memset(&page, 0, sizeof(page));
	memset(palette, 0, sizeof(palette));
	memset(palette + 255 * 3, 255, 3);
	page.width = page.height = MH_POWERVR_LAMP_ATLAS_SIZE;
	page.rgba =
		(unsigned char *)calloc((size_t)page.width * page.height, 4);
	CHECK(page.rgba != NULL);
	shared.pages = &page;
	shared.count = 1;

	for (i = 0; i < MH_NAME_LABELS_MAX; i++)
		profiles[i].colors[0] = 0xff8040UL;

	CHECK(mh_powervr_name_mesh_create(&font, profiles, MH_NAME_LABELS_MAX,
		PATCHED_ACTIVE_MASK, &shared, palette, &names,
		NULL) == MH_ERROR_NONE);
	CHECK(names.profile == MH_RENDER_POWERVR &&
		mh_mesh_texture(&names.mesh, 0) == &page &&
		names.mesh.textures[0].rgba == NULL);
	CHECK(mh_name_mesh_update(&names, PATCHED_ACTIVE_MASK, positions, radii,
		pose, &projection, &frustum, NULL) == MH_ERROR_NONE);
	CHECK(names.visible_mask == PATCHED_ACTIVE_MASK &&
		names.mesh.vertices[0].position[2] == .5f &&
		names.mesh.vertices[0].projection_w == 10);
	CHECK(names.labels[8].uv[0] == 129 / 256.0f &&
		names.labels[0].uv[0] == 1 / 256.0f);
	CHECK(names.mesh.material_rgb[0] == 160 &&
		names.mesh.material_rgb[1] > 80 &&
		names.mesh.material_rgb[1] < 81);
	memcpy(vertices, names.mesh.vertices, sizeof(vertices));
	memcpy(colors, names.mesh.material_rgb, sizeof(colors));
	names.labels[0].uv[2] = -1;
	CHECK(mh_name_mesh_update(&names, PATCHED_ACTIVE_MASK, positions, radii,
		pose, &projection, &frustum, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(vertices, names.mesh.vertices, sizeof(vertices)) == 0 &&
		memcmp(colors, names.mesh.material_rgb, sizeof(colors)) == 0);
	CHECK(mh_name_mesh_update(&names, 0, positions, radii, pose,
		&projection, &frustum, NULL) == MH_ERROR_NONE);
	memset(vertices, 0, sizeof(vertices));
	memset(colors, 0, sizeof(colors));
	CHECK(names.visible_mask == 0 &&
		memcmp(vertices, names.mesh.vertices, sizeof(vertices)) == 0 &&
		memcmp(colors, names.mesh.material_rgb, sizeof(colors)) == 0);
	mh_name_mesh_free(&names);

	/* The shared page outlives its borrowed mesh binding. */
	CHECK(page.rgba[(129U * 256U + 2) * 4 + 3] == 0);
	free(page.rgba);
	free(font.image.rgba);

	return 0;
}

static int
powervr_atlas_updates(void)
{
	struct mh_name_images names;
	struct mh_image atlas;
	struct mh_name_label labels[MH_NAME_LABELS_MAX],
		saved[MH_NAME_LABELS_MAX];
	unsigned char palette[768], indices[16], *snapshot;
	size_t size, half, i;

	memset(&names, 0, sizeof(names));
	memset(&atlas, 0, sizeof(atlas));
	memset(labels, 0, sizeof(labels));
	memset(palette, 0, sizeof(palette));
	memset(palette + 255 * 3, 255, 3);
	memset(indices, 1, sizeof(indices));
	atlas.width = atlas.height = MH_POWERVR_LAMP_ATLAS_SIZE;
	size = (size_t)atlas.width * atlas.height * 4;
	half = size / 2;
	atlas.rgba = (unsigned char *)malloc(size);
	snapshot = (unsigned char *)malloc(size);
	CHECK(atlas.rgba != NULL && snapshot != NULL);
	memset(atlas.rgba, 37, size);
	names.count = MH_NAME_LABELS_MAX;
	names.active_mask = (1U << 0) | (1U << 8);
	names.images[0].width = names.images[0].height = 4;
	names.images[0].size = sizeof(indices);
	names.images[0].indices = indices;
	names.images[8] = names.images[0];
	labels[8].height = 2;
	labels[8].color[0] = 123;
	CHECK(mh_powervr_lamp_atlas_names(
		&names, palette, &atlas, labels, NULL) == MH_ERROR_NONE);
	CHECK(atlas.rgba[half] == 255 && atlas.rgba[half + 128 * 4] == 255);
	CHECK(labels[0].uv[0] == 1 / 256.0f && labels[8].uv[0] == 129 / 256.0f);
	memcpy(snapshot, atlas.rgba, size);
	memcpy(saved, labels, sizeof(saved));
	names.images[8].width = 129;
	CHECK(mh_powervr_lamp_atlas_names(
		&names, palette, &atlas, labels, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(snapshot, atlas.rgba, size) == 0 &&
		memcmp(saved, labels, sizeof(saved)) == 0);
	names.active_mask = 1;
	CHECK(mh_powervr_lamp_atlas_names(
		&names, palette, &atlas, labels, NULL) == MH_ERROR_NONE);
	CHECK(atlas.rgba[half] == 255 && atlas.rgba[half + 128 * 4] == 0);
	CHECK(labels[8].uv[0] == 0 && labels[8].height == 2 &&
		labels[8].color[0] == 123);
	names.active_mask = 0;
	CHECK(mh_powervr_lamp_atlas_names(
		&names, palette, &atlas, labels, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(snapshot, atlas.rgba, half) == 0);

	for (i = half; i < size; i++)
		CHECK(atlas.rgba[i] == 0);

	free(snapshot);
	free(atlas.rgba);

	return 0;
}

int
main(void)
{
	struct mh_projection projection;
	struct mh_name_label label;
	struct mh_name_quad result, saved;
	float view[12], position[3];

	CHECK(atlas_mesh_contracts() == 0);
	CHECK(powervr_atlas_updates() == 0);
	memset(&label, 0, sizeof(label));
	memset(view, 0, sizeof(view));
	view[0] = view[4] = view[8] = 1;
	position[0] = position[1] = 0;
	position[2] = 10;
	label.color[0] = 255;
	label.color[1] = 128;
	label.height = 2;
	label.size[0] = label.size[1] = .8f;
	label.uv[0] = 1 / 256.0f;
	label.uv[1] = 129 / 256.0f;
	label.uv[2] = 65 / 256.0f;
	label.uv[3] = 142 / 256.0f;
	CHECK(mh_projection_setup(640, 480, 640 / 480.0f, 1, .5f, 300,
		&projection, NULL) == MH_ERROR_NONE);
	CHECK(mh_name_label_project(&label, position, position, view,
		&projection, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.visible && result.quad.color[0] == 160 &&
		result.quad.color[2] == 0);
	CHECK(result.quad.xy[0][0] == 271.25f &&
		result.quad.xy[1][0] == 368.75f);
	CHECK(mh_patched_name_label_project(&label, position, position, view,
		&projection, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.visible && result.quad.xy[0][0] == 272 &&
		result.quad.xy[1][0] == 368);
	saved = result;
	label.uv[0] += .5f;
	label.uv[2] += .5f;
	CHECK(mh_patched_name_label_project(&label, position, position, view,
		&projection, &result, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(result.quad.xy, saved.quad.xy, sizeof(result.quad.xy)) ==
		0);
	label.uv[0] -= .5f;
	label.uv[2] -= .5f;
	saved = result;
	label.uv[2] = 0;
	CHECK(mh_name_label_project(&label, position, position, view,
		&projection, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &result, sizeof(result)) == 0);
	label.uv[2] = 65 / 256.0f;
	position[2] = 51;
	CHECK(mh_name_label_project(&label, position, position, view,
		&projection, &result, NULL) == MH_ERROR_NONE &&
		!result.visible);
	position[2] = 10;
	label.size[0] = .009f;
	CHECK(mh_name_label_project(&label, position, position, view,
		&projection, &result, NULL) == MH_ERROR_NONE &&
		!result.visible);
	label.size[0] = .8f;
	position[0] = 1e10f;
	saved = result;
	CHECK(mh_name_label_project(&label, position, position, view,
		&projection, &result, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &result, sizeof(result)) == 0);
	puts("Name projection, distance rejection, minimum size and invalid-input preservation pass");

	return 0;
}
