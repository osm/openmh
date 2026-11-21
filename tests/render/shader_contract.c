#include <stdio.h>
#include <string.h>

#include "backends/render/sdl_gpu_shaders.h"
#include "backends/render/sdl_gpu_vertex_input.h"
#include "backends/render/shader_uniforms.h"
#include "engine/render/openmh_light_buffer.h"

static const char *const names[MH_GPU_SHADER_COUNT] = {"material_vert",
	"material_glenz_vert", "material_frag", "direct3d_vert",
	"direct3d_glenz_vert", "direct3d_frag", "powervr_vert",
	"powervr_glenz_vert", "powervr_frag", "ui_vert", "ui_frag", "tron_vert",
	"tron_frag", "retro_vert", "retro_frag", "depth_vert", "depth_frag",
	"openmh_vert"};

static int
check_selection(void)
{
	SDL_GPUShaderCreateInfo info;
	struct mh_error error;
	SDL_GPUShaderFormat formats;

	formats = mh_gpu_shader_formats();

	if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) == 0 ||
		mh_gpu_shader_info(MH_GPU_MATERIAL_VERTEX, formats, &info,
			&error) != MH_ERROR_NONE ||
		info.format != SDL_GPU_SHADERFORMAT_SPIRV ||
		error.code != MH_ERROR_NONE || error.message != NULL)
		return 0;

	if (mh_gpu_shader_info(MH_GPU_MATERIAL_VERTEX, 0, &info, &error) !=
			MH_ERROR_UNSUPPORTED ||
		error.message == NULL)
		return 0;

	if (mh_gpu_shader_info(MH_GPU_MATERIAL_VERTEX,
		SDL_GPU_SHADERFORMAT_DXIL, &info,
		NULL) != MH_ERROR_UNSUPPORTED)
		return 0;

	if (mh_gpu_shader_info(MH_GPU_SHADER_COUNT, formats, &info, NULL) !=
		MH_ERROR_INVALID)
		return 0;

	if ((formats & SDL_GPU_SHADERFORMAT_MSL) == 0 &&
		mh_gpu_shader_info(MH_GPU_MATERIAL_VERTEX,
			SDL_GPU_SHADERFORMAT_MSL, &info,
			NULL) != MH_ERROR_UNSUPPORTED)
		return 0;

	return 1;
}

static int
write_source(const char *directory, const char *format, const char *name,
	const SDL_GPUShaderCreateInfo *info)
{
	char path[1024];
	FILE *file;
	int success;

	/* All format/program names come from the bounded catalogs above. */
	if (strlen(directory) > sizeof(path) - 128)
		return 0;

	strcpy(path, directory);
	strcat(path, "/");
	strcat(path, format);
	strcat(path, "_");
	strcat(path, name);
	strcat(path, ".bin");
	file = fopen(path, "wb");

	if (file == NULL)
		return 0;

	success =
		fwrite(info->code, 1, info->code_size, file) == info->code_size;

	if (fclose(file) != 0)
		success = 0;

	return success;
}

static int
check_programs(SDL_GPUShaderFormat format, const char *directory)
{
	SDL_GPUShaderCreateInfo info;
	const char *format_name;
	const char *entrypoint;
	unsigned int i;
	int included;

	format_name = format == SDL_GPU_SHADERFORMAT_SPIRV ? "vulkan" : "metal";
	entrypoint = format == SDL_GPU_SHADERFORMAT_SPIRV ? "main" : "main0";

	for (i = 0; i < MH_GPU_SHADER_COUNT; i++) {
		included = 1;

		if (i >= MH_GPU_DIRECT3D_VERTEX &&
			i <= MH_GPU_DIRECT3D_FRAGMENT)
			included = MH_RENDER_BUILD_DIRECT3D;
		else if (i >= MH_GPU_POWERVR_VERTEX &&
			i <= MH_GPU_POWERVR_FRAGMENT)
			included = MH_RENDER_BUILD_POWERVR;
		else if (i == MH_GPU_OPENMH_VERTEX)
			included = MH_RENDER_BUILD_OPENMH;

		if (!included) {
			if (mh_gpu_shader_info((enum mh_gpu_shader)i, format,
				&info, NULL) != MH_ERROR_NOT_FOUND)
				return 0;

			continue;
		}

		if (mh_gpu_shader_info((enum mh_gpu_shader)i, format, &info,
			NULL) != MH_ERROR_NONE ||
			info.format != format ||
			strcmp(info.entrypoint, entrypoint) != 0 ||
			info.code == NULL || info.code_size < 4 ||
			info.num_storage_buffers != 0 ||
			info.num_storage_textures != 0)
			return 0;

		if (format == SDL_GPU_SHADERFORMAT_SPIRV) {
			if (memcmp(info.code, "\003\002\043\007", 4) != 0 ||
				info.code_size % 4 != 0)
				return 0;
		} else if (info.code[info.code_size - 1] != '\0' ||
			strstr((const char *)info.code, "main0(") == NULL)
			return 0;

		if (directory == NULL)
			continue;

		if (!write_source(directory, format_name, names[i], &info))
			return 0;

		printf("program %s %s %s %s %u %u\n", names[i], format_name,
			info.stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vert"
								: "frag",
			info.entrypoint, (unsigned int)info.num_uniform_buffers,
			(unsigned int)info.num_samplers);
	}

	return 1;
}

static void
print_vertex_input(enum mh_gpu_shader shader)
{
	const SDL_GPUVertexInputState *input;
	struct mh_gpu_vertex_input vertex_input;
	const SDL_GPUVertexAttribute *attribute;
	const SDL_GPUVertexBufferDescription *buffer;
	const char *type;
	unsigned int i;

	input = mh_gpu_vertex_input(shader, &vertex_input);

	if (input == NULL)
		return;

	for (i = 0; i < input->num_vertex_buffers; i++) {
		buffer = &input->vertex_buffer_descriptions[i];
		printf("buffer %s %u %u\n", names[shader],
			(unsigned int)buffer->slot,
			(unsigned int)buffer->pitch);
	}

	for (i = 0; i < input->num_vertex_attributes; i++) {
		attribute = &input->vertex_attributes[i];

		switch (attribute->format) {
		case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT:
			type = "float";
			break;

		case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2:
			type = "vec2";
			break;

		case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3:
			type = "vec3";
			break;

		case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4:
			type = "vec4";
			break;

		default:
			type = "unsupported";
			break;
		}

		printf("input %s %u %s %u %u\n", names[shader],
			(unsigned int)attribute->location, type,
			(unsigned int)attribute->offset,
			(unsigned int)attribute->buffer_slot);
	}
}

static void
print_uniform_member(
	const char *block, const char *member, unsigned int float_offset)
{
	printf("member %s %s %lu\n", block, member,
		(unsigned long)(float_offset * sizeof(float)));
}

static void
print_uniforms(void)
{
	printf("block Lighting %lu\n",
		(unsigned long)(MH_OPENMH_LIGHT_FLOATS * sizeof(float)));
	printf("block Camera %lu\n",
		(unsigned long)(MH_GPU_CAMERA_FLOATS * sizeof(float)));
	printf("block SceneMaterial %lu\n",
		(unsigned long)(MH_GPU_MATERIAL_FLOATS * sizeof(float)));
	printf("block Vector %lu\n", (unsigned long)(4 * sizeof(float)));
	print_uniform_member("Camera", "matrix", 0);
	print_uniform_member("Camera", "view", MH_GPU_CAMERA_VIEW);
	print_uniform_member("Camera", "model", MH_GPU_CAMERA_MODEL);
	print_uniform_member("Camera", "viewport", MH_GPU_CAMERA_ASPECT);
	print_uniform_member("Camera", "fog", MH_GPU_CAMERA_FOG);
	print_uniform_member("Camera", "water", MH_GPU_CAMERA_WATER_PHASE);
	print_uniform_member(
		"SceneMaterial", "options", MH_GPU_MATERIAL_ENVIRONMENT);
	print_uniform_member(
		"SceneMaterial", "distance_pass", MH_GPU_MATERIAL_DISTANT);
}

int
main(int argc, char *argv[])
{
	const char *directory;
	unsigned int i;
	int success;

	if (argc != 2) {
		fprintf(stderr, "usage: %s --check|OUTPUT_DIRECTORY\n",
			argv[0]);
		return 1;
	}

	directory = strcmp(argv[1], "--check") == 0 ? NULL : argv[1];
	success = sizeof(float) == 4 && check_selection() &&
		check_programs(SDL_GPU_SHADERFORMAT_SPIRV, directory);

	if (success &&
		(mh_gpu_shader_formats() & SDL_GPU_SHADERFORMAT_MSL) != 0)
		success = check_programs(SDL_GPU_SHADERFORMAT_MSL, directory);

	if (!success) {
		fprintf(stderr,
			"shader selection, metadata or source export failed\n");
		return 1;
	}

	if (directory != NULL) {
		print_uniforms();

		for (i = 0; i < MH_GPU_SHADER_COUNT; i++)
			print_vertex_input((enum mh_gpu_shader)i);
	}

	return 0;
}
