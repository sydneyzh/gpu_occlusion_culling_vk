#version 450 core

const vec3 LIGHT_DIR = vec3(.25f, .74f, .62f);
layout (location = 0) in vec3 normal_in;
layout (location = 1) in vec2 uv_in;
layout (location = 2) flat in int mtl_idx;

struct Mtl_props {
    vec4 tex_indices;
    vec3 diffuse;
    float opacity;
    vec3 specular;
    float specular_exponent;
    vec3 emmisive;
    float padding;
};

layout (set = 1, binding = 0) readonly buffer Mtl_buffer{
    Mtl_props props[];
};

layout (set = 1, binding = 1) uniform sampler2D mtl_textures[2];

layout(location = 0) out vec4 frag_color;

void main(void)
{
    int tex_idx = int(props[mtl_idx].tex_indices.x);
    float is_sky = step(mtl_idx, 1.f);
    float lambertian = (1.f - is_sky) * max(.45f, dot(LIGHT_DIR, normal_in)) + is_sky;
    vec3 diffuse = props[mtl_idx].diffuse * lambertian * texture(mtl_textures[tex_idx], uv_in).xyz;
    frag_color.rgb = diffuse;
    frag_color.a = 1.f;
}
