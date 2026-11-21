#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static int
capture_matches(const char *path, const double expected[3])
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int channel;
	int ok;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	pixel = (const unsigned char *)rgba->pixels + 120 * rgba->pitch +
		160 * 4;
	ok = 1;

	for (channel = 0; channel < 3; channel++)
		if (fabs(pixel[channel] - expected[channel]) > 1.1) {
			fprintf(stderr, "channel %u: %u expected %.5f\n",
				channel, pixel[channel], expected[channel]);
			ok = 0;
		}

	SDL_DestroySurface(rgba);

	return ok;
}

int
main(int argc, char *argv[])
{
	static const float xy[3][2] = {{-.8f, -.8f}, {.8f, -.8f}, {0, .8f}};
	static const float depths[3] = {32, 96, 210};

	/*
	 * Density 10, SDK default .01 InvZ scale, table
	 * index=floor(2^21*.01/Z).
	 */
	static const double fog_amount[3] = {
		7.0 / 256, 78.0 / 256, 149.0 / 256};

	static const double cue[3] = {32, 64, 128};
	static const double clear[3] = {.055 * 255, .070 * 255, .095 * 255};
	static const float color[3] = {180, 100, 60};
	static const double sampled[3][3] = {
		{255, 255, 255}, {132, 198, 255}, {136, 204, 255}};

	unsigned char pixels[3][4] = {{255, 255, 255, 255},
		{128, 192, 255, 255}, {128, 192, 255, 136}};

	unsigned char pages[2][32 * 64 * 4];
	unsigned char formats[3] = {0, MH_TEXTURE_RGB555, MH_TEXTURE_ARGB4444};

	struct mh_mesh mesh;
	const struct mh_mesh *list[2];
	struct mh_vertex vertices[3];
	struct mh_mesh_primitive primitive;
	struct mh_draw draw;
	struct mh_image textures[3];
	struct mh_projection projection;
	struct mh_fog fog;
	struct mh_gpu_renderer *gpu;
	float raw[9], depth;
	double expected[3], alpha, amount, background;
	unsigned int i, pixel, c, test, material, distance;
	int ok, fog_on;

	if (argc != 2)
		return 2;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL video: %s\n", SDL_GetError());
		return 1;
	}

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&primitive, 0, sizeof(primitive));
	memset(&draw, 0, sizeof(draw));

	for (i = 0; i < 3; i++) {
		textures[i].width = i == 0 ? 1 : 32;
		textures[i].height = i == 0 ? 1 : 64;
		textures[i].rgba = i == 0 ? pixels[0] : pages[i - 1];

		if (i != 0)
			for (pixel = 0; pixel < 32 * 64; pixel++)
				memcpy(pages[i - 1] + pixel * 4, pixels[i], 4);
	}

	mesh.vertices = vertices;
	mesh.vertex_count = 3;
	mesh.material_rgb = raw;
	mesh.material_rgb_count = 3;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.primitives = &primitive;
	mesh.primitive_count = 1;
	mesh.textures = textures;
	mesh.texture_count = 3;
	mesh.texture_formats = formats;
	mesh.texture_format_count = 3;
	draw.count = primitive.count = 3;
	primitive.type = 4;
	draw.prelit = 1;
	list[0] = &mesh;
	list[1] = &mesh;
	memset(raw, 0, sizeof(raw));
	gpu = mh_gpu_powervr_open(NULL, list, 2, 320, 240, 0);
	ok = gpu != NULL && mh_gpu_renderer_mesh_visible(gpu, 1, 0) &&
		gpu->image_count == 3 &&
		gpu->meshes[0].images[1] == gpu->meshes[1].images[1] &&
		gpu->meshes[0].images[2] == gpu->meshes[1].images[2] &&
		gpu->images[1].width == 64 && gpu->images[1].height == 64 &&
		gpu->images[1].levels == 1 && gpu->images[2].levels == 1 &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 300,
			&projection, NULL) == MH_ERROR_NONE &&
		mh_fog_configure(cue, 300, &fog, NULL) == MH_ERROR_NONE;

	for (test = 0; test < 27 && ok; test++) {
		material = test % 3;
		distance = test >= 24 ? 0 : (test / 3) % 3;
		fog_on = test >= 9 && test < 18;
		depth = depths[distance];
		draw.texture = material;
		draw.alpha_test = material == 2;

		for (i = 0; i < 3; i++) {
			vertices[i].position[0] = xy[i][0] * depth;
			vertices[i].position[1] = xy[i][1] * depth;
			vertices[i].position[2] = -depth;
			vertices[i].uv[0] = vertices[i].uv[1] = .5f;

			/*
			 * Padded half, then repeat back into the authored half.
			 */
			if (test >= 18)
				vertices[i].uv[0] = test < 21 ? 1.5f : 2.5f;

			if (test >= 24)
				vertices[i].uv[0] = 1.0f;

			for (c = 0; c < 3; c++)
				raw[i * 3 + c] = color[c] + .875f;
		}

		/* Alpha 136 represents inverted ARGB4444 transparency 7. */
		alpha = material == 2 ? 7 : 0;

		if (distance == 2)
			alpha += 5;

		alpha = 1 - alpha / 16;

		/*
		 * Bilinear alpha halfway into transparent padding gives
		 * nibble 11.
		 */
		if (test >= 24 && material == 2)
			alpha = 5.0 / 16;

		amount = fog_on ? fog_amount[distance] : 0;

		for (c = 0; c < 3; c++) {
			background = fog_on ? cue[c] : clear[c];
			expected[c] = color[c] * sampled[material][c] / 255.0;

			if (test >= 24 && material != 0)
				expected[c] *= .5;

			if (test >= 18 && test < 21 && material != 0) {
				expected[c] = 0;

				if (material == 2)
					alpha = 0;
			}

			expected[c] += (cue[c] - 1 - expected[c]) * amount;
			expected[c] =
				expected[c] * alpha + background * (1 - alpha);
		}

		ok = mh_gpu_renderer_set_fog(gpu, fog_on ? &fog : NULL) &&
			mh_gpu_renderer_mesh_lit(gpu, 0, vertices) &&
			mh_gpu_renderer_scene_frame_lens(gpu, &projection,
				identity, NULL, 0, NULL, NULL) &&
			mh_gpu_renderer_capture(gpu, argv[1]) &&
			capture_matches(argv[1], expected);

		if (!ok)
			fprintf(stderr, "PowerVR frame %u failed: %s\n", test,
				SDL_GetError());
	}

	/* Uploads must leave borrowed decoded pixels and dimensions intact. */
	for (i = 0; i < 2 && ok; i++) {
		ok = textures[i + 1].width == 32 &&
			textures[i + 1].height == 64;

		for (pixel = 0; pixel < 32 * 64 && ok; pixel++)
			ok = memcmp(pages[i] + pixel * 4, pixels[i + 1], 4) ==
				0;
	}

	mh_gpu_renderer_destroy(gpu);
	SDL_Quit();

	if (ok)
		puts("PowerVR: 27 GPU frames pass texture precision/padding/repeat/bilinear alpha, shared immutable uploads, ordinary/distant colors, transparency and fog");

	return ok ? 0 : 1;
}
