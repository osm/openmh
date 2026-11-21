#version 450

const float depth_range = 65536.0;

layout(location = 0) in vec3 color_w;
layout(location = 1) in float alpha_w;
layout(location = 2) in vec2 uv;

layout(set = 2, binding = 0) uniform sampler2D diffuse;

layout(set = 3, binding = 0) uniform Material
{
	/* fog, texture alpha, vertex alpha, reversed depth bias */
	vec4 options;
} material;

layout(location = 0) out vec4 output_color;

void
main()
{
	vec3 color;
	float vertex_alpha, alpha;
	bool fog_enabled, texture_alpha, diffuse_alpha;

	fog_enabled = material.options.x > 0.5;
	texture_alpha = material.options.y > 0.5;
	diffuse_alpha = material.options.z > 0.5;

	/* RGB/fog/alpha interpolate in screen space; UVs retain perspective. */
	color = color_w * gl_FragCoord.w;
	vertex_alpha = clamp(alpha_w * gl_FragCoord.w, 0.0, 1.0);
	alpha = diffuse_alpha ? vertex_alpha : 1.0;

	if (texture_alpha) {
		alpha *= texture(diffuse, uv).a;

		if (alpha <= 0.0)
			discard;
	}

	if (fog_enabled)
		color *= 1.0 - vertex_alpha;

	output_color = vec4(color, alpha);

	/*
	 * Native D32 representation of signed 16-bit reversed-Z bias levels.
	 * This does not reproduce Voodoo depth quantization or line coverage.
	 */
	gl_FragDepth = clamp(
		gl_FragCoord.z - material.options.w / depth_range, 0.0, 1.0);
}
