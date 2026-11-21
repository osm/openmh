#include <stddef.h>

#include "backends/render/shader_vertex_layout.h"
#include "engine/render/mesh.h"
#include "engine/render/tron.h"
#include "engine/ui/draw.h"
#include "engine/ui/retro.h"
#include "interfaces/render/glenz.h"

static const struct mh_shader_attribute model_attributes[] = {
	{0, 0, 3, offsetof(struct mh_vertex, position)},
	{1, 0, 3, offsetof(struct mh_vertex, normal)},
	{2, 0, 2, offsetof(struct mh_vertex, uv)},
	{3, 0, 3, offsetof(struct mh_vertex, color)},
	{4, 0, 1, offsetof(struct mh_vertex, projection_w)},
	{5, 0, 1, offsetof(struct mh_vertex, opacity)},
	{6, 1, 2, offsetof(struct mh_glenz_coordinates, reflection)},
	{7, 1, 2, offsetof(struct mh_glenz_coordinates, phong)}};
static const struct mh_shader_vertex_layout model_input = {
	1, 6, {sizeof(struct mh_vertex), 0}, model_attributes};
static const struct mh_shader_vertex_layout glenz_input = {2, 8,
	{sizeof(struct mh_vertex), sizeof(struct mh_glenz_coordinates)},
	model_attributes};

static const struct mh_shader_attribute ui_attributes[] = {
	{0, 0, 2, offsetof(struct mh_ui_vertex, position)},
	{1, 0, 2, offsetof(struct mh_ui_vertex, uv)},
	{2, 0, 4, offsetof(struct mh_ui_vertex, color)}};
static const struct mh_shader_vertex_layout ui_input = {
	1, 3, {sizeof(struct mh_ui_vertex), 0}, ui_attributes};

static const struct mh_shader_attribute tron_attributes[] = {
	{0, 0, 2,
		offsetof(struct mh_tron_vertex, line) +
			offsetof(struct mh_tron_line_vertex, xy)},
	{1, 0, 3,
		offsetof(struct mh_tron_vertex, line) +
			offsetof(struct mh_tron_line_vertex, rgb)},
	{2, 0, 1,
		offsetof(struct mh_tron_vertex, line) +
			offsetof(struct mh_tron_line_vertex, depth)},
	{3, 0, 1,
		offsetof(struct mh_tron_vertex, line) +
			offsetof(struct mh_tron_line_vertex, alpha)},
	{4, 0, 1,
		offsetof(struct mh_tron_vertex, line) +
			offsetof(struct mh_tron_line_vertex, reciprocal_w)},
	{5, 0, 2, offsetof(struct mh_tron_vertex, texture_w)}};
static const struct mh_shader_vertex_layout tron_input = {
	1, 6, {sizeof(struct mh_tron_vertex), 0}, tron_attributes};

static const struct mh_shader_attribute retro_attributes[] = {
	{0, 0, 2,
		offsetof(struct mh_retro_draw_vertex, vertex) +
			offsetof(struct mh_retro_vertex, xy)},
	{1, 0, 1,
		offsetof(struct mh_retro_draw_vertex, vertex) +
			offsetof(struct mh_retro_vertex, depth)},
	{2, 0, 4,
		offsetof(struct mh_retro_draw_vertex, vertex) +
			offsetof(struct mh_retro_vertex, rgb)},
	{3, 0, 2, offsetof(struct mh_retro_draw_vertex, uv)}};
static const struct mh_shader_vertex_layout retro_input = {
	1, 4, {sizeof(struct mh_retro_draw_vertex), 0}, retro_attributes};

static const struct mh_shader_attribute depth_attributes[] = {{0, 0, 3, 0}};
static const struct mh_shader_vertex_layout depth_input = {
	1, 1, {3 * sizeof(float), 0}, depth_attributes};

const struct mh_shader_vertex_layout *
mh_shader_vertex_layout(enum mh_gpu_shader shader)
{
	switch (shader) {
	case MH_GPU_OPENMH_VERTEX:
	case MH_GPU_MATERIAL_VERTEX:
	case MH_GPU_DIRECT3D_VERTEX:
	case MH_GPU_POWERVR_VERTEX:
		return &model_input;
	case MH_GPU_MATERIAL_GLENZ_VERTEX:
	case MH_GPU_DIRECT3D_GLENZ_VERTEX:
	case MH_GPU_POWERVR_GLENZ_VERTEX:
		return &glenz_input;
	case MH_GPU_UI_VERTEX:
		return &ui_input;
	case MH_GPU_TRON_VERTEX:
		return &tron_input;
	case MH_GPU_DEPTH_VERTEX:
		return &depth_input;
	case MH_GPU_RETRO_VERTEX:
		return &retro_input;
	default:
		return NULL;
	}
}
