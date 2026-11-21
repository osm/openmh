#version 450

layout(location = 0) in vec2 vertex_uv;
layout(location = 1) in vec4 vertex_color;

layout(set = 2, binding = 0) uniform sampler2D atlas;

layout(location = 0) out vec4 output_color;

void
main()
{
	output_color = texture(atlas, vertex_uv) * vertex_color;
}
