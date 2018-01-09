#version 450 core

const vec3 LIGHT_DIR = normalize(vec3(.2f, .6f, .1f));

layout (location = 0) in vec3 normal_in;

layout(location = 0) out vec4 frag_color;

void main(void)
{
    frag_color.rgb = vec3(max(0.f, dot(LIGHT_DIR, normal_in)));
    frag_color.a = 1.f;
}
