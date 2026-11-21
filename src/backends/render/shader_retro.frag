#version 450

layout(location = 0) in vec4 vertex_color;
layout(location = 1) in vec2 vertex_uv;

layout(set = 2, binding = 0) uniform sampler2D atlas;

layout(set = 3, binding = 0) uniform Material
{
	vec4 options;
} material;

layout(location = 0) out vec4 output_color;

void
main()
{
	bool textured;

	textured = material.options.x > 0.5;
	output_color = vertex_color;

	if (textured)
		output_color *= texture(atlas, vertex_uv);

	/*
	 * Alpha test ALWAYS: even transparent font texels retain depth writes.
	 */
}
