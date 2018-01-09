#version 450 core

layout(triangles) in;

layout(location = 0) in vec4 glpos_in[];
layout(location = 1) in vec3 normal_in[];
layout(location = 2) in uint is_closer_in[];

layout(triangle_strip, max_vertices = 3) out;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout(location = 0) out vec3 normal_out;

void main()
{
    bool to_emit = bool(is_closer_in[0] | is_closer_in[1] | is_closer_in[2]);
    if (to_emit) {
	gl_Position = glpos_in[0];
	normal_out = normal_in[0];
	EmitVertex();

	gl_Position = glpos_in[1];
	normal_out = normal_in[1];
	EmitVertex();

	gl_Position = glpos_in[2];
	normal_out = normal_in[2];
	EmitVertex();
    }
}
