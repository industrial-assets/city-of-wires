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
} ppUBO;

layout(set=0, binding=1) uniform sampler2D hdrTexture;
layout(set=0, binding=2) uniform sampler2D bloomTexture;

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
vec3 renderSunDisk(vec2 uv, vec3 sunDir, vec3 cameraDir) {
    // Calculate sun position in screen space
    vec3 sunWorldDir = normalize(sunDir);
    
    // Project sun direction to screen space
    vec2 sunScreenPos = vec2(
        dot(vec3(1.0, 0.0, 0.0), sunWorldDir) * 0.5 + 0.5,
        dot(vec3(0.0, 1.0, 0.0), sunWorldDir) * 0.5 + 0.5
    );
    
    // Distance from sun in screen space
    float sunDist = distance(uv, sunScreenPos);
    
    // Sun disk size and intensity
    float sunRadius = 0.05; // Size of the sun
    float sunCore = 0.02; // Bright core
    
    // Create sun disk with soft edges
    float sunFactor = 1.0 - smoothstep(sunCore, sunRadius, sunDist);
    
    // Only visible when sun is above horizon or visible in sky
    // Check if sun direction projects to a reasonable screen position
    if (sunScreenPos.x >= -0.5 && sunScreenPos.x <= 1.5 && 
        sunScreenPos.y >= -0.5 && sunScreenPos.y <= 1.5) {
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
    
    return vec3(0.0);
}

void main() {
    // Sample HDR and bloom textures
    vec4 hdrColor = texture(hdrTexture, vUV);
    vec4 bloomColor = texture(bloomTexture, vUV);
    
    // Render visible sun disk
    vec3 sunWorldDir = normalize(vec3(0.2, -0.8, 0.3)); // Match skyLightDir from UBO
    vec3 cameraDir = vec3(0.0, 0.0, 1.0); // Assuming looking forward
    vec3 sunDiskColor = renderSunDisk(vUV, sunWorldDir, cameraDir);
    
    // Combine HDR, bloom, and sun disk
    vec3 finalColor = hdrColor.rgb + bloomColor.rgb * ppUBO.bloomIntensity;
    finalColor += sunDiskColor; // Sun disk is already HDR bright
    
    // Apply exposure
    finalColor *= ppUBO.exposure;
    
    // Tone mapping
    finalColor = toneMapping(finalColor);
    
    // Color grading (enhance saturation around sun)
    vec3 baseGraded = colorGrading(finalColor);
    
    // Increase saturation near sun position for atmospheric effect
    vec3 sunWorldDir2 = normalize(vec3(0.2, -0.8, 0.3));
    vec2 sunScreenPos = vec2(
        dot(vec3(1.0, 0.0, 0.0), sunWorldDir2) * 0.5 + 0.5,
        dot(vec3(0.0, 1.0, 0.0), sunWorldDir2) * 0.5 + 0.5
    );
    float sunInfluence = 1.0 - smoothstep(0.0, 0.4, distance(vUV, sunScreenPos));
    sunInfluence = pow(sunInfluence, 2.0);
    
    // Boost saturation near sun
    float luminance = dot(baseGraded, vec3(0.2126, 0.7152, 0.0722));
    vec3 saturated = mix(vec3(luminance), baseGraded, ppUBO.saturation + sunInfluence * 0.3);
    finalColor = mix(baseGraded, saturated, sunInfluence * 0.5);
    
    // Apply vignette
    float vignetteFactor = vignette(vUV);
    finalColor *= vignetteFactor;
    
    // Add film grain
    float grainValue = grain(vUV, 0.0) * ppUBO.grainStrength;
    finalColor += vec3(grainValue);
    
    // Clamp to valid range
    finalColor = clamp(finalColor, 0.0, 1.0);
    
    outColor = vec4(finalColor, 1.0);
}
