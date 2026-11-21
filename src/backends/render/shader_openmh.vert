#version 450

/* OpenMH owns lighting arithmetic; shared fragment shading preserves the
 * game's textures, screen-linear color interpolation, fog and blending. */
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 color;
layout(location = 4) in float projection_w;
layout(location = 5) in float opacity;

layout(set = 1, binding = 0) uniform Camera {
	mat4 matrix;
	mat4 view;
	mat4 model;
	vec4 viewport;
	vec4 fog;
	vec4 water;
} camera;
layout(set = 1, binding = 1) uniform Material {
	vec4 options;
	vec4 distance_pass;
} material;

/* std140 layout shared with mh_openmh_light_buffer. Each light occupies 3 vec4s. */
struct Light { vec4 direction; vec4 color; vec4 view; };
layout(set = 1, binding = 2) uniform Lighting {
	vec4 ambient_count;
	vec4 brightness_specular;
	vec4 tint;
	Light lights[64];
} lighting;
layout(set = 1, binding = 3) uniform Surface { vec4 base_mode; } surface;

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

vec3
shade_vertex(vec3 world_normal)
{
	vec3 illumination = lighting.ambient_count.rgb;
	if (surface.base_mode.w > 1.5)
		illumination = vec3(1.0);
	else {
		for (int i = 0; i < int(lighting.ambient_count.w); i++) {
			Light light = lighting.lights[i];
			float diffuse = dot(world_normal, light.direction.xyz);
			if (diffuse <= 0.0) continue;
			float highlight = 0.0;
			if (light.direction.w > 0.5) {
				vec3 reflected = 2.0 * diffuse * world_normal - light.direction.xyz;
				float alignment = dot(reflected, light.view.xyz);
				if (alignment > 0.5) {
					float square = alignment * alignment;
					highlight = square * square * lighting.brightness_specular.w;
				}
			}
			illumination += (1.5 * diffuse + highlight) * light.color.rgb;
		}
	}
	return clamp(clamp(surface.base_mode.rgb * lighting.brightness_specular.rgb *
		illumination, 0.0, 1.0) * lighting.tint.rgb, 0.0, 1.0);
}

vec2
reflection_coordinates(vec4 clip_position)
{
	vec2 screen = clip_position.xy / max(clip_position.w, 0.000001);
	screen.y *= camera.viewport.x;
	vec2 view_normal = (camera.view * vec4(normal, 0.0)).xy;
	return clamp(vec2(0.5) + 0.2495 * (view_normal + screen), 0.0, 1.0);
}

vec4
water_position(vec4 clip_position)
{
	if (!(camera.water.w > 0.5 && clip_position.w > 0.000001))
		return clip_position;
	float dy = -(1.0 + clip_position.y / clip_position.w) * camera.viewport.z * 0.5;
	float wave = sin(dy * camera.water.z + camera.water.x) * 10.0;
	clip_position.x = clip_position.x * camera.water.y +
		(1.0 - camera.water.y + 2.0 * wave / camera.viewport.y) * clip_position.w;
	clip_position.y = clip_position.y * camera.water.y +
		(camera.water.y - 1.0) * clip_position.w;
	return clip_position;
}

void
main()
{
	gl_Position = camera.matrix * vec4(position, 1.0);
	if (projection_w > 0.0)
		gl_Position *= projection_w;
	vertex_normal = (camera.model * vec4(normal, 0.0)).xyz;
	vertex_uv = material.options.x > 0.5 ? reflection_coordinates(gl_Position) : uv;
	gl_Position = water_position(gl_Position);

	/* Weight then divide in the fragment stage for screen-linear RGB/fog. */
	vertex_color_w = shade_vertex(vertex_normal) * gl_Position.w;
	vertex_prelit = 1.0;
	vertex_opacity_w = (material.distance_pass.w > 0.5 ? opacity : 1.0) * gl_Position.w;
	flat_color = color;
	flat_shading = material.distance_pass.z;
	float depth = -(camera.view * vec4(position, 1.0)).z * camera.fog.w;
	float visibility = depth < 0.2 ? 1.0 : clamp((0.7 - depth) * 2.0, 0.0, 1.0);
	vertex_fog_w = material.options.z * (1.0 - floor(visibility * 255.0) / 255.0) * gl_Position.w;
	background_amount_w = floor(clamp(max(depth - 0.8, 0.0) * 7.0, 0.0, 1.0) * 255.0) / 255.0 * gl_Position.w;
	fog_color = camera.fog.rgb;
	vertex_alpha_test = material.options.w;
	distant_face = material.distance_pass.x;
	powervr_fog = vec2(0.0, material.options.z);
}
