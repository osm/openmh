#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/error.h"

static int
draw_frame(struct mh_renderer *renderer, unsigned int mode,
	const float matrix[16], const float view[16],
	const struct mh_projection *projection)
{
	return mode >= 4
		? mh_renderer_tron_scene_ui(
			renderer, projection, view, NULL, 0, NULL)
		: mh_renderer_scene_frame(renderer, matrix, view, NULL, 0);
}

static SDL_Surface *
read_capture(struct mh_renderer *renderer, const char *prefix,
	unsigned int mode, unsigned int mask, unsigned int hidden)
{
	char path[1024], suffix[40];
	size_t length, suffix_length;
	SDL_Surface *bmp, *rgba;

	sprintf(suffix, "-%u-%u-%u.bmp", mode, mask, hidden);
	length = strlen(prefix);
	suffix_length = strlen(suffix);

	if (length > sizeof(path) - 1 - suffix_length)
		return NULL;

	memcpy(path, prefix, length);
	memcpy(path + length, suffix, suffix_length + 1);

	if (!mh_renderer_capture(renderer, path))
		return NULL;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return NULL;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	return rgba;
}

int
main(int argc, char *argv[])
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	static const double fog_color[3] = {32, 64, 96};
	struct mh_vertex vertices[12], sky_vertices[3];
	struct mh_mesh mesh, sky;
	const struct mh_mesh *meshes[2];
	struct mh_draw draws[2], sky_draw;
	struct mh_mesh_primitive primitives[2], reference_primitives[2];
	struct mh_image white;
	struct mh_projection projection;
	struct mh_fog fog;
	struct mh_renderer *renderer;
	SDL_Surface *reference, *masked;
	unsigned char pixel[4] = {255, 255, 255, 255}, hidden[2];
	float matrix[16], view[16];
	unsigned int mode, mask, i, j, corner;
	int success, y, different;
	const unsigned char *left, *right;

	if (argc != 2 || strlen(argv[1]) > 900)
		return 2;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(draws, 0, sizeof(draws));
	memset(&sky, 0, sizeof(sky));
	memset(&sky_draw, 0, sizeof(sky_draw));
	memset(sky_vertices, 0, sizeof(sky_vertices));
	memset(primitives, 0, sizeof(primitives));
	memset(matrix, 0, sizeof(matrix));
	matrix[0] = .5f;
	matrix[5] = .75f;
	matrix[10] = matrix[14] = -.5f;
	matrix[15] = 1;
	mesh.vertices = vertices;
	mesh.vertex_count = 12;
	mesh.draws = draws;
	mesh.draw_count = 2;
	white.rgba = pixel;
	white.width = white.height = 1;
	mesh.textures = &white;
	mesh.texture_count = 1;
	mesh.draw_hidden = hidden;
	mesh.primitives = primitives;
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

	for (i = 0; i < 2; i++) {
		draws[i].first = i * 6;
		draws[i].count = 6;
		draws[i].prelit = 1;
		primitives[i].first = i * 6;
		primitives[i].count = 6;
		primitives[i].draw = i;
		primitives[i].type = 4;

		for (j = 0; j < 6; j++) {
			corner = corners[j];
			vertices[i * 6 + j].position[0] = (i ? .2f : -1.6f) +
				(corner == 1 || corner == 2 ? 1.4f : 0);
			vertices[i * 6 + j].position[1] =
				corner >= 2 ? -.7f : .7f;
			vertices[i * 6 + j].position[2] = -2;
			vertices[i * 6 + j].color[0] =
				vertices[i * 6 + j].color[1] =
					vertices[i * 6 + j].color[2] = .5f;
		}
	}

	if (mh_projection_setup(960, 640, 1.5f, 1, .5f, 60, &projection,
		NULL) != MH_ERROR_NONE ||
		mh_fog_configure(fog_color, 60, &fog, NULL) != MH_ERROR_NONE)
		return 1;

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "%s\n", SDL_GetError());
		return 1;
	}

	memset(hidden, 0, sizeof(hidden));
	meshes[0] = &mesh;
	meshes[1] = &sky;
	renderer = mh_sdl_renderer_create_scene(NULL, meshes, 2);
	success = renderer != NULL;

	if (success)
		success = mh_renderer_mesh_sky(renderer, 1);

	for (mode = 0; mode < 6 && success; mode++) {
		memset(view, 0, sizeof(view));
		view[0] = view[5] = view[15] = 1;
		view[10] = mode == 2 ? 25 : 1;
		draws[0].alpha_test = draws[1].alpha_test = mode == 1;
		success = mh_renderer_update_mesh(renderer, 0) &&
			mh_renderer_set_fog(
				renderer, mode == 2 ? &fog : NULL) &&
			mh_renderer_mesh_visible(renderer, 1, mode == 2);

		if (success && (mode == 3 || mode == 5))
			success = mh_renderer_mesh_unavailable(
				renderer, 0, vertices);

		for (mask = 0; mask < 4 && success; mask++) {
			/*
			 * Reference removes complete draws. The actual frame
			 * retains every range/vertex and changes only the
			 * visibility bytes.
			 */
			for (i = 0; i < 2; i++) {
				hidden[i] = 0;
				draws[i].count = mask & (1U << i) ? 0 : 6;
			}

			mesh.primitive_count = 0;
			mesh.primitives = reference_primitives;

			for (i = 0; i < 2; i++)
				if (draws[i].count != 0)
					reference_primitives[mesh
							.primitive_count++] =
						primitives[i];

			success = draw_frame(
				renderer, mode, matrix, view, &projection);
			reference = success
				? read_capture(renderer, argv[1], mode, mask, 0)
				: NULL;

			for (i = 0; i < 2; i++) {
				draws[i].count = 6;
				hidden[i] = (unsigned char)((mask >> i) & 1U);
			}

			mesh.primitive_count = 2;
			mesh.primitives = primitives;

			if (success)
				success = draw_frame(renderer, mode, matrix,
					view, &projection);

			masked = success
				? read_capture(renderer, argv[1], mode, mask, 1)
				: NULL;

			success =
				success && reference != NULL && masked != NULL;
			different = 0;

			for (y = 0; success && y < reference->h; y++) {
				left = (const unsigned char *)
						reference->pixels +
					y * reference->pitch;

				right = (const unsigned char *)masked->pixels +
					y * masked->pitch;

				if (memcmp(left, right,
					(size_t)reference->w * 4) != 0)
					success = 0;

				if (memcmp(left + 480 * 4, left + 240 * 4, 3) !=
						0 ||
					memcmp(left + 480 * 4, left + 720 * 4,
						3) != 0)
					different = 1;
			}

			if (mask != 3 && !different)
				success = 0;

			SDL_DestroySurface(reference);
			SDL_DestroySurface(masked);

			if (!success)
				fprintf(stderr,
					"mode %u mask %u: visibility pixels differ or fixture is empty\n",
					mode, mask);
		}
	}

	if (!success)
		fprintf(stderr, "GPU: %s\n", SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();

	if (success)
		puts("24 object visibility frames match omitted draws across ordinary, alpha, distant, unavailable and TRON passes");

	return !success;
}
