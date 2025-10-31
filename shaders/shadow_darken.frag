#version 450

// Shadow darkening fragment shader - darkens pixels where stencil > 0

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    // This pass runs with stencil test enabled
    // Only fragments where stencil > 0 (in shadow) will be processed
    // We output a dark multiply color to darken the scene
    
    // Dark shadow color (not fully black to allow ambient light)
    float shadowDarkness = 0.3; // 70% darker
    outColor = vec4(vec3(shadowDarkness), 1.0);
}

