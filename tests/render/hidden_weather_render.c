#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/provider.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/storage/filesystem_file_read.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/asset_mesh_load.h"
#include "client/diagnostics/asset_path.h"
#include "engine/assets/mesh_load.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/render/atmosphere_sky.h"
#include "engine/storage/asset_path.h"

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static double
sample_ramp(double uv)
{
	double coordinate, fraction;
	int first, second;

	coordinate = (uv - floor(uv)) * 256 - .5;
	first = (int)floor(coordinate);
	fraction = coordinate - floor(coordinate);
	second = (first + 1 + 256) % 256;
	first = (first + 256) % 256;

	return first * (1 - fraction) + second * fraction;
}

static int
capture_matches(const char *path, const struct mh_weather_camera *camera)
{
	static const unsigned int xs[] = {13, 211, 481, 719, 941};
	static const unsigned int ys[] = {5, 39, 113, 229, 371, 450, 620};
	struct mh_weather_vertex v[8];
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int draws, i, j, k, n, indices[3];
	double s, t, weight[3], denominator, u, w, color[3], texel[3], error;
	int success;

	if (mh_weather_sky(camera, v, &draws, NULL) != MH_ERROR_NONE ||
		draws != 2)
		return 0;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	success = 1;

	for (j = 0; j < sizeof(ys) / sizeof(ys[0]); j++)
		for (i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
			s = (xs[i] + .5) / camera->width;
			t = (ys[j] + .5) / v[2].xy[1];

			if (t >= 1)
				for (k = 0; k < 3; k++)
					color[k] = (camera->bottom >>
							(16 - k * 8)) &
						255;
			else {
				if (s >= t) {
					indices[0] = 0;
					indices[1] = 1;
					indices[2] = 2;
					weight[0] = 1 - s;
					weight[1] = s - t;
					weight[2] = t;
				} else {
					indices[0] = 0;
					indices[1] = 2;
					indices[2] = 3;
					weight[0] = 1 - t;
					weight[1] = s;
					weight[2] = t - s;
				}

				denominator = u = w = 0;

				for (k = 0; k < 3; k++)
					color[k] = 0;

				for (n = 0; n < 3; n++) {
					k = indices[n];
					denominator +=
						weight[n] * v[k].reciprocal_w;
					u += weight[n] * v[k].reciprocal_w *
						v[k].uv[0];
					w += weight[n] * v[k].reciprocal_w *
						v[k].uv[1];

					for (k = 0; k < 3; k++)
						color[k] += weight[n] *
							((v[indices[n]].color >>
								(16 - k * 8)) &
								255);
				}

				texel[0] = sample_ramp(u / denominator);
				texel[1] = sample_ramp(w / denominator);
				texel[2] = 100;

				for (k = 0; k < 3; k++)
					color[k] *= texel[k] / 255;
			}

			pixel = (const unsigned char *)rgba->pixels +
				ys[j] * rgba->pitch + xs[i] * 4;

			for (k = 0; k < 3; k++) {
				error = fabs(pixel[k] - color[k]);

				if (error > 1.2) {
					fprintf(stderr,
						"weather pixel %u,%u channel %u: %u expected %.6f\n",
						xs[i], ys[j], k, pixel[k],
						color[k]);
					success = 0;
				}
			}
		}

	SDL_DestroySurface(rgba);

	return success;
}

static enum mh_error_code
load_sky(const char *root, unsigned int track, struct mh_mesh *mesh,
	struct mh_error *error_out)
{
	struct mh_track config;
	struct mh_file file;
	char path[1024];
	enum mh_error_code status;

	status = mh_track_config_load(root, track, &config, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_asset_path(
			root, config.background, path, sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_file_read(path, &file, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_sky_mesh_create(mh_image_fallback_decoder(),
			file.data, file.size, mesh, error_out);
		free(file.data);
	}

	if (status == MH_ERROR_NONE)
		status = mh_mesh_environment_load(root, track, mesh, error_out);

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_mesh mesh;
	struct mh_image *images;
	struct mh_weather_camera camera;
	struct mh_water_frame water;
	float phase;
	struct mh_sky_state ordinary;
	struct mh_track track;
	struct mh_renderer *renderer;
	unsigned char tga[26];
	char path[1024];
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i, x, y, mode;
	int success;

	if (argc != 3 || strlen(argv[2]) > 900) {
		fprintf(stderr,
			"usage: mh-hidden-weather-render ASSET_ROOT OUTPUT_PREFIX\n");
		return 2;
	}

	memset(&mesh, 0, sizeof(mesh));
	memset(&camera, 0, sizeof(camera));
	camera.width = MH_VIEW_WIDTH;
	camera.height = MH_VIEW_HEIGHT;
	camera.focal = 1.8f;
	camera.forward[0] = .6f;
	camera.forward[2] = .8f;
	camera.position[0] = -1250;
	camera.position[2] = -750;
	camera.underwater = 1;
	camera.top = 0x7f60a0ffUL;
	camera.bottom = 0x7f00a080UL;
	memset(tga, 255, sizeof(tga));
	memset(tga, 0, 18);
	tga[2] = 3;
	tga[12] = 2;
	tga[14] = 4;
	tga[16] = 8;
	tga[17] = 32;
	status = mh_sky_mesh_create(
		mh_image_fallback_decoder(), tga, sizeof(tga), &mesh, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	images = (struct mh_image *)realloc(mesh.textures, 3 * sizeof(*images));

	if (images == NULL) {
		mh_mesh_free(&mesh);
		return 1;
	}

	mesh.textures = images;
	memset(&images[2], 0, sizeof(images[2]));
	mesh.texture_count = 3;
	mesh.environment_texture = 2;
	images[2].rgba = (unsigned char *)malloc(256 * 256 * 4);

	if (images[2].rgba == NULL) {
		mh_mesh_free(&mesh);
		return 1;
	}

	images[2].width = images[2].height = 256;

	for (y = 0; y < 256; y++)
		for (x = 0; x < 256; x++) {
			images[2].rgba[(y * 256 + x) * 4] = (unsigned char)x;
			images[2].rgba[(y * 256 + x) * 4 + 1] =
				(unsigned char)y;
			images[2].rgba[(y * 256 + x) * 4 + 2] = 100;
			images[2].rgba[(y * 256 + x) * 4 + 3] =
				0; /* Original sky ignores texture alpha. */
		}

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		mh_mesh_free(&mesh);
		return 1;
	}

	renderer = mh_sdl_renderer_create(NULL, &mesh);
	success = renderer != NULL;

	for (mode = 0; success && mode < 2; mode++) {
		camera.underwater = (int)mode;

		/* Selected special sky must bypass the world wave. */
		phase = 2;

		if (success)
			success = mh_water_frame(&phase, MH_VIEW_WIDTH,
					MH_VIEW_HEIGHT, &water,
					NULL) == MH_ERROR_NONE &&
				mh_renderer_set_water(renderer, &water);

		sprintf(path, "%s-perspective-%u.bmp", argv[2], mode);

		if (success)
			success = mh_sky_mesh_weather(&mesh, &camera, NULL) ==
					MH_ERROR_NONE &&
				mh_renderer_mesh_sky(renderer, 0) &&
				mh_renderer_frame(
					renderer, identity, identity, 0) &&
				mh_renderer_capture(renderer, path) &&
				capture_matches(path, &camera);
	}

	/* Reusing one uploaded mesh must restore ordinary/black state. */
	if (success)
		success = mh_sky_mesh_black(&mesh, NULL) == MH_ERROR_NONE;

	for (i = 0; success && i < 6; i++)
		success = mesh.vertices[i].projection_w == 10000;

	memset(&ordinary, 0, sizeof(ordinary));
	memset(&track, 0, sizeof(track));
	track.horizon = .25;

	if (success)
		success = mh_sky_mesh_update(&mesh, &ordinary, &track, identity,
				identity, MH_VIEW_WIDTH, MH_VIEW_HEIGHT,
				NULL) == MH_ERROR_NONE;

	for (i = 0; success && i < 18; i++)
		success = mesh.vertices[i].projection_w == 10000;

	if (success)
		success =
			mesh.draws[0].texture == 1 && mesh.draws[2].count == 6;

	mh_renderer_destroy(renderer);
	mh_mesh_free(&mesh);
	camera.underwater = 1;

	for (i = 1; success && i <= 8; i++) {
		status = load_sky(argv[1], i, &mesh, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			success = 0;
			break;
		}

		sprintf(path, "%s-track%u.bmp", argv[2], i);
		status = mh_sky_mesh_weather(&mesh, &camera, &error);
		renderer = status == MH_ERROR_NONE
			? mh_sdl_renderer_create(NULL, &mesh)
			: NULL;

		success = renderer != NULL &&
			mh_renderer_mesh_sky(renderer, 0) &&
			mh_renderer_frame(renderer, identity, identity, 0) &&
			mh_renderer_capture(renderer, path);
		mh_renderer_destroy(renderer);
		mh_mesh_free(&mesh);
	}

	mh_mesh_free(&mesh);

	if (!success)
		fprintf(stderr, "Hidden weather rendering failed: %s\n",
			SDL_GetError());

	SDL_Quit();

	if (success)
		puts("Perspective sky, screen-linear RGB, repeat U/V, alpha bypass, mode restoration and eight original textures passed");

	return !success;
}
