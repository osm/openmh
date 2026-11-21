#version 450

/* Shared textured vertex processing for prelit geometry and GPU effects. */

const float projection_w_min = 0.000001;
const float environment_uv_scale = 0.2495;
const float water_amplitude = 10.0;
const float color_range = 255.0;
const float fog_start = 0.2;
const float fog_width = 0.5;
const float distant_start = 0.8;
const float distant_scale = 7.0;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 color;
layout(location = 4) in float projection_w;
layout(location = 5) in float opacity;
#ifdef MH_GLENZ
/* Reflection and Phong must agree at coplanar depth across both pipelines. */
invariant gl_Position;
layout(location = 6) in vec2 reflection_uv;
layout(location = 7) in vec2 phong_uv;
#endif

layout(set = 1, binding = 0) uniform Camera
{
	mat4 matrix;
	mat4 view;
	mat4 model;
	vec4 viewport; /* height/width, width, height, PowerVR profile */
	vec4 fog;      /* Normalized fog RGB and reciprocal far distance. */
	vec4 water;    /* phase, scale, frequency, enabled */
} camera;

layout(set = 1, binding = 1) uniform Material
{
	/* environment pass, prelit, fog enabled, alpha test */
	vec4 options;

	/* distant, prepared geometry/fog, flat color, diffuse alpha */
	vec4 distance_pass;
} material;

layout(location = 0) out vec3 vertex_normal;
layout(location = 1) out vec2 vertex_uv;
layout(location = 2) out vec3 vertex_color_w;
layout(location = 3) flat out float vertex_prelit;
layout(location = 4) out float vertex_fog_w;
layout(location = 5) flat out vec3 fog_color;
layout(location = 6) flat out float vertex_alpha_test;
layout(location = 7) flat out float distant_face;
layout(location = 8) out float background_amount_w;
layout(location = 9) flat out vec3 flat_color;
layout(location = 10) flat out float flat_shading;
layout(location = 11) out float vertex_opacity_w;
layout(location = 12) flat out vec2 powervr_fog;

/*
 * Fog visibility and background fade use byte values before normalization.
 */
float
fog_visibility(float depth)
{
	float visibility;

	visibility = depth < fog_start
		? 1.0
		: clamp((fog_width - (depth - fog_start)) * 2.0, 0.0, 1.0);

	return floor(visibility * color_range);
}

float
fog_background(float depth)
{
	return floor(clamp(max(depth - distant_start, 0.0) * distant_scale, 0.0,
			     1.0) *
		color_range);
}

#ifndef MH_GLENZ
/*
 * Same operation family as mh_software_reflection_uv, with hardware scale and
 * normalized coordinates. Software retains its own texel-space math.
 */
vec2
reflection_uv(vec4 clip_position, vec3 source_normal)
{
	vec2 screen, view_normal;

	screen = clip_position.xy / max(clip_position.w, projection_w_min);
	screen.y *= camera.viewport.x;
	view_normal = (camera.view * vec4(source_normal, 0.0)).xy;

	return clamp(vec2(0.5) + environment_uv_scale * (view_normal + screen),
		0.0, 1.0);
}
#endif

/*
 * Change XY after UV generation. Retain Z/W. Clip-space form of the
 * bottom-right-anchored pixel transformation. GPU clipping still differs from
 * CPU clipping.
 */
vec4
water_position(vec4 clip_position)
{
	float phase, scale, frequency, width, height, dy, wave;

	if (!(camera.water.w > 0.5 && clip_position.w > projection_w_min))
		return clip_position;

	phase = camera.water.x;
	scale = camera.water.y;
	frequency = camera.water.z;
	width = camera.viewport.y;
	height = camera.viewport.z;

	dy = -(1.0 + clip_position.y / clip_position.w) * height * 0.5;
	wave = sin(dy * frequency + phase) * water_amplitude;
	clip_position.x = clip_position.x * scale +
		(1.0 - scale + 2.0 * wave / width) * clip_position.w;
	clip_position.y =
		clip_position.y * scale + (scale - 1.0) * clip_position.w;

	return clip_position;
}

void
main()
{
	float depth, environment_pass, prepared, fog_enabled;

	environment_pass = material.options.x;
	prepared = material.distance_pass.y;
	fog_enabled = material.options.z;

	gl_Position = camera.matrix * vec4(position, 1.0);
	powervr_fog = vec2(camera.viewport.w, fog_enabled);

	/*
	 * Immediate special-sky quads retain their supplied reciprocal W.
	 * Scaling every clip component preserves normalized position/depth,
	 * while UV interpolation uses the original perspective denominator.
	 */
	if (projection_w > 0.0)
		gl_Position *= projection_w;

	vertex_normal = (camera.model * vec4(normal, 0.0)).xyz;
	vertex_uv = uv;
#ifdef MH_GLENZ
	vertex_uv = environment_pass > 1.5 ? phong_uv : reflection_uv;
#else
	if (environment_pass > 0.5 && prepared < 0.5) {
		/*
		 * Per-corner normal transform, screen offset in half-width
		 * units, and the original 0.2495 scale. Compute before
		 * interpolation, including the per-vertex clamp. SDL applies
		 * its viewport Y conversion after this shader.
		 */
		vertex_uv = reflection_uv(gl_Position, normal);
	}
#endif
	gl_Position = water_position(gl_Position);

	/*
	 * Keep RGB, fog and distant fade linear in screen space. Explicit
	 * homogeneous weighting avoids zeroed noperspective values when the
	 * tested Vulkan backend clips a triangle at multiple viewport edges.
	 * Smooth interpolation of (value * W), followed by gl_FragCoord.w in
	 * the fragment shader, gives sum(screen_barycentric * value).
	 */
	vertex_color_w = color * gl_Position.w;
	vertex_opacity_w = (material.distance_pass.w > 0.5 ? opacity : 1.0) *
		gl_Position.w;

	/* The material submission selects flat shading for ground effects. */
	flat_color = color;
	flat_shading = material.distance_pass.z;
	vertex_prelit = material.options.y;

	/*
	 * Truncate visibility to a byte at each vertex, then interpolate fog
	 * across the screen. Interpolating depth first changes the ramp.
	 */
	depth = -(camera.view * vec4(position, 1.0)).z * camera.fog.w;
	vertex_fog_w =
		fog_enabled * (1.0 - fog_visibility(depth) / color_range);
	vertex_fog_w *= gl_Position.w;
	fog_color = camera.fog.rgb;
	vertex_alpha_test = material.options.w;
	distant_face = material.distance_pass.x;

	/* Separately byte-quantized fade of fully fogged faces. */
	background_amount_w =
		fog_background(depth) / color_range * gl_Position.w;
	if (prepared > 0.5) {
		/*
		 * CPU preparation already clipped raw RGB and packed fog bytes.
		 * Normal attributes carry visibility/background for these
		 * vertices.
		 */
		vertex_prelit = 1.0;
		vertex_fog_w = fog_enabled * (1.0 - normal.x) * gl_Position.w;
		background_amount_w = normal.y * gl_Position.w;
	}
}
