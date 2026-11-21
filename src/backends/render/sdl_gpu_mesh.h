#ifndef MH_GPU_MESH_H
#define MH_GPU_MESH_H

#include "backends/render/sdl_gpu_renderer.h"
#include "engine/render/scene_effect.h"

struct mh_gpu_renderer;

#define NO_IMAGE ((size_t)-1)

int	mh_gpu_error(const char *operation, size_t mesh, size_t item,
	Uint32 size);

/*
 * Startup-only shared image plan; maximum_upload is the per-mesh transfer size.
 * Partial allocations remain owned by the renderer on failure.
 */
int	mh_gpu_prepare_images(struct mh_gpu_renderer *gpu,
	Uint32 *maximum_upload, int verbose);

/*
 * Borrows the transfer buffer; uploaded texture batches finish before return.
 */
int	mh_gpu_upload_mesh(struct mh_gpu_renderer *gpu, size_t index,
	SDL_GPUTransferBuffer *transfer);

/*
 * Vertex updates copy data and publish cached CPU attributes after submission.
 * Effect selectors publish their effect only after a successful upload.
 */
int	mh_gpu_renderer_update_mesh(struct mh_gpu_renderer *renderer,
	size_t index);
int	mh_gpu_renderer_mesh_visible(struct mh_gpu_renderer *renderer,
	size_t index, int visible);
int	mh_gpu_renderer_mesh_lit(struct mh_gpu_renderer *renderer, size_t index,
	const struct mh_vertex *vertices);
int	mh_gpu_renderer_mesh_unavailable(struct mh_gpu_renderer *renderer,
	size_t index, const struct mh_vertex *vertices);
int	mh_gpu_renderer_mesh_skids(struct mh_gpu_renderer *renderer,
	size_t index);
int	mh_gpu_renderer_mesh_shadows(struct mh_gpu_renderer *renderer,
	size_t index);
int	mh_gpu_renderer_mesh_particles(struct mh_gpu_renderer *renderer,
	size_t index);
int	mh_gpu_renderer_mesh_sky(struct mh_gpu_renderer *renderer,
	size_t index);
int	mh_gpu_renderer_mesh_halos(struct mh_gpu_renderer *renderer,
	size_t index);

#endif
