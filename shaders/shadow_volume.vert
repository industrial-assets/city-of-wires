#version 450

// Shadow volume vertex shader - transforms shadow volume geometry to clip space

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float time;
    vec3 fogColor;
    float fogStart;
    float fogEnd;
    float fogHeight;
    float sunIntensity;
    float ambientIntensity;
    vec3 sunDirection;
    float shadowBias;
    vec3 sunColor;
    mat4 lightSpaceMatrix;
} ubo;

layout(location = 0) in vec3 inPosition;

void main() {
    // Transform shadow volume vertex to clip space
    // Shadow volumes are already extruded in world space,
    // we just need to transform them
    gl_Position = ubo.proj * ubo.view * vec4(inPosition, 1.0);
}
