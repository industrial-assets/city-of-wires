#version 450

layout(location=0) in vec2 vUV;

layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform PostProcessingUBO {
    float exposure;
    float bloomThreshold;
    float bloomIntensity;
    float fogHeightFalloff;
    float fogHeightOffset;
    float vignetteStrength;
    float vignetteRadius;
    float grainStrength;
    float contrast;
    float saturation;
    float colorTemperature;
    float lightShaftIntensity;
    float lightShaftDensity;
    mat4 view;  // View matrix for sun projection
    mat4 proj;  // Projection matrix for sun projection
    vec3 sunWorldDir;  // World-space sun direction (normalized, points towards sun)
} ppUBO;

layout(set=0, binding=1) uniform sampler2D hdrTexture;
layout(set=0, binding=2) uniform sampler2D bloomTexture;
layout(set=0, binding=3) uniform sampler2D depthTexture;

// Tone mapping (Reinhard)
vec3 toneMapping(vec3 color) {
    return color / (color + vec3(1.0));
}

// Color grading
vec3 colorGrading(vec3 color) {
    // Adjust contrast
    color = (color - 0.5) * ppUBO.contrast + 0.5;
    
    // Adjust saturation
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, ppUBO.saturation);
    
    // Color temperature adjustment (cooler for dystopian look)
    vec3 warm = vec3(1.0, 0.8, 0.6);
    vec3 cool = vec3(0.6, 0.8, 1.0);
    vec3 temperature = mix(warm, cool, ppUBO.colorTemperature);
    color *= temperature;
    
    return color;
}

// Vignette effect
float vignette(vec2 uv) {
    vec2 center = vec2(0.5);
    float dist = distance(uv, center);
    return 1.0 - smoothstep(ppUBO.vignetteRadius, 1.0, dist) * ppUBO.vignetteStrength;
}

// Film grain
float grain(vec2 uv, float time) {
    return fract(sin(dot(uv + time, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
}

// Render a visible sun disk that blooms and saturates
// For an infinite sun, we project the world-space direction directly
vec3 renderSunDisk(vec2 uv) {
    // Use the sun world-space direction from UBO (points towards sun from scene)
    // Fallback to hardcoded direction if UBO is not set correctly for debugging
    vec3 sunWorldDir = normalize(ppUBO.sunWorldDir);
    // Debug: if sun direction is zero/uninitialized, use a default
    if (length(ppUBO.sunWorldDir) < 0.1) {
        sunWorldDir = normalize(vec3(0.2, -0.8, 0.3));
    }
    
    // Transform sun direction to view space (direction vectors use w=0)
    vec3 sunViewDir = normalize((ppUBO.view * vec4(sunWorldDir, 0.0)).xyz);
    
    // In view space with glm::lookAt, camera looks down -Z, so forward is negative Z
    // Only render if sun is in front of camera (z < 0 means forward)
    if (sunViewDir.z < 0.0) {
        // For an infinite sun, project a point very far away along the direction
        // Use the far plane distance (matches projection matrix far plane = 1000.0)
        float farPlane = 1000.0;
        
        // Calculate position in view space along the sun direction
        // Since sunViewDir.z < 0 means it's pointing forward (-Z direction),
        // multiplying by farPlane gives us a position with z = negative value (in front)
        vec3 sunViewPos = sunViewDir * farPlane;
        
        // Project to clip space
        vec4 sunClipSpace = ppUBO.proj * vec4(sunViewPos, 1.0);
        
        // Check if sun is in front of camera and within view frustum
        if (sunClipSpace.w > 0.0) {
            // Convert to NDC [-1,1]
            vec2 sunNDC = sunClipSpace.xy / sunClipSpace.w;
            
            // Convert NDC [-1,1] to screen space [0,1]
            vec2 sunScreenPos = sunNDC * 0.5 + 0.5;
            sunScreenPos.y = 1.0 - sunScreenPos.y; // Flip Y for screen coordinates
            
            // Distance from sun in screen space
            float sunDist = distance(uv, sunScreenPos);
            
            // Sun disk size and intensity
            float sunRadius = 0.05; // Size of the sun
            float sunCore = 0.02; // Bright core
            
            // Create sun disk with soft edges
            float sunFactor = 1.0 - smoothstep(sunCore, sunRadius, sunDist);
            
            // Only visible when sun projects to screen space
            if (sunScreenPos.x >= -0.5 && sunScreenPos.x <= 1.5 && 
                sunScreenPos.y >= -0.5 && sunScreenPos.y <= 1.5) {
                
                // Check if sun is occluded by geometry
                // Sample depth at sun position
                float sceneDepth = texture(depthTexture, sunScreenPos).r;
                
                // Calculate expected depth for sun (at far distance, should be near 1.0)
                // If there's geometry closer (depth < ~0.99), hide the sun
                float sunDepthThreshold = 0.99;
                if (sceneDepth < sunDepthThreshold) {
                    // Sun is occluded by geometry
                    return vec3(0.0);
                }
                
                // Warm sun color (dimmed through pollution)
                vec3 sunColor = vec3(1.0, 0.9, 0.7) * 2.5;
                sunColor *= sunFactor;
                
                // Add lens flare effect when looking near the sun
                float flareDist = distance(uv, sunScreenPos);
                float flareFactor = 1.0 - smoothstep(0.0, 0.3, flareDist);
                flareFactor = pow(flareFactor, 3.0) * 0.3;
                sunColor += vec3(1.0, 0.95, 0.8) * flareFactor;
                
                return sunColor;
            }
        }
    }
    
    return vec3(0.0);
}

void main() {
    // Sample HDR texture
    vec4 hdrColor = texture(hdrTexture, vUV);
    
    // Render visible sun disk (uses sun direction from UBO)
    vec3 sunDiskColor = renderSunDisk(vUV);
    
    // Combine HDR with sun disk
    vec3 finalColor = hdrColor.rgb + sunDiskColor;
    
    // Apply exposure (clamp to reasonable range to avoid issues)
    float exposure = clamp(ppUBO.exposure, 0.1, 10.0);
    finalColor *= exposure;
    
    // Tone mapping
    finalColor = toneMapping(finalColor);
    
    // Apply color grading
    finalColor = colorGrading(finalColor);
    
    // Apply vignette (less aggressive)
    float vignetteFactor = vignette(vUV);
    finalColor = mix(finalColor * 0.95, finalColor, vignetteFactor);
    
    // Add film grain
    float grainValue = grain(vUV, 0.0) * ppUBO.grainStrength;
    finalColor += vec3(grainValue);
    
    // Clamp to valid range
    finalColor = clamp(finalColor, 0.0, 1.0);
    
    outColor = vec4(finalColor, 1.0);
}
