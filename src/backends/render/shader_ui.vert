#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(set = 1, binding = 0) uniform Viewport
{
	vec4 size;
} viewport;

layout(location = 0) out vec2 vertex_uv;
layout(location = 1) out vec4 vertex_color;

void
main()
{
	gl_Position = vec4(position.x * 2.0 / viewport.size.x - 1.0,
		1.0 - position.y * 2.0 / viewport.size.y, 0.0, 1.0);
	vertex_uv = uv;
	vertex_color = color;
}
