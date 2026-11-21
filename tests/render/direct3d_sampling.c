#include <math.h>
#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"

enum sampling_pass {
	TEXTURED,
	CUTOUT,
	SKY,
	PARTICLE,
	GLENZ,
	PASS_COUNT
};

struct sampling_values {
	float		uv[2];
	unsigned int	point_repeat, point_clamp;
	unsigned int	linear_repeat, linear_clamp, linear_sky;
};

struct sampling_scene {
	struct mh_mesh			mesh;
	struct mh_draw			draw;
	struct mh_vertex		vertices[6];
	struct mh_glenz_coordinates	coordinates[6];
	struct mh_renderer_glenz	material;
	struct mh_projection		projection;
	struct mh_renderer		*gpu;
	float				raw[18];
};

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void
make_quad(struct mh_vertex vertices[6], float raw[18], const float uv[2],
	int camera_space)
{
	static const float xy[6][2] = {{-.8f, -.8f}, {.8f, -.8f}, {.8f, .8f},
		{-.8f, -.8f}, {.8f, .8f}, {-.8f, .8f}};

	unsigned int i, channel;

	memset(vertices, 0, 6 * sizeof(*vertices));

	for (i = 0; i < 6; i++) {
		vertices[i].position[0] = xy[i][0] * (camera_space ? 10 : 1);
		vertices[i].position[1] = xy[i][1] * (camera_space ? 7.5f : 1);
		vertices[i].position[2] = camera_space ? -10 : .95f;
		memcpy(vertices[i].uv, uv, sizeof(vertices[i].uv));

		for (channel = 0; channel < 3; channel++) {
			vertices[i].color[channel] = 1;
			raw[i * 3 + channel] = 255;
		}
	}
}

static int
capture_matches(const char *path, unsigned int expected)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int x, y, c;
	int ok;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	ok = 1;

	for (y = 110; y < 130 && ok; y++) {
		for (x = 150; x < 170 && ok; x++) {
			pixel = (const unsigned char *)rgba->pixels +
				y * rgba->pitch + x * 4;

			for (c = 0; c < 3; c++) {
				if (fabs((double)pixel[c] - expected) > 1.1) {
					fprintf(stderr,
						"pixel %u,%u channel %u: %u expected %u\n",
						x, y, c, pixel[c], expected);
					ok = 0;
					break;
				}
			}
		}
	}

	SDL_DestroySurface(rgba);

	return ok;
}

static unsigned int
expected_color(const struct sampling_values *values, unsigned int pass,
	unsigned int filter, int direct3d)
{
	int nearest;

	nearest = filter == MH_TEXTURE_FILTER_NEAREST ||
		(pass == CUTOUT &&
			(direct3d || filter == MH_TEXTURE_FILTER_ORIGINAL));

	if (pass == SKY || pass == PARTICLE)
		nearest = 0;

	if (direct3d || pass == GLENZ)
		return nearest ? values->point_repeat : values->linear_repeat;

	if (pass == SKY)
		return values->linear_sky;

	return nearest ? values->point_clamp : values->linear_clamp;
}

static int
draw_sample(struct sampling_scene *scene, const float uv[2], unsigned int pass,
	unsigned int filter, int direct3d)
{
	unsigned int i;
	int ok;

	make_quad(scene->vertices, scene->raw, uv,
		direct3d && pass != SKY && pass != PARTICLE);
	scene->draw.alpha_test = pass == CUTOUT;
	scene->draw.environment = pass == GLENZ;
	scene->mesh.environment_texture = pass == GLENZ ? 1 : 0;

	for (i = 0; i < 6; i++)
		memcpy(scene->coordinates[i].reflection, uv,
			sizeof(scene->coordinates[i].reflection));

	ok = mh_renderer_mesh_lit(scene->gpu, 0, scene->vertices) &&
		mh_renderer_mesh_glenz(scene->gpu, 0,
			pass == GLENZ ? &scene->material : NULL,
			NULL) == MH_ERROR_NONE &&
		mh_renderer_set_texture_filter(scene->gpu,
			(enum mh_texture_filter)filter, NULL) == MH_ERROR_NONE;

	if (ok && pass == SKY)
		ok = mh_renderer_mesh_sky(scene->gpu, 0);
	else if (ok && pass == PARTICLE)
		ok = mh_renderer_mesh_particles(scene->gpu, 0);

	if (!ok)
		return 0;

	return direct3d ? mh_renderer_scene_frame_lens(scene->gpu,
				&scene->projection, identity, NULL, 1, NULL,
				NULL, NULL, NULL) == MH_ERROR_NONE
			: mh_renderer_scene_frame(
				scene->gpu, identity, identity, NULL, 1);
}

int
main(int argc, char *argv[])
{
	/* Distinct rows/columns expose addressing and edge interpolation. */
	static const struct sampling_values samples[] = {
		{{.25f, .25f}, 32, 32, 32, 32, 32},
		{{1.25f, .25f}, 32, 96, 32, 96, 32},
		{{-.25f, .25f}, 96, 32, 96, 32, 96},
		{{.25f, 1.25f}, 32, 160, 32, 160, 160},
		{{.25f, -.25f}, 160, 32, 160, 32, 32},
		{{0, .25f}, 32, 32, 64, 32, 64},
		{{.5f, .25f}, 96, 96, 64, 64, 64},
		{{.25f, 0}, 32, 32, 96, 32, 32}};

	struct sampling_scene scene;
	const struct mh_mesh *input;
	struct mh_image textures[3];
	struct mh_fog fog;
	const double cue[3] = {0, 0, 0};
	unsigned char white[4] = {255, 255, 255, 255};
	unsigned char black[4] = {0, 0, 0, 255};
	unsigned char texels[16] = {32, 32, 32, 255, 96, 96, 96, 255, 160, 160,
		160, 255, 224, 224, 224, 255};

	unsigned int pass, test, filter, direct3d, expected, frames;
	int ok;

	if (argc != 2)
		return 2;

	memset(&scene.mesh, 0, sizeof(scene.mesh));
	memset(&scene.draw, 0, sizeof(scene.draw));
	memset(scene.coordinates, 0, sizeof(scene.coordinates));
	textures[0].width = textures[0].height = 1;
	textures[0].rgba = white;
	textures[1].width = textures[1].height = 2;
	textures[1].rgba = texels;
	textures[2] = textures[0];
	textures[2].rgba = black;
	make_quad(scene.vertices, scene.raw, samples[0].uv, 0);
	scene.mesh.vertices = scene.vertices;
	scene.mesh.vertex_count = 6;
	scene.mesh.material_rgb = scene.raw;
	scene.mesh.material_rgb_count = 6;
	scene.mesh.draws = &scene.draw;
	scene.mesh.draw_count = 1;
	scene.mesh.textures = textures;
	scene.mesh.texture_count = 3;
	scene.draw.count = 6;
	scene.draw.prelit = 1;
	scene.draw.texture = 1;
	scene.material.coordinates = scene.coordinates;
	scene.material.count = 6;
	scene.material.reflection_texture = 1;
	scene.material.phong_texture = 2;
	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);

	if (!SDL_Init(SDL_INIT_VIDEO))
		return 1;

	input = &scene.mesh;
	scene.gpu =
		mh_sdl_renderer_create_scene_sized(NULL, &input, 1, 320, 240);
	ok = scene.gpu != NULL &&
		mh_fog_configure(cue, 180, &fog, NULL) == MH_ERROR_NONE &&
		mh_renderer_set_fog(scene.gpu, &fog) &&
		mh_projection_setup(320, 240, 4.0f / 3, 1, .5f, 180,
			&scene.projection, NULL) == MH_ERROR_NONE;
	frames = 0;

	for (pass = 0; pass < PASS_COUNT && ok; pass++) {
		for (test = 0;
			test < sizeof(samples) / sizeof(samples[0]) && ok;
			test++) {
			for (filter = 0; filter < 4 && ok; filter++) {
				for (direct3d = 0; direct3d < 2 && ok;
					direct3d++) {
					expected =
						expected_color(&samples[test],
							pass, filter, direct3d);
					ok = draw_sample(&scene,
						samples[test].uv, pass,
						filter, direct3d) &&
						mh_renderer_capture(
							scene.gpu, argv[1]) &&
						capture_matches(
							argv[1], expected);
					frames++;

					if (!ok)
						fprintf(stderr,
							"Sampling pass %u UV %u filter %u Direct3D=%u: %s\n",
							pass, test, filter,
							direct3d,
							SDL_GetError());
				}
			}
		}
	}

	mh_renderer_destroy(scene.gpu);
	SDL_Quit();

	if (ok)
		printf("%u Glide/Direct3D sampling frames pass: repeat/clamp, U/V edge interpolation, alpha point filtering, sky/effect isolation and all filter choices\n",
			frames);

	return !ok;
}
