#version 450

layout(location = 0) in vec3 probe;

layout(set = 1, binding = 0) uniform Parameters
{
	vec4 size;
} parameters;

layout(location = 0) flat out vec3 query;

void
main()
{
	int column = gl_VertexIndex % int(parameters.size.x);
	int row = gl_VertexIndex / int(parameters.size.x);
	vec2 pixel = vec2(column, row) + vec2(0.5);

	gl_Position = vec4(pixel.x * 2.0 / parameters.size.x - 1.0,
		1.0 - pixel.y * 2.0 / parameters.size.y, 0.0, 1.0);
	gl_PointSize = 1.0;
	query = vec3(int(probe.x), parameters.size.z - 1.0 - int(probe.y), probe.z);
}
