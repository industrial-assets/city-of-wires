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
            // sunScreenPos.y = 1.0 - sunScreenPos.y; // Invert vertical movement
            
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

// Volumetric light shafts (god rays)
vec3 renderLightShafts(vec2 uv) {
    if (ppUBO.lightShaftIntensity <= 0.0) return vec3(0.0);
    
    // Project sun position to screen space
    vec4 sunViewPos = ppUBO.view * vec4(-ppUBO.sunWorldDir * 1000.0, 1.0);
    vec4 sunClipPos = ppUBO.proj * sunViewPos;
    
    if (sunClipPos.w > 0.0) {
        vec3 sunNDC = sunClipPos.xyz / sunClipPos.w;
        vec2 sunScreenPos = sunNDC.xy * 0.5 + 0.5;
        
        // Only compute light shafts if sun is somewhat visible on screen
        if (sunScreenPos.x >= -0.3 && sunScreenPos.x <= 1.3 && 
            sunScreenPos.y >= -0.3 && sunScreenPos.y <= 1.3) {
            
            // March from current pixel toward sun
            vec2 rayDir = sunScreenPos - uv;
            float rayLength = length(rayDir);
            rayDir = normalize(rayDir);
            
            // Number of samples along the ray
            int numSamples = int(ppUBO.lightShaftDensity * 32.0);
            numSamples = clamp(numSamples, 8, 64);
            
            float stepSize = rayLength / float(numSamples);
            vec2 stepVec = rayDir * stepSize;
            
            // Accumulate light along the ray
            float accumulation = 0.0;
            vec2 marchPos = uv;
            
            for (int i = 0; i < numSamples; i++) {
                marchPos += stepVec;
                
                // Check if we're out of bounds
                if (marchPos.x < 0.0 || marchPos.x > 1.0 || 
                    marchPos.y < 0.0 || marchPos.y > 1.0) {
                    break;
                }
                
                // Sample depth at this position
                float depth = texture(depthTexture, marchPos).r;
                
                // If depth is far (close to 1.0), light can pass through
                // If depth is near (close to 0.0), geometry blocks the light
                // This creates the volumetric effect
                // Relaxed threshold so more light gets through
                float depthFactor = smoothstep(0.99, 1.0, depth);
                
                // Accumulate light (exponential falloff with distance)
                float distFactor = 1.0 - (float(i) / float(numSamples));
                accumulation += depthFactor * distFactor;
            }
            
            // Normalize and apply intensity
            accumulation /= float(numSamples);
            accumulation *= ppUBO.lightShaftIntensity;
            
            // Fade based on distance from sun (wider spread for more visible effect)
            float sunDist = distance(uv, sunScreenPos);
            float distanceFade = 1.0 - smoothstep(0.0, 1.0, sunDist);
            accumulation *= distanceFade;
            
            // Add constant atmospheric glow for more visible effect
            accumulation += 0.1 * ppUBO.lightShaftIntensity * (1.0 - sunDist);
            
            // Teal volumetric light color (cool atmospheric scattering)
            vec3 shaftColor = vec3(0.4, 0.9, 0.95) * accumulation;
            
            return shaftColor;
        }
    }
    
    return vec3(0.0);
}

void main() {
    // Sample HDR texture
    vec4 hdrColor = texture(hdrTexture, vUV);
    
    // Render volumetric light shafts
    vec3 lightShafts = renderLightShafts(vUV);
    
    // Render visible sun disk (uses sun direction from UBO)
    vec3 sunDiskColor = renderSunDisk(vUV);
    
    // Combine HDR with light shafts and sun disk
    vec3 finalColor = hdrColor.rgb + lightShafts + sunDiskColor;
    
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
