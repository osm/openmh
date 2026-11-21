#version 450

/* Direct3D scene shading. Keep this profile independently editable. */
const float normal_length_squared_min = 0.00001;

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

/* Inspection lighting; original prepared race vertices are already lit. */
float
inspection_light(vec3 normal)
{
	vec3 direction;
	float length_squared, light;

	length_squared = dot(normal, normal);
	direction = length_squared > normal_length_squared_min
		? normal * inversesqrt(length_squared)
		: vec3(0, 1, 0);
	light = 0.45 +
		0.55 * max(dot(direction, normalize(vec3(0.4, 0.8, 0.5))), 0.0);

	if (vertex_prelit > 0.5)
		light = 1.0;

	return light;
}

vec4
shade_distant(vec4 texel, float background_amount)
{
	float opacity;

	opacity = 1.0 - clamp(background_amount, 0.0, 1.0);

	if (vertex_alpha_test > 0.5) {
		opacity *= texel.a;

		if (opacity <= 0.0)
			discard;
	}

	return vec4(fog_color, opacity);
}

vec4
shade_material(vec4 texel, vec3 vertex_color, float vertex_fog)
{
	float light;

	light = inspection_light(vertex_normal);

	return vec4(mix(texel.rgb * vertex_color * light, fog_color,
			    clamp(vertex_fog, 0.0, 1.0)),
		vertex_opacity_w * gl_FragCoord.w);
}

void
main()
{
	vec4 texel;
	vec3 vertex_color;
	float vertex_fog, background_amount;

	/*
	 * Undo the vertex weighting to recover screen-linear attributes.
	 * gl_FragCoord.w is the interpolated reciprocal clip W.
	 */
	vertex_color = flat_shading > 0.5 ? flat_color
					  : vertex_color_w * gl_FragCoord.w;
	vertex_fog = vertex_fog_w * gl_FragCoord.w;
	background_amount = background_amount_w * gl_FragCoord.w;
	texel = texture(diffuse, vertex_uv);

	/* Alpha materials use GREATER/zero; particles have their own cutoff. */
	if (vertex_alpha_test > 1.5 && texel.a < 0.5)
		discard;

	if (vertex_alpha_test > 0.5 && texel.a <= 0.0)
		discard;

	if (distant_face > 0.5) {
		output_color = shade_distant(texel, background_amount);
		return;
	}

	output_color = shade_material(texel, vertex_color, vertex_fog);
}
