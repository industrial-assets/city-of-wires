#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inColor;
layout(location=2) in float inIntensity;
layout(location=3) in vec2 inUV;
layout(location=4) in float inTexIndex;

layout(location=0) out vec3 vColor;
layout(location=1) out float vIntensity;
layout(location=2) out vec3 vWorldPos;
layout(location=3) out vec2 vUV;
layout(location=4) flat out int vTexIndex;

layout(set=0, binding=0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float time;
    vec3 fogColor;
    float fogDensity;
    vec3 skyLightDir;
    float skyLightIntensity;
} ubo;

void main() {
    vColor = inColor;
    vIntensity = inIntensity;
    vWorldPos = (ubo.model * vec4(inPos, 1.0)).xyz;
    vUV = inUV;
    vTexIndex = int(inTexIndex + 0.5);
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPos, 1.0);
}
