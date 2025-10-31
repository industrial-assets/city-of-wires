#version 450

layout(location=0) in vec2 vUV;
layout(location=0) out vec4 outColor;

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

// Render a visible sun disk
vec3 renderSunDisk(vec2 uv, vec3 sunDir, mat4 view, mat4 proj) {
    // Normalize sun direction (it points towards the sun)
    vec3 sunWorldDir = normalize(sunDir);
    
    // Project sun direction to screen space
    // Convert world direction to view space, then to clip space
    vec4 sunViewDir = view * vec4(sunWorldDir, 0.0);
    vec4 sunClipDir = proj * sunViewDir;
    
    // Normalize to NDC, then convert to screen space [0,1]
    if (abs(sunClipDir.w) < 0.001) {
        return vec3(0.0); // Sun is at infinity
    }
    
    vec2 sunNDC = sunClipDir.xy / sunClipDir.w;
    vec2 sunScreenPos = sunNDC * 0.5 + 0.5;
    sunScreenPos.y = 1.0 - sunScreenPos.y; // Flip Y for screen coordinates
    
    // Distance from sun in screen space
    float sunDist = distance(uv, sunScreenPos);
    
    // Sun disk size
    float sunRadius = 0.03; // Size of the sun
    float sunCore = 0.015; // Bright core
    
    // Create sun disk with soft edges
    float sunFactor = 1.0 - smoothstep(sunCore, sunRadius, sunDist);
    
    // Only visible when sun is in front of camera (positive Z in view space)
    // Check if sun is in front by checking if it projects to a reasonable screen position
    if (sunScreenPos.x >= -0.2 && sunScreenPos.x <= 1.2 && 
        sunScreenPos.y >= -0.2 && sunScreenPos.y <= 1.2 &&
        sunViewDir.z < 0.0) { // Sun is in front of camera
        // Warm sun color (dimmed through pollution)
        vec3 sunColor = vec3(1.0, 0.9, 0.7) * 3.0;
        sunColor *= sunFactor;
        
        // Add lens flare effect when looking near the sun
        float flareDist = distance(uv, sunScreenPos);
        float flareFactor = 1.0 - smoothstep(0.0, 0.4, flareDist);
        flareFactor = pow(flareFactor, 3.0) * 0.4;
        sunColor += vec3(1.0, 0.95, 0.8) * flareFactor;
        
        return sunColor;
    }
    
    return vec3(0.0);
}

void main() {
    // Render sun disk
    vec3 sunWorldDir = normalize(ubo.skyLightDir);
    vec3 sunDiskColor = renderSunDisk(vUV, sunWorldDir, ubo.view, ubo.proj);
    
    // Output with additive blending (alpha = 1 for additive)
    outColor = vec4(sunDiskColor, 1.0);
}

