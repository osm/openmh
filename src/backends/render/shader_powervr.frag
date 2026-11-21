#version 450
#extension GL_GOOGLE_include_directive : require

#include "shader_powervr.glsl"

layout(location = 0) in vec3 vertex_normal;
layout(location = 1) in vec2 vertex_uv;
layout(location = 2) in vec3 vertex_color_w;
layout(location = 3) flat in float vertex_prelit;
layout(location = 4) in float vertex_fog_w;
layout(location = 5) flat in vec3 fog_color;
layout(location = 6) flat in float vertex_alpha_test;
layout(location = 7) flat in float distant_face;
layout(location = 8) in float background_amount_w;
layout(location = 9) flat in vec3 flat_color;
layout(location = 10) flat in float flat_shading;
layout(location = 11) in float vertex_opacity_w;
layout(location = 12) flat in vec2 powervr_fog;

layout(set = 2, binding = 0) uniform sampler2D diffuse;

layout(location = 0) out vec4 output_color;

void
main()
{
	vec4 texel;
	vec3 vertex_color;
	bool fog_enabled;
	float transparency;

	/*
	 * SGL depth planes use fInvW, including immediate sky/weather quads. A
	 * monotone native mapping keeps their ordering without clipping large
	 * reciprocal values or reproducing the ISP's fixed-point precision.
	 */
	gl_FragDepth = 1.0 / (1.0 + gl_FragCoord.w);

	/*
	 * Recover the original screen-linear attributes from vertex weighting.
	 */
	vertex_color = flat_shading > 0.5 ? flat_color
					  : vertex_color_w * gl_FragCoord.w;
	texel = texture(diffuse, vertex_uv);
	fog_enabled = powervr_fog.y > 0.5;
	transparency = vertex_opacity_w * gl_FragCoord.w;
	output_color = powervr_shade(texel, vertex_color, fog_color,
		fog_enabled, gl_FragCoord.w, transparency);
}
