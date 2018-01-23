#version 450 core

layout (location= 0) in vec3 pos_in;
layout (location= 1) in vec3 normal_in;
layout (location= 2) in vec2 uv_in;

layout (location= 3) in vec4 tc0;
layout (location= 4) in vec4 tc1;
layout (location= 5) in vec4 tc2;
layout (location= 6) in vec4 tc3;
layout (location= 7) in float mtl_idx;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 model;
    mat4 normal;
    mat4 view;
    mat4 projection_clip;
    float cam_near;
    float cam_far;
    vec2 resolution;
} ubo_in;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout (location = 0) out vec3 normal_out;
layout (location = 1) out vec2 uv_out;
layout (location = 2) out int mtl_idx_out;

void main(void)
{
    mat4 inst_transform = mat4(tc0, tc1, tc2, tc3);
    mat4 inst_normal = transpose(inverse(inst_transform));
    gl_Position = ubo_in.projection_clip * ubo_in.view * ubo_in.model * inst_transform * vec4(pos_in, 1.f);
    normal_out = normalize((ubo_in.normal * inst_normal * vec4(normal_in, 1.f)).xyz);
    uv_out = uv_in;
    mtl_idx_out = int(mtl_idx);
}
