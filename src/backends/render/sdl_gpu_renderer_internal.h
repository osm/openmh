#ifndef MH_SDL_GPU_RENDERER_INTERNAL_H
#define MH_SDL_GPU_RENDERER_INTERNAL_H

#include "backends/render/sdl_gpu_material_queue.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_pipelines.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "engine/core/limits.h"
#include "engine/render/mipmap.h"
#include "engine/render/renderer.h"
#include "engine/render/scene_face.h"
#include "engine/render/scene_tron.h"

enum {
	MH_GPU_PIXEL_BYTES = 4
};

#include "engine/render/openmh_light_buffer.h"

struct mh_gpu_mesh {
	struct mh_openmh_light_buffer	openmh;
	SDL_GPUBuffer			*glenz_vertices;
	SDL_GPUTransferBuffer		*glenz_upload;
	size_t				glenz_reflection, glenz_phong;
	int				glenz;
	SDL_GPUBuffer			*vertices;
	SDL_GPUTransferBuffer		*vertex_upload;

	/* Indices into the scene's shared immutable image pool. */

	size_t				*images;
	const struct mh_mesh		*mesh;

	/* Validated for every scene frame. */
	struct mh_mesh_lod		range;
	int				hidden;
	enum mh_scene_effect		effect;

	/* CPU object lighting replaces the inspection shader. */
	int				lit;

	/* Latest successful upload, for whole-face dispatch. */
	float				*positions;

	/* Latest CPU lighting, also consumed by TRON outlines/fills. */
	float				*colors;

	/* PowerVR ordering uses the uploaded reciprocal depth. */
	float				*projection_w;
	float				*material_rgb;
	struct mh_glenz_coordinates	*glenz_coordinates;
};

struct mh_gpu_primitive {
	size_t	model, draw, first, count, next;
	int	near_clipped, prepared;
};

struct mh_gpu_image {
	unsigned int		format;
	unsigned int		width, height;
	const struct mh_image	*source;
	SDL_GPUTexture		*texture;

	/* First mesh using it owns the startup upload batch. */
	size_t			owner;
	Uint32			offset;
	unsigned int		levels;
};

/*
 * Private scene state. Meshes/atlas pixels and the window are borrowed;
 * renderer destruction waits for the device before releasing owned resources.
 */
struct mh_gpu_renderer {
	struct mh_renderer		base;

	/*
	 * Immutable geometry, texture and effect policy selected at creation.
	 */
	int				powervr;
	struct mh_gpu_powervr_order	*powervr_order;
	SDL_GPUDevice			*device;
	SDL_Window			*window;
	int				window_claimed;
	unsigned int			width, height;
	struct mh_gpu_pipeline_set	material_pipelines;
	struct mh_gpu_pipeline_set	openmh_pipelines;
	struct mh_gpu_pipeline_set	direct3d_pipelines;
	struct mh_gpu_pipeline_set	powervr_pipelines;
	SDL_GPUTexture			*color;
	SDL_GPUTexture			*back_color;
	SDL_GPUTexture			*third_color;
	int	color_ready, back_color_ready, third_color_ready, motion_blur;
	SDL_GPUTexture			*depth;
	SDL_GPUSampler			*sampler;
	SDL_GPUSampler			*sky_sampler;
	SDL_GPUSampler			*repeat_sampler;
	SDL_GPUSampler			*alpha_sampler;
	SDL_GPUSampler			*filter_samplers[3], *repeat_filters[3];
	enum mh_texture_filter		texture_filter;

	struct mh_gpu_primitive		*primitives;
	size_t				primitive_capacity, primitive_count;

	struct mh_vertex		*prepared_vertices;
	struct mh_glenz_coordinates	*prepared_glenz;
	size_t	prepared_count, prepared_capacity, prepared_buffer_capacity;
	SDL_GPUBuffer			*prepared_buffer;
	SDL_GPUTransferBuffer		*prepared_upload;
	size_t	list_heads[MH_LIST_COUNT], list_tails[MH_LIST_COUNT];

	struct mh_gpu_mesh		*meshes;
	size_t				mesh_count;
	struct mh_gpu_image		*images;
	size_t				image_count;

	struct mh_renderer_scene_part	*parts;
	size_t				part_count;

	SDL_GPUGraphicsPipeline		*ui_pipeline;
	SDL_GPUTexture			*ui_texture;
	SDL_GPUSampler			*ui_sampler;
	SDL_GPUBuffer			*ui_vertices;
	SDL_GPUTransferBuffer		*ui_transfer;

	struct mh_fog			fog;
	int				fog_enabled;

	struct mh_water_frame		water;
	int				water_enabled;

	SDL_GPUTransferBuffer		*depth_transfer;
	SDL_GPUFence			*depth_fence;
	struct mh_depth_probe		*depth_probes;
	size_t				depth_capacity, depth_count;

	struct mh_gpu_tron		*tron;
	struct mh_gpu_retro		*retro;
	struct mh_tron_scene		tron_scene;
};

#endif
