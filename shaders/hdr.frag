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

layout(set=0, binding=1) uniform sampler2D buildingTexture;

// Enhanced fog calculation with height-based density
float calculateEnhancedFog(vec3 worldPos, vec3 cameraPos) {
    float distance = length(worldPos - cameraPos);
    
    // Height-based fog density
    float heightFactor = 1.0 - clamp((worldPos.y - ubo.cameraPos.y) * 0.01, 0.0, 1.0);
    float heightDensity = ubo.fogDensity * (1.0 + heightFactor * 2.0);
    
    // Distance-based fog
    float fogFactor = exp(-heightDensity * distance);
    
    // Add some noise for more realistic fog
    float noise = sin(worldPos.x * 0.1 + worldPos.z * 0.1 + ubo.time * 0.5) * 0.1;
    fogFactor += noise * 0.1;
    
    return clamp(fogFactor, 0.0, 1.0);
}

// Enhanced volumetric lighting with light shafts
vec3 calculateLightShafts(vec3 worldPos, vec3 cameraPos) {
    vec3 lightDir = normalize(ubo.skyLightDir);
    vec3 viewDir = normalize(cameraPos - worldPos);
    
    // Calculate light shaft intensity
    float shaftFactor = max(0.0, dot(viewDir, lightDir));
    shaftFactor = pow(shaftFactor, 8.0); // Sharp falloff for dramatic shafts
    
    // Distance attenuation
    float distance = length(worldPos - cameraPos);
    float distanceAttenuation = 1.0 / (1.0 + distance * 0.01);
    
    // Height-based intensity (stronger near ground)
    float heightFactor = 1.0 - clamp(worldPos.y * 0.01, 0.0, 1.0);
    
    vec3 shaftColor = ubo.fogColor * shaftFactor * distanceAttenuation * heightFactor;
    return shaftColor * ubo.skyLightIntensity * 0.5;
}

// Atmospheric scattering
vec3 calculateAtmosphericScattering(vec3 worldPos, vec3 cameraPos) {
    vec3 lightDir = normalize(ubo.skyLightDir);
    vec3 viewDir = normalize(cameraPos - worldPos);
    
    // Rayleigh scattering (blue/green tint)
    float cosAngle = dot(viewDir, lightDir);
    float rayleigh = 1.0 + cosAngle * cosAngle;
    
    // Mie scattering (forward scattering)
    float mie = (1.0 - 0.9 * 0.9) / (4.0 * 3.14159 * pow(1.0 + 0.9 * 0.9 - 2.0 * 0.9 * cosAngle, 1.5));
    
    vec3 scattering = vec3(0.1, 0.3, 0.2) * rayleigh + vec3(0.2, 0.1, 0.05) * mie;
    return scattering * ubo.skyLightIntensity * 0.3;
}

void main() {
    // Sample texture
    vec3 textureColor = texture(buildingTexture, vUV).rgb;
    
    // Mix texture with building color for variation
    vec3 baseColor = mix(vec3(0.3, 0.3, 0.3), textureColor, 0.7);
    
    // Add some subtle lighting variation
    float lightingVariation = 0.8 + 0.2 * sin(vUV.x * 10.0 + ubo.time * 0.5);
    baseColor *= lightingVariation;
    
    // Calculate enhanced fog
    vec3 worldPos = vec3(vUV.x * 100.0 - 50.0, 0.0, vUV.y * 100.0 - 50.0);
    float fogFactor = calculateEnhancedFog(worldPos, ubo.cameraPos);
    
    // Calculate light shafts
    vec3 lightShafts = calculateLightShafts(worldPos, ubo.cameraPos);
    
    // Calculate atmospheric scattering
    vec3 scattering = calculateAtmosphericScattering(worldPos, ubo.cameraPos);
    
    // Mix base color with fog and atmospheric effects
    vec3 finalColor = mix(ubo.fogColor, baseColor, fogFactor);
    finalColor += lightShafts;
    finalColor += scattering;
    
    // Apply HDR exposure
    finalColor = finalColor * 2.0; // Boost for HDR
    
    outColor = vec4(finalColor, 1.0);
}
