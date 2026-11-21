#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/error.h"

static SDL_Surface *
read_capture(
	struct mh_renderer *renderer, const char *prefix, const char *suffix)
{
	char path[1024];
	size_t prefix_length, suffix_length;
	SDL_Surface *bmp, *rgba;

	prefix_length = strlen(prefix);
	suffix_length = strlen(suffix);

	if (prefix_length > sizeof(path) - 1 - suffix_length)
		return NULL;

	memcpy(path, prefix, prefix_length);
	memcpy(path + prefix_length, suffix, suffix_length + 1);

	if (!mh_renderer_capture(renderer, path))
		return NULL;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return NULL;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	return rgba;
}

static int
surfaces_match(const SDL_Surface *first, const SDL_Surface *second)
{
	int y;

	if (first == NULL || second == NULL || first->w != second->w ||
		first->h != second->h)
		return 0;

	for (y = 0; y < first->h; y++)
		if (memcmp((const unsigned char *)first->pixels +
				y * first->pitch,
			(const unsigned char *)second->pixels +
				y * second->pitch,
			(size_t)first->w * 4) != 0)
			return 0;

	return 1;
}

static int
surface_nonempty(const SDL_Surface *surface)
{
	const unsigned char *pixels;
	unsigned int x, y;
	int nonempty;

	pixels = (const unsigned char *)surface->pixels;
	nonempty = 0;

	for (y = 0; y < 240 && !nonempty; y++)
		for (x = 0; x < 320; x++)
			if (memcmp(pixels + y * surface->pitch + x * 4, pixels,
				3) != 0) {
				nonempty = 1;
				break;
			}

	return nonempty;
}

static int
draw_frame(struct mh_renderer *renderer, unsigned int mode,
	const float matrix[16], const float view[16], const float *models,
	const struct mh_projection *projection)
{
	return mode >= 4 && mode <= 6
		? mh_renderer_tron_scene_ui(
			renderer, projection, view, models, 0, NULL)
		: mh_renderer_scene_frame(renderer, matrix, view, models, 0);
}

static int
select_effect(struct mh_renderer *renderer, unsigned int index,
	unsigned int mode, const struct mh_vertex *vertices)
{
	if (!mh_renderer_update_mesh(renderer, index))
		return 0;

	if (mode == 3 || mode == 6)
		return mh_renderer_mesh_unavailable(renderer, index, vertices);

	if (mode == 7)
		return mh_renderer_mesh_sky(renderer, index);

	if (mode == 8)
		return mh_renderer_mesh_halos(renderer, index);

	return 1;
}

int
main(int argc, char *argv[])
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	static const unsigned int orders[6][3] = {{0, 1, 2}, {0, 2, 1},
		{1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};

	static const float colors[4][3] = {{.2f, .1f, .05f}, {.05f, .45f, .1f},
		{.1f, .1f, .8f}, {.7f, .2f, .2f}};

	static const double fog_color[3] = {32, 64, 96};
	struct mh_vertex vertices[24], reference_vertices[18], sky_vertices[3];
	struct mh_draw draws[4], reference_draws[3], sky_draw;
	struct mh_mesh_primitive primitives[4], reference_primitives[3];
	struct mh_mesh meshes[2], reference_mesh, sky;
	const struct mh_mesh *inputs[3];
	struct mh_renderer_scene_part parts[4], bad[4];
	struct mh_renderer *renderer, *reference_renderer;
	struct mh_image white;
	struct mh_projection projection;
	struct mh_fog fog;
	SDL_Surface *actual, *reference, *baseline;
	unsigned char pixel[4] = {255, 255, 255, 255};
	float matrix[16], view[16], transforms[48];
	unsigned int i, j, n, mode, order, lod, source;
	int success, changed;

	if (argc != 2 || strlen(argv[1]) > 900)
		return 2;

	memset(meshes, 0, sizeof(meshes));
	memset(&reference_mesh, 0, sizeof(reference_mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(draws, 0, sizeof(draws));
	memset(primitives, 0, sizeof(primitives));
	memset(parts, 0, sizeof(parts));
	memset(transforms, 0, sizeof(transforms));

	for (i = 0; i < 2; i++)
		for (j = 0; j < 4; j++)
			transforms[i * 16 + j * 5] = 1;

	transforms[28] = .125f;
	white.width = white.height = 1;
	white.rgba = pixel;
	memset(&sky, 0, sizeof(sky));
	memset(&sky_draw, 0, sizeof(sky_draw));
	memset(sky_vertices, 0, sizeof(sky_vertices));
	sky.vertices = sky_vertices;
	sky.vertex_count = 3;
	sky.draws = &sky_draw;
	sky.draw_count = 1;
	sky.textures = &white;
	sky.texture_count = 1;
	sky_draw.count = 3;
	sky_draw.prelit = 1;

	for (i = 0; i < 3; i++) {
		sky_vertices[i].position[0] = i == 1 ? 3 : -1;
		sky_vertices[i].position[1] = i == 2 ? 3 : -1;

		for (j = 0; j < 3; j++)
			sky_vertices[i].color[j] = .4f;
	}

	for (i = 0; i < 4; i++) {
		draws[i].first = (i % 2) * 6;
		draws[i].count = 6;
		draws[i].prelit = 1;
		primitives[i].first = (i % 2) * 6;
		primitives[i].count = 6;
		primitives[i].draw = i % 2;
		primitives[i].type = 4;

		for (j = 0; j < 6; j++) {
			n = corners[j];
			vertices[i * 6 + j].position[0] =
				n == 1 || n == 2 ? .8f : -.8f;
			vertices[i * 6 + j].position[1] = n >= 2 ? -.5f : .5f;
			vertices[i * 6 + j].position[2] = -2;
			memcpy(vertices[i * 6 + j].color, colors[i],
				sizeof(colors[i]));
		}
	}

	for (i = 0; i < 2; i++) {
		meshes[i].vertices = vertices + i * 12;
		meshes[i].vertex_count = 12;
		meshes[i].draws = draws + i * 2;
		meshes[i].draw_count = 2;
		meshes[i].primitives = primitives + i * 2;
		meshes[i].primitive_count = 2;
		meshes[i].textures = &white;
		meshes[i].texture_count = 1;
		inputs[i] = &meshes[i];
		meshes[1].lods[i].vertex_first = i * 6;
		meshes[1].lods[i].vertex_count = 6;
		meshes[1].lods[i].draw_first =
			meshes[1].lods[i].primitive_first = i;
		meshes[1].lods[i].draw_count =
			meshes[1].lods[i].primitive_count = 1;
	}

	meshes[1].lod_count = 2;
	reference_mesh.vertices = reference_vertices;
	reference_mesh.vertex_count = 18;
	reference_mesh.draws = reference_draws;
	reference_mesh.draw_count = 3;
	reference_mesh.primitives = reference_primitives;
	reference_mesh.primitive_count = 3;
	reference_mesh.textures = &white;
	reference_mesh.texture_count = 1;
	memset(reference_vertices, 0, sizeof(reference_vertices));

	for (i = 0; i < 3; i++) {
		reference_draws[i] = draws[0];
		reference_draws[i].first = i * 6;
		reference_primitives[i] = primitives[0];
		reference_primitives[i].first = i * 6;
		reference_primitives[i].draw = i;
	}

	memset(matrix, 0, sizeof(matrix));
	matrix[0] = .5f;
	matrix[5] = .75f;
	matrix[10] = matrix[14] = -.5f;
	matrix[15] = 1;

	if (mh_projection_setup(320, 240, 1.5f, 1, .5f, 60, &projection,
		NULL) != MH_ERROR_NONE ||
		mh_fog_configure(fog_color, 60, &fog, NULL) != MH_ERROR_NONE)
		return 1;

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	inputs[2] = &sky;
	renderer =
		mh_sdl_renderer_create_scene_sized(NULL, inputs, 3, 320, 240);
	inputs[0] = &reference_mesh;
	inputs[1] = &sky;
	reference_renderer =
		mh_sdl_renderer_create_scene_sized(NULL, inputs, 2, 320, 240);
	success = renderer != NULL && reference_renderer != NULL;

	if (success)
		success = mh_renderer_mesh_sky(renderer, 2) &&
			mh_renderer_mesh_sky(reference_renderer, 1);

	for (mode = 0; success && mode < 9; mode++) {
		memset(view, 0, sizeof(view));
		view[0] = view[5] = view[15] = 1;
		view[10] = mode == 2 ? 25 : 1;

		for (i = 0; i < 4; i++)
			draws[i].alpha_test = mode == 1 || mode == 5;

		for (i = 0; i < 24; i++)
			vertices[i].position[2] = mode >= 7 ? .5f : -2;

		success = mh_renderer_set_fog(
				renderer, mode == 2 ? &fog : NULL) &&
			mh_renderer_set_fog(
				reference_renderer, mode == 2 ? &fog : NULL);

		if (success)
			success = mh_renderer_mesh_visible(
					renderer, 2, mode == 2) &&
				mh_renderer_mesh_visible(
					reference_renderer, 1, mode == 2);

		baseline = NULL;
		changed = 0;

		for (order = 0; success && order < 6; order++) {
			memset(parts, 0, sizeof(parts));

			for (i = 0; i < 3; i++) {
				source = orders[order][i];

				if (source == 2) {
					parts[i].mesh = 1;
					parts[i].active_lod = 1;
				} else {
					parts[i].span.draw_first =
						parts[i].span.primitive_first =
							source;
					parts[i].span.draw_count =
						parts[i].span.primitive_count =
							1;
				}
			}

			parts[3].mesh = 2;
			parts[3].active_lod = 1;

			/*
			 * Model roles must not change hardware material
			 * ordering.
			 */
			parts[1].model_role = MH_RENDER_MODEL_CHILD;
			parts[2].model_role = MH_RENDER_MODEL_WORLD;
			success = mh_renderer_scene_order(renderer, parts, 4);
			memcpy(bad, parts, sizeof(bad));
			bad[1] = bad[0];

			if (success)
				success = !mh_renderer_scene_order(
						renderer, bad, 4) &&
					!mh_renderer_scene_order(
						renderer, parts, 3);

			memcpy(bad, parts, sizeof(bad));
			bad[0].mesh = 3;

			if (success)
				success = !mh_renderer_scene_order(
					renderer, bad, 4);

			memcpy(bad, parts, sizeof(bad));
			bad[0].model_role = MH_RENDER_MODEL_CHILD;

			if (success)
				success = !mh_renderer_scene_order(
					renderer, bad, 4);

			memcpy(bad, parts, sizeof(bad));
			bad[1].model_role = (enum mh_renderer_model_role) - 1;

			if (success)
				success = !mh_renderer_scene_order(
					renderer, bad, 4);

			memcpy(bad, parts, sizeof(bad));
			bad[0].model_role = MH_RENDER_MODEL_WORLD;

			if (success)
				success = !mh_renderer_scene_order(
					renderer, bad, 4);

			memcpy(bad, parts, sizeof(bad));
			bad[0].active_lod = 0;
			bad[0].span.draw_count = (size_t)-1;

			if (success)
				success = !mh_renderer_scene_order(
					renderer, bad, 4);

			/*
			 * The renderer owns its order; caller storage can be
			 * discarded.
			 */
			memset(parts, 0, sizeof(parts));

			for (lod = 0; success && lod < 2; lod++) {
				meshes[1].lod = lod;

				for (i = 0; i < 3; i++) {
					source = orders[order][i];

					if (source == 2)
						source += lod;

					memcpy(reference_vertices + i * 6,
						vertices + source * 6,
						6 * sizeof(*vertices));

					if (source >= 2 && mode < 7)
						for (j = 0; j < 6; j++)
							reference_vertices[i *
									6 +
								j]
								.position[0] +=
								.125f;

					reference_draws[i].alpha_test =
						draws[source].alpha_test;
				}

				success = select_effect(renderer, 0, mode,
						vertices) &&
					select_effect(renderer, 1, mode,
						vertices + 12) &&
					select_effect(reference_renderer, 0,
						mode, reference_vertices);

				if (success)
					success = draw_frame(renderer, mode,
							matrix, view,
							transforms,
							&projection) &&
						draw_frame(reference_renderer,
							mode, matrix, view,
							NULL, &projection);

				actual = success
					? read_capture(renderer, argv[1],
						"-actual.bmp")
					: NULL;

				reference = success
					? read_capture(reference_renderer,
						argv[1], "-reference.bmp")
					: NULL;

				success = success &&
					surfaces_match(actual, reference);

				if (success && lod == 0) {
					if (baseline == NULL) {
						baseline = reference;
						reference = NULL;
					} else if (!surfaces_match(
							baseline, reference))
						changed = 1;
				}

				if (success)
					success = surface_nonempty(actual);

				SDL_DestroySurface(actual);
				SDL_DestroySurface(reference);

				if (!success)
					fprintf(stderr,
						"scene order mode %u permutation %u LOD %u failed\n",
						mode, order, lod);
			}
		}

		if (success &&
			(mode == 0 || mode == 1 || mode == 5 || mode == 7) &&
			!changed) {
			fprintf(stderr,
				"scene order mode %u did not distinguish permutations\n",
				mode);
			success = 0;
		}

		SDL_DestroySurface(baseline);
	}

	if (success) {
		/*
		 * Reset restores mesh-array order: both fixed objects, then the
		 * currently selected car detail, without retaining the last
		 * permutation.
		 */
		for (i = 0; i < 24; i++)
			vertices[i].position[2] = -2;

		for (i = 0; i < 4; i++)
			draws[i].alpha_test = 0;

		for (i = 0; i < 3; i++) {
			source = i == 2 ? 3 : i;
			memcpy(reference_vertices + i * 6,
				vertices + source * 6, 6 * sizeof(*vertices));

			if (i == 2)
				for (j = 0; j < 6; j++)
					reference_vertices[i * 6 + j]
						.position[0] += .125f;

			reference_draws[i].alpha_test = 0;
		}

		success = mh_renderer_scene_order(renderer, NULL, 0) &&
			select_effect(renderer, 0, 0, vertices) &&
			select_effect(renderer, 1, 0, vertices + 12) &&
			select_effect(
				reference_renderer, 0, 0, reference_vertices);

		if (success)
			success = draw_frame(renderer, 0, matrix, view,
					transforms, &projection) &&
				draw_frame(reference_renderer, 0, matrix, view,
					NULL, &projection);

		actual = success
			? read_capture(renderer, argv[1], "-actual.bmp")
			: NULL;

		reference = success ? read_capture(reference_renderer, argv[1],
					"-reference.bmp")
				: NULL;

		success = success && surfaces_match(actual, reference);
		SDL_DestroySurface(actual);
		SDL_DestroySurface(reference);
	}

	if (!success)
		fprintf(stderr, "GPU: %s\n", SDL_GetError());

	mh_renderer_destroy(reference_renderer);
	mh_renderer_destroy(renderer);
	SDL_Quit();

	if (success)
		puts("108 scene order/LOD frames and order reset match physically reordered geometry across nine rendering modes");

	return !success;
}
