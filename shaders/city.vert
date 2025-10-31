#version 450

layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inColor;
layout(location=2) in vec2 inUV;
layout(location=3) in float inTexIndex;
layout(location=4) in vec3 inNormal;

layout(location=0) out vec3 vColor;
layout(location=1) out vec2 vUV;
layout(location=2) out vec3 vWorldPos;
layout(location=3) flat out int vTexIndex;
layout(location=4) out vec3 vNormal;

layout(set=0, binding=0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 cameraPos;
    float time;
    vec3 fogColor;
    float fogDensity;
    vec3 skyLightDir;
    float skyLightIntensity;
    float texTiling;
    float textureCount;
} ubo;

void main() {
    vColor = inColor;
    vUV = inUV;
    vWorldPos = (ubo.model * vec4(inPos, 1.0)).xyz;
    // Transform normal to world space (assuming no non-uniform scaling)
    vNormal = normalize((ubo.model * vec4(inNormal, 0.0)).xyz);
    vTexIndex = int(inTexIndex + 0.5);
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPos, 1.0);
}
