#version 450 core

layout (location= 0) in vec3 pos_in;
layout (location= 1) in vec3 normal_in;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 view;
    mat4 normal;
    mat4 model;
    mat4 projection_clip;
    float cam_near;
    float cam_far;
} ubo_in;

layout(set = 1, binding = 0) uniform sampler2D depth_tex;

layout (location = 0) out vec4 glpos_out;
layout (location = 1) out vec3 normal_out;
layout (location = 2) out uint is_closer_out;

vec2 get_attachment_uv(vec4 glpos)
{
    return glpos.xy / glpos.w * .5f + .5f;
}

float read_depth_attachment(float pixel)
{
    // return view depth
    return - ubo_in.cam_near * ubo_in.cam_far /
	( ( ubo_in.cam_far - ubo_in.cam_near ) * pixel - ubo_in.cam_far );
}

void main(void)
{
    vec4 view_pos = ubo_in.view * ubo_in.model * vec4(pos_in, 1.f);
    glpos_out = ubo_in.projection_clip * view_pos;
    normal_out = (ubo_in.normal * vec4(normal_in, 1.f)).xyz;

    vec2 uv = get_attachment_uv(glpos_out);
    float scene_depth = read_depth_attachment(texture(depth_tex, uv).r);

    // return 1 if view_depth is smaller than scene_depth
    is_closer_out = uint(step(-view_pos.z, scene_depth));
}
