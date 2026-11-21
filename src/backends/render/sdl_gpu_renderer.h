#ifndef MH_SDL_RENDERER_H
#define MH_SDL_RENDERER_H

#include <SDL3/SDL.h>

#include "interfaces/render/renderer.h"

struct mh_gpu_renderer;

/* Backend entry points for SDL-specific render fixtures. */
struct mh_renderer	*mh_sdl_renderer_create(SDL_Window *window,
	const struct mh_mesh *mesh);
struct mh_renderer	*mh_sdl_renderer_create_scene(SDL_Window *window,
	const struct mh_mesh *const *meshes, size_t count);
struct mh_renderer	*mh_sdl_renderer_create_scene_sized(SDL_Window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height);

SDL_GPUTexture		*mh_gpu_create_texture_levels(
	struct mh_gpu_renderer *gpu, Uint32 width, Uint32 height,
	SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage,
	unsigned int levels);
struct mh_gpu_renderer	*mh_gpu_powervr_open(SDL_Window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, int diagnostics);
SDL_GPUTexture		*mh_gpu_create_texture(struct mh_gpu_renderer *gpu,
	Uint32 width, Uint32 height, SDL_GPUTextureFormat format,
	SDL_GPUTextureUsageFlags usage);
int			 mh_gpu_renderer_set_fog(
	struct mh_gpu_renderer *renderer, const struct mh_fog *fog);
enum mh_error_code	 mh_gpu_set_blur_history(struct mh_renderer *renderer,
	unsigned int frames, struct mh_error *error_out);
int			 mh_gpu_renderer_set_motion_blur(
	struct mh_gpu_renderer *renderer, int enabled);
int			 mh_gpu_renderer_set_water(
	struct mh_gpu_renderer *renderer, const struct mh_water_frame *frame);

/* Accepts NULL or a partially constructed renderer; borrowed inputs survive. */
void	mh_gpu_renderer_destroy(struct mh_gpu_renderer *renderer);

/* Borrows meshes, image pixels and window through renderer destruction. */
struct mh_gpu_renderer	*mh_gpu_renderer_open(SDL_Window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, int diagnostics);

#endif
