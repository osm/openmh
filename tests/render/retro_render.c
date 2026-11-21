#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "backends/retro/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_path.h"
#include "client/diagnostics/asset_ui.h"
#include "client/race/race.h"
#include "engine/assets/retro.h"
#include "engine/core/error.h"
#include "engine/storage/asset_path.h"
#include "interfaces/retro/source.h"

static struct mh_retro_assets assets;
static struct mh_retro_draw draw;
static struct mh_hud_assets hud;
static struct mh_ui_list ui;
static int viewport_width = MH_VIEW_WIDTH, viewport_height = MH_VIEW_HEIGHT;

enum {
	BACKDROP_VERTICES = 30
};

struct pixel {
	double		depth;
	unsigned char	rgb[3], edge;
};

/*
 * Independent screen-space barycentric rasterizer. Compares alpha blending,
 * depth writes (including transparent text), atlas sampling and draw order.
 * Only pixels within 1/64 pixel of a triangle edge or .002 texel of a nearest
 * sampling boundary are omitted: hardware subpixel precision can decide them
 * differently. This does not emulate Voodoo depth/dither/raster quantization.
 */
static int
capture_matches(const struct mh_retro_draw *frame, const char *path)
{
	struct pixel *pixels, *p;
	const struct mh_retro_draw_vertex *v;
	const unsigned char *texel, *actual;
	SDL_Surface *bmp, *rgba;
	double px[3], py[3], area, weights[3], edge, length, z, alpha, rgb[3],
		uv[2], f;
	double dx, dy, distance;
	unsigned int i, j, k, channel, tx, ty, checked;
	int x, y, left, right, top, bottom, inside, success;

	pixels = (struct pixel *)calloc(
		viewport_width * viewport_height, sizeof(*pixels));

	if (pixels == NULL)
		return 0;

	for (i = 0; i < (unsigned int)(viewport_width * viewport_height); i++) {
		pixels[i].rgb[0] = 32;
		pixels[i].rgb[1] = 48;
		pixels[i].rgb[2] = 80;
	}

	for (i = 0; i < frame->count; i += 3) {
		v = frame->vertices + i;

		for (j = 0; j < 3; j++) {
			px[j] = v[j].vertex.xy[0];
			py[j] = v[j].vertex.xy[1];
		}

		area = (px[1] - px[0]) * (py[2] - py[0]) -
			(py[1] - py[0]) * (px[2] - px[0]);

		if (area == 0)
			continue;

		left = right = (int)floor(px[0]);
		top = bottom = (int)floor(py[0]);

		for (j = 1; j < 3; j++) {
			x = (int)floor(px[j]);
			y = (int)floor(py[j]);

			if (x < left)
				left = x;

			if (x > right)
				right = x;

			if (y < top)
				top = y;

			if (y > bottom)
				bottom = y;
		}

		left--;
		top--;
		right++;
		bottom++;

		if (left < 0)
			left = 0;

		if (top < 0)
			top = 0;

		if (right >= viewport_width)
			right = viewport_width - 1;

		if (bottom >= viewport_height)
			bottom = viewport_height - 1;

		for (y = top; y <= bottom; y++)
			for (x = left; x <= right; x++) {
				p = pixels + y * viewport_width + x;
				inside = 1;
				distance = 1e9;

				for (j = 0; j < 3; j++) {
					k = (j + 1) % 3;
					dx = px[k] - px[j];
					dy = py[k] - py[j];
					edge = dx * (y + .5 - py[j]) -
						dy * (x + .5 - px[j]);
					length = sqrt(dx * dx + dy * dy);

					if (length > 0 &&
						fabs(edge) / length < distance)
						distance = fabs(edge) / length;

					weights[(j + 2) % 3] = edge / area;

					if (edge / area < 0)
						inside = 0;
				}

				if (distance < 1.0 / 64)
					p->edge = 1;

				if (!inside)
					continue;

				z = alpha = 0;
				memset(rgb, 0, sizeof(rgb));
				memset(uv, 0, sizeof(uv));

				for (j = 0; j < 3; j++) {
					z += weights[j] * v[j].vertex.depth;
					alpha += weights[j] *
						v[j].vertex.alpha / 255;

					for (k = 0; k < 3; k++)
						rgb[k] += weights[j] *
							v[j].vertex.rgb[k];

					for (k = 0; k < 2; k++)
						uv[k] +=
							weights[j] * v[j].uv[k];
				}

				if (i >= BACKDROP_VERTICES && z < p->depth)
					continue;

				if (i >= BACKDROP_VERTICES &&
					i < frame->text_end) {
					uv[0] *= hud.atlas.image.width;
					uv[1] *= hud.atlas.image.height;

					for (j = 0; j < 2; j++) {
						f = uv[j] - floor(uv[j]);

						if (f < .002 || f > .998)
							p->edge = 1;

						if (uv[j] < 0)
							uv[j] = 0;
					}

					tx = (unsigned int)uv[0];
					ty = (unsigned int)uv[1];

					if (tx >= hud.atlas.image.width)
						tx = hud.atlas.image.width - 1;

					if (ty >= hud.atlas.image.height)
						ty = hud.atlas.image.height - 1;

					texel = hud.atlas.image.rgba +
						((size_t)ty *
								hud.atlas.image
									.width +
							tx) *
							4;
					alpha *= texel[3] / 255.0;

					for (k = 0; k < 3; k++)
						rgb[k] *= texel[k] / 255.0;
				}

				for (k = 0; k < 3; k++) {
					f = rgb[k] * alpha +
						p->rgb[k] * (1 - alpha);

					if (f < 0)
						f = 0;

					if (f > 255)
						f = 255;

					p->rgb[k] =
						(unsigned char)floor(f + .5);
				}

				p->depth = z;
			}
	}

	bmp = SDL_LoadBMP(path);
	rgba = bmp == NULL ? NULL
			: SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);
	success = rgba != NULL && rgba->w == viewport_width &&
		rgba->h == viewport_height;
	checked = 0;

	for (y = 0; y < viewport_height && success; y++)
		for (x = 0; x < viewport_width && success; x++) {
			p = pixels + y * viewport_width + x;

			if (p->edge)
				continue;

			checked++;
			actual = (const unsigned char *)rgba->pixels +
				y * rgba->pitch + x * 4;

			for (channel = 0; channel < 3; channel++)
				if (abs((int)actual[channel] -
					p->rgb[channel]) > 2) {
					fprintf(stderr,
						"%s pixel %d,%d channel %u: %u expected %u\n",
						path, x, y, channel,
						actual[channel],
						p->rgb[channel]);
					success = 0;
					break;
				}
		}

	if (checked < (unsigned int)(viewport_width * viewport_height * 9 /
			10) &&
		success) {
		fprintf(stderr, "too few stable pixels\n");
		success = 0;
	}

	SDL_DestroySurface(rgba);
	free(pixels);

	return success;
}

int
main(int argc, char *argv[])
{
	static const unsigned int lines[] = {
		0, 4, 10, 30, 60, 90, 110, 130, 140, 141};

	const float background[4] = {32.0f / 255, 48.0f / 255, 80.0f / 255, 1};
	struct mh_retro_state state;
	struct mh_renderer *renderer;
	struct mh_error error;
	enum mh_error_code status;
	char path[1024], suffix[40];
	size_t length;
	unsigned int i;
	unsigned long size;
	char *end;
	int success;

	if ((argc != 3 && argc != 5) || strlen(argv[2]) > 900)
		return 2;

	if (argc == 5) {
		size = strtoul(argv[3], &end, 10);

		if (*end != 0 || size < MH_VIEW_MIN_WIDTH ||
			size > MH_VIEW_MAX_WIDTH)
			return 2;

		viewport_width = (int)size;
		size = strtoul(argv[4], &end, 10);

		if (*end != 0 || size < MH_VIEW_MIN_HEIGHT ||
			size > MH_VIEW_MAX_HEIGHT)
			return 2;

		viewport_height = (int)size;
	}

	status = mh_retro_load_from(mh_retro_fallback_source(),
		mh_filesystem_resources(), argv[1], "Data/Retro", &assets,
		&error);

	if (status == MH_ERROR_NONE)
		status = mh_hud_assets_load(argv[1], &hud, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Retro assets: %s\n", error.message);
		return 1;
	}

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		free(hud.atlas.image.rgba);
		return 1;
	}

	renderer = mh_sdl_renderer_create_scene_sized(NULL, NULL, 0,
		(unsigned int)viewport_width, (unsigned int)viewport_height);
	success =
		renderer != NULL && mh_renderer_ui_upload(renderer, &hud.atlas);
	mh_ui_begin(&ui, viewport_width, viewport_height);

	if (success)
		success = mh_ui_quad_scaled(&ui, &hud.atlas, &hud.solid, 0, 0,
			viewport_width, viewport_height, background);

	length = strlen(argv[2]);

	for (i = 0; i < sizeof(lines) / sizeof(lines[0]) && success; i++) {
		state.line = lines[i];
		state.time = i * 7.25f;
		state.wave = i * 14.5f;
		state.scroll = (i % 5) * .25f;
		status = mh_retro_draw(&assets, &hud, viewport_width,
			viewport_height, 0, &state, &draw, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Retro draw: %s\n", error.message);
			success = 0;
			break;
		}

		if (i == 0) {
			draw.width++;
			success = !mh_renderer_scene_frame_retro(
				renderer, NULL, NULL, NULL, 0, &ui, &draw);
			draw.width--;

			if (!success)
				break;
		}

		sprintf(suffix, "-%u.bmp", i);
		memcpy(path, argv[2], length);
		memcpy(path + length, suffix, strlen(suffix) + 1);
		success = mh_renderer_scene_frame_retro(
				renderer, NULL, NULL, NULL, 0, &ui, &draw) &&
			mh_renderer_capture(renderer, path) &&
			capture_matches(&draw, path);
	}

	if (!success)
		fprintf(stderr, "Retro capture check failed: %s\n",
			SDL_GetError());

	mh_renderer_destroy(renderer);
	SDL_Quit();
	free(hud.atlas.image.rgba);

	if (success)
		puts("10 RETRO captures match independent barycentric composition: backdrop, original font, mesh overlap and depth");

	return !success;
}
