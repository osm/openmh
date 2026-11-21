#include <string.h>

#include "backends/render/sdl_gpu_shaders.h"

struct shader_source {
	enum mh_gpu_shader	shader;
	const unsigned char	*bytes;
	size_t			size;
};

struct shader_format {
	SDL_GPUShaderFormat		format;
	const char			*entrypoint;
	const struct shader_source	*sources;
	size_t				count;
};

struct shader_program {
	SDL_GPUShaderStage	stage;
	unsigned int		uniforms, samplers;
};

#include "shader_sources.h"

/*
 * Bindings are independent of format and pipelines. Indexed by mh_gpu_shader;
 * resource reflection checks every entry.
 */
static const struct shader_program programs[MH_GPU_SHADER_COUNT] = {
	{SDL_GPU_SHADERSTAGE_VERTEX, 2, 0},   /* material_vert */
	{SDL_GPU_SHADERSTAGE_VERTEX, 2, 0},   /* material_glenz_vert */
	{SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1}, /* material_frag */
	{SDL_GPU_SHADERSTAGE_VERTEX, 2, 0},   /* direct3d_vert */
	{SDL_GPU_SHADERSTAGE_VERTEX, 2, 0},   /* direct3d_glenz_vert */
	{SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1}, /* direct3d_frag */
	{SDL_GPU_SHADERSTAGE_VERTEX, 2, 0},   /* powervr_vert */
	{SDL_GPU_SHADERSTAGE_VERTEX, 2, 0},   /* powervr_glenz_vert */
	{SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1}, /* powervr_frag */
	{SDL_GPU_SHADERSTAGE_VERTEX, 1, 0},   /* ui_vert */
	{SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1}, /* ui_frag */
	{SDL_GPU_SHADERSTAGE_VERTEX, 1, 0},   /* tron_vert */
	{SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1}, /* tron_frag */
	{SDL_GPU_SHADERSTAGE_VERTEX, 1, 0},   /* retro_vert */
	{SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1}, /* retro_frag */
	{SDL_GPU_SHADERSTAGE_VERTEX, 1, 0},   /* depth_vert */
	{SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1}, /* depth_frag */
	{SDL_GPU_SHADERSTAGE_VERTEX, 4, 0} /* openmh_vert */};

SDL_GPUShaderFormat
mh_gpu_shader_formats(void)
{
	SDL_GPUShaderFormat supported;
	size_t i;

	supported = 0;

	for (i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
		supported |= formats[i].format;

	return supported;
}

static const struct shader_format *
select_format(SDL_GPUShaderFormat supported)
{
	size_t i;

	/* Catalog order determines which supported format takes priority. */
	for (i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
		if ((supported & formats[i].format) != 0)
			return &formats[i];

	return NULL;
}

static const struct shader_source *
find_source(const struct shader_format *format, enum mh_gpu_shader shader)
{
	unsigned int i;

	for (i = 0; i < format->count; i++)
		if (format->sources[i].shader == shader)
			return &format->sources[i];

	return NULL;
}

enum mh_error_code
mh_gpu_shader_info(enum mh_gpu_shader shader, SDL_GPUShaderFormat supported,
	SDL_GPUShaderCreateInfo *info, struct mh_error *error_out)
{
	const struct shader_format *format;
	const struct shader_source *source;
	const struct shader_program *program;

	if (info == NULL || shader < 0 || shader >= MH_GPU_SHADER_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid GPU shader request");

	format = select_format(supported);

	if (format == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"GPU device supports no compiled shader format");

	source = find_source(format, shader);

	if (source == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"GPU shader program is missing from the selected format");

	program = &programs[shader];
	memset(info, 0, sizeof(*info));
	info->code = source->bytes;
	info->code_size = source->size;
	info->entrypoint = format->entrypoint;
	info->format = format->format;
	info->stage = program->stage;
	info->num_uniform_buffers = program->uniforms;
	info->num_samplers = program->samplers;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

SDL_GPUShader *
mh_gpu_shader_create(SDL_GPUDevice *device, enum mh_gpu_shader shader)
{
	SDL_GPUShaderCreateInfo info;
	struct mh_error error;

	if (device == NULL) {
		SDL_SetError("missing GPU device");
		return NULL;
	}

	if (mh_gpu_shader_info(shader, SDL_GetGPUShaderFormats(device), &info,
		&error) != MH_ERROR_NONE) {
		SDL_SetError("%s", error.message);
		return NULL;
	}

	return SDL_CreateGPUShader(device, &info);
}
