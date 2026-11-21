#version 450

layout(location = 0) flat in vec3 query;
layout(set = 2, binding = 0) uniform sampler2D depth_image;
layout(location = 0) out vec4 output_color;

void
main()
{
	float sampled = texelFetch(depth_image, ivec2(query.xy), 0).r;
	float visible = query.z < sampled ? 1.0 : 0.0;

	output_color = vec4(visible, 0.0, 0.0, 1.0);
}
