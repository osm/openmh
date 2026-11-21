#version 450

const float depth_range = 65536.0;
const float color_range = 255.0;
const float texture_scale = 256.0;

layout(location = 0) in vec2 position;
layout(location = 1) in vec3 color;
layout(location = 2) in float depth;
layout(location = 3) in float alpha;
layout(location = 4) in float reciprocal_w;
layout(location = 5) in vec2 texture_w;

layout(set = 1, binding = 0) uniform Parameters
{
	vec4 viewport; /* width, height, unused, unused */
} parameters;

layout(location = 0) out vec3 color_w;
layout(location = 1) out float alpha_w;
layout(location = 2) out vec2 uv;

void
main()
{
	float w;

	w = 1.0 / reciprocal_w;
	gl_Position = vec4(2.0 * position.x / parameters.viewport.x - 1.0,
			      1.0 - 2.0 * position.y / parameters.viewport.y,
			      1.0 - depth / depth_range, 1.0) *
		w;
	color_w = color * (w / color_range);
	alpha_w = alpha * (w / color_range);
	uv = texture_w * (w / texture_scale);
}
