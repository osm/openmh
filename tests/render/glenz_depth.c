#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

enum {
	MESH_COUNT = 3,
	VERTEX_COUNT = 6,
	POSE_COUNT = 8
};

/* Deliberately closer than a guessed legacy 16/65535 depth offset. */
#define DEPTH_GAP (1.0f / 131072.0f)

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static int
check_capture(const char *path, unsigned int front)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int side, x, y, channel, expected;
	int success;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	success = 1;

	/*
	 * Interior patches avoid coverage edges while crossing both triangles.
	 */
	for (side = 0; side < 2 && success; side++) {
		for (y = 105; y < 135 && success; y++) {
			for (x = 110 + side * 80;
				x < 130 + side * 80 && success; x++) {
				pixel = (const unsigned char *)rgba->pixels +
					y * rgba->pitch + x * 4;

				for (channel = 0; channel < 3; channel++) {
					expected = side
						? (front == 0 ? 70 : 35)
						: (channel == 1 ? 255 : 0);

					if (fabs((double)pixel[channel] -
						expected) > 1.1) {
						fprintf(stderr,
							"depth pixel %u,%u channel %u: %u expected %u\n",
							x, y, channel,
							pixel[channel],
							expected);
						success = 0;
						break;
					}
				}
			}
		}
	}

	SDL_DestroySurface(rgba);

	return success;
}

static void
set_transforms(float transforms[MESH_COUNT][16], float depth, int perspective,
	unsigned int front, unsigned int pose)
{
	float angle, wx, wy, offset;
	unsigned int mesh;

	angle = (float)pose * .02f;
	wx = perspective ? .35f : 0;
	wy = perspective ? -.25f : 0;

	for (mesh = 0; mesh < MESH_COUNT; mesh++) {
		memcpy(transforms[mesh], identity, sizeof(identity));
		offset = mesh == 2 ? -DEPTH_GAP : mesh == front ? 0 : DEPTH_GAP;
		transforms[mesh][0] = (float)cos(angle);
		transforms[mesh][1] = (float)sin(angle);
		transforms[mesh][4] = -(float)sin(angle);
		transforms[mesh][5] = (float)cos(angle);
		transforms[mesh][2] = depth * wx + .02f;
		transforms[mesh][6] = depth * wy - .01f;
		transforms[mesh][3] = wx;
		transforms[mesh][7] = wy;
		transforms[mesh][12] = .002f * (float)pose;
		transforms[mesh][14] = depth + offset;
	}
}

static int
check_depth_frames(struct mh_renderer *renderer, const char *path)
{
	static const float depths[3] = {.05f, .5f, .95f};
	float transforms[MESH_COUNT][16];
	unsigned int scenario, depth, perspective, front, pose;

	/* Each depth tests both projections and both mesh submission orders. */
	for (scenario = 0; scenario < 12; scenario++) {
		depth = scenario / 4;
		perspective = (scenario / 2) % 2;
		front = scenario % 2;

		for (pose = 0; pose < POSE_COUNT; pose++) {
			set_transforms(transforms, depths[depth], perspective,
				front, pose);

			if (!mh_renderer_scene_frame(renderer, identity,
				identity, transforms[0], 1) ||
				!mh_renderer_capture(renderer, path) ||
				!check_capture(path, front)) {
				fprintf(stderr,
					"Glenz depth scenario %u pose %u failed: %s\n",
					scenario, pose, SDL_GetError());
				return 0;
			}
		}
	}

	return 1;
}

int
main(int argc, char *argv[])
{
	static const float xy[VERTEX_COUNT][2] = {{-.8f, -.8f}, {.8f, -.8f},
		{.8f, .8f}, {-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	unsigned char texels[3][4] = {
		{255, 255, 255, 255}, {80, 100, 120, 255}, {60, 40, 20, 255}};

	struct mh_image images[3];
	struct mh_vertex vertices[MESH_COUNT][VERTEX_COUNT];
	struct mh_mesh meshes[MESH_COUNT];
	struct mh_draw draws[MESH_COUNT];
	const struct mh_mesh *inputs[MESH_COUNT];
	struct mh_glenz_coordinates coordinates[VERTEX_COUNT];
	struct mh_renderer_glenz material;
	struct mh_renderer *renderer;
	unsigned int mesh, vertex, channel;
	int success;

	if (argc != 2)
		return 2;

	memset(images, 0, sizeof(images));
	memset(vertices, 0, sizeof(vertices));
	memset(meshes, 0, sizeof(meshes));
	memset(draws, 0, sizeof(draws));
	memset(coordinates, 0, sizeof(coordinates));

	for (mesh = 0; mesh < MESH_COUNT; mesh++) {
		images[mesh].rgba = texels[mesh];
		images[mesh].width = 1;
		images[mesh].height = 1;
		meshes[mesh].vertices = vertices[mesh];
		meshes[mesh].vertex_count = VERTEX_COUNT;
		meshes[mesh].draws = &draws[mesh];
		meshes[mesh].draw_count = 1;
		meshes[mesh].textures = images;
		meshes[mesh].texture_count = 3;
		meshes[mesh].environment_texture = 1;
		inputs[mesh] = &meshes[mesh];
		draws[mesh].count = VERTEX_COUNT;
		draws[mesh].prelit = 1;
		draws[mesh].environment = mesh != 2;

		for (vertex = 0; vertex < VERTEX_COUNT; vertex++) {
			vertices[mesh][vertex].position[0] = mesh == 2
				? xy[vertex][0] * .5f - .4f
				: xy[vertex][0];
			vertices[mesh][vertex].position[1] = xy[vertex][1];

			for (channel = 0; channel < 3; channel++)
				vertices[mesh][vertex].color[channel] =
					mesh == 2 ? (channel == 1 ? 1 : 0)
						: (mesh == 0 ? .5f : .25f);
		}
	}

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	renderer = mh_sdl_renderer_create_scene_sized(
		NULL, inputs, MESH_COUNT, 320, 240);
	success = renderer != NULL;
	material.coordinates = coordinates;
	material.count = VERTEX_COUNT;
	material.reflection_texture = 1;
	material.phong_texture = 2;

	for (mesh = 0; mesh < 2 && success; mesh++)
		success = mh_renderer_mesh_glenz(renderer, mesh, &material,
				NULL) == MH_ERROR_NONE;

	if (success)
		success = check_depth_frames(renderer, argv[1]);

	if (!success)
		fprintf(stderr, "Glenz depth: %s\n", SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();

	if (success)
		puts("Glenz depth: 96 moving/sloped frames passed (near/mid/far, perspective, both overlap orders, close occlusion)");

	return success ? 0 : 1;
}
