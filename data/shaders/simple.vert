#version 450 core

layout (location= 0) in vec3 pos_in;
layout (location= 1) in vec3 normal_in;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 model;
    mat4 normal;
    mat4 view;
    mat4 projection_clip;
    float cam_near;
    float cam_far;
} ubo_in;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout (location = 0) out vec3 normal_out;

void main(void)
{
    gl_Position = ubo_in.projection_clip * ubo_in.view * ubo_in.model * vec4(pos_in, 1.f);
    normal_out = (ubo_in.normal * vec4(normal_in, 1.f)).xyz;
}
