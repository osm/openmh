#version 450

const float depth_range = 65536.0;
const float color_range = 255.0;

layout(location = 0) in vec2 position;
layout(location = 1) in float depth;
layout(location = 2) in vec4 color;
layout(location = 3) in vec2 uv;

layout(set = 1, binding = 0) uniform Parameters
{
	vec4 viewport;
} parameters;

layout(location = 0) out vec4 vertex_color;
layout(location = 1) out vec2 vertex_uv;

void
main()
{
	gl_Position = vec4(2.0 * position.x / parameters.viewport.x - 1.0,
		1.0 - 2.0 * position.y / parameters.viewport.y,
		1.0 - depth / depth_range, 1.0);
	vertex_color = color / color_range;
	vertex_uv = uv;
}
