#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D debugFontSampler;

void main() {
    float alpha = texture(debugFontSampler, fragTexCoord).r;
    outColor = vec4(fragColor, alpha);
}
