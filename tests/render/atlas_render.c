#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/assets/remap.h"
#include "engine/core/error.h"

struct fixture {
	struct mh_mesh			mesh;
	struct mh_vertex		vertices[6];
	struct mh_draw			draw;
	struct mh_mesh_primitive	primitive;
	struct mh_image			images[3];
	unsigned char			pixels[16];
};

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
static const float xy[6][2] = {
	{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
static unsigned int textures_created, textures_released;
static SDL_GPUTexture *shared_texture;

SDL_GPUTexture	*__real_SDL_CreateGPUTexture(SDL_GPUDevice *device,
	const SDL_GPUTextureCreateInfo *info);
void		 __real_SDL_ReleaseGPUTexture(SDL_GPUDevice *device,
	SDL_GPUTexture *texture);

SDL_GPUTexture *
__wrap_SDL_CreateGPUTexture(
	SDL_GPUDevice *device, const SDL_GPUTextureCreateInfo *info)
{
	SDL_GPUTexture *texture;

	texture = __real_SDL_CreateGPUTexture(device, info);

	if (texture != NULL && info->width == 4 && info->height == 2) {
		textures_created++;
		shared_texture = texture;
	}

	return texture;
}

void
__wrap_SDL_ReleaseGPUTexture(SDL_GPUDevice *device, SDL_GPUTexture *texture)
{
	if (texture == shared_texture) {
		textures_released++;
		shared_texture = NULL;
	}

	__real_SDL_ReleaseGPUTexture(device, texture);
}

static void
write_u32le(unsigned char *bytes, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		bytes[i] = (unsigned char)(value >> (i * 8));
}

static void
make_quad(struct fixture *f, unsigned int side, unsigned char *white)
{
	unsigned int i, axis;

	memset(f, 0, sizeof(*f));
	f->mesh.vertices = f->vertices;
	f->mesh.vertex_count = 6;
	f->mesh.draws = &f->draw;
	f->mesh.draw_count = 1;
	f->mesh.primitives = &f->primitive;
	f->mesh.primitive_count = 1;
	f->primitive.count = 6;
	f->mesh.textures = f->images;
	f->mesh.texture_count = side + 2;
	f->draw.count = 6;
	f->draw.prelit = 1;
	f->draw.texture = side + 1;
	strcpy(f->draw.texture_name, side ? "green.lbm" : "red.lbm");

	for (i = 0; i <= side; i++) {
		f->images[i].width = f->images[i].height = 1;
		f->images[i].rgba = white;
	}

	f->images[side + 1].width = f->images[side + 1].height = 2;
	f->images[side + 1].rgba = f->pixels;

	for (i = 0; i < 4; i++) {
		f->pixels[i * 4 + side] = 255;
		f->pixels[i * 4 + 3] = 255;
	}

	for (i = 0; i < 6; i++) {
		f->vertices[i].position[0] =
			xy[i][0] * .4f + (side ? .5f : -.5f);
		f->vertices[i].position[1] = xy[i][1] * .8f;
		f->vertices[i].position[2] = .5f;

		for (axis = 0; axis < 2; axis++)
			f->vertices[i].uv[axis] = xy[i][axis] < 0 ? .25f : .75f;

		for (axis = 0; axis < 3; axis++)
			f->vertices[i].color[axis] = 1;
	}
}

static int
capture_matches(const char *path)
{
	SDL_Surface *bmp, *rgba;
	const unsigned char *pixel;
	unsigned int side, channel;
	int success;

	bmp = SDL_LoadBMP(path);

	if (bmp == NULL)
		return 0;

	rgba = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmp);

	if (rgba == NULL)
		return 0;

	success = rgba->w == 320 && rgba->h == 240;

	for (side = 0; success && side < 2; side++) {
		pixel = (const unsigned char *)rgba->pixels +
			120 * rgba->pitch + (80 + side * 160) * 4;

		for (channel = 0; channel < 3; channel++)
			if (pixel[channel] != (channel == side ? 255 : 0))
				success = 0;
	}

	SDL_DestroySurface(rgba);

	return success;
}

int
main(int argc, char *argv[])
{
	unsigned char records[126], page_pixels[32], white[4];
	struct mh_atlas_map remap;
	struct mh_atlas atlas;
	struct mh_image page;
	struct fixture f[2];
	const struct mh_mesh *meshes[2];
	struct mh_renderer *renderer;
	unsigned int i, x, y, cycle, step;
	unsigned char *record;
	struct mh_error error;
	enum mh_error_code status;
	char path[1024];
	int success;

	if (argc != 2 || strlen(argv[1]) > 900)
		return 2;

	memset(records, 0, sizeof(records));
	memset(white, 255, sizeof(white));
	write_u32le(records, 2);
	write_u32le(records + 4, 1);
	write_u32le(records + 12, 4);
	write_u32le(records + 16, 2);
	write_u32le(records + 20, 0x3f800000UL);
	write_u32le(records + 24, 0x3f800000UL);

	for (i = 0; i < 2; i++) {
		record = records + 28 + i * 49;
		strcpy((char *)record, i ? "green.lbm" : "red.lbm");
		write_u32le(record + 21, i * 2);
		write_u32le(record + 33, 2);
		write_u32le(record + 37, 2);
		write_u32le(record + 41, 2);
		write_u32le(record + 45, 2);
		make_quad(&f[i], i, white);
		meshes[i] = &f[i].mesh;
	}

	memset(page_pixels, 0, sizeof(page_pixels));

	for (y = 0; y < 2; y++)
		for (x = 0; x < 4; x++) {
			page_pixels[(y * 4 + x) * 4 + (x >= 2)] = 255;
			page_pixels[(y * 4 + x) * 4 + 3] = 255;
		}

	page.width = 4;
	page.height = 2;
	page.rgba = page_pixels;
	memset(&atlas, 0, sizeof(atlas));
	atlas.pages = &page;
	atlas.count = 1;
	memset(&remap, 0, sizeof(remap));
	status =
		mh_atlas_remap_decode(records, sizeof(records), &remap, &error);

	for (i = 0; status == MH_ERROR_NONE && i < 2; i++)
		status = mh_mesh_bind_atlas(&f[i].mesh, &remap, &atlas, &error);

	SDL_SetHintWithPriority(
		SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);
	success = status == MH_ERROR_NONE && SDL_Init(SDL_INIT_VIDEO);

	for (cycle = 0; success && cycle < 2; cycle++) {
		renderer = mh_sdl_renderer_create_scene_sized(
			NULL, meshes, 2, 320, 240);
		success = renderer != NULL && textures_created == cycle + 1 &&
			textures_released == cycle;

		for (step = 0; success && step < 4; step++) {
			for (i = 0; success && i < 2; i++) {
				if (step == 1)
					success = mh_renderer_update_mesh(
						renderer, i);

				if (step == 2)
					success = mh_renderer_mesh_lit(
						renderer, i, f[i].vertices);

				if (step == 3)
					success = mh_renderer_mesh_unavailable(
							renderer, i,
							f[i].vertices) &&
						mh_renderer_mesh_unavailable(
							renderer, i, NULL);
			}

			sprintf(path, "%s-%u-%u.bmp", argv[1], cycle, step);

			if (success)
				success =
					mh_renderer_scene_frame(renderer,
						identity, identity, NULL, 0) &&
					mh_renderer_capture(renderer, path) &&
					capture_matches(path);
		}

		mh_renderer_destroy(renderer);
		success = success && textures_created == cycle + 1 &&
			textures_released == cycle + 1;
	}

	for (i = 0; i < 2; i++) {
		free(f[i].mesh.atlas_images);
		free(f[i].mesh.atlas_uv);
	}

	if (!success)
		fprintf(stderr,
			"Atlas render failed: %s; created %u released %u\n",
			status != MH_ERROR_NONE ? error.message
						: SDL_GetError(),
			textures_created, textures_released);

	mh_atlas_map_free(&remap);
	SDL_Quit();

	if (success)
		puts("Atlas GPU: shared upload/release, sampling, vertex refresh, lighting, unavailable restore and recreation passed");

	return !success;
}
