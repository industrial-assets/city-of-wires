#version 450

layout(location=0) in vec3 vColor;
layout(location=1) in vec2 vUV;
layout(location=2) in vec3 vWorldPos;
layout(location=3) flat in int vTexIndex;
layout(location=4) in vec3 vNormal;

layout(location=0) out vec4 outColor;

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

const int MAX_BUILDING_TEXTURES = 8;
layout(set=0, binding=1) uniform sampler2D buildingTextures[MAX_BUILDING_TEXTURES];
layout(set=0, binding=3) uniform sampler2D shadowMap;

// Calculate shadow factor using percentage-closer filtering
float calculateShadow(vec3 worldPos) {
    // Project world position to light space
    vec4 lightSpacePos = ubo.lightSpaceMatrix * vec4(worldPos, 1.0);
    
    // Perform perspective divide
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    
    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;
    
    // Check if position is outside light frustum
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 0.3; // Outside shadow map, assume some ambient
    }
    
    // Get closest depth value from light's perspective
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    
    // Get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    
    // Simple shadow check with bias to prevent shadow acne
    float bias = 0.005;
    float shadow = currentDepth - bias > closestDepth ? 0.3 : 1.0;
    
    // PCF for softer shadows
    float shadowSum = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float pcfDepth = texture(shadowMap, projCoords.xy + offset).r;
            shadowSum += (currentDepth - bias > pcfDepth) ? 0.3 : 1.0;
        }
    }
    shadow = shadowSum / 9.0;
    
    return shadow;
}

// Enhanced fog calculation with height variation
float calculateFog(vec3 worldPos, vec3 cameraPos) {
    float distance = length(worldPos - cameraPos);
    
    // Height-based fog density - denser at ground level
    float heightFactor = 1.0 - clamp((worldPos.y - cameraPos.y) * 0.005, 0.0, 1.0);
    float heightDensity = ubo.fogDensity * (1.0 + heightFactor * 1.5);
    
    // Distance-based fog with height variation
    float fogFactor = exp(-heightDensity * distance);
    
    return clamp(fogFactor, 0.0, 1.0);
}

// Enhanced volumetric lighting with light shafts through smog
vec3 calculateVolumetricLight(vec3 worldPos, vec3 cameraPos) {
    vec3 lightDir = normalize(ubo.skyLightDir);
    vec3 viewDir = normalize(cameraPos - worldPos);
    
    // Calculate light shaft intensity - stronger when looking towards light
    float shaftFactor = max(0.0, dot(viewDir, -lightDir));
    
    // Sharper falloff for more dramatic light shafts
    shaftFactor = pow(shaftFactor, 2.5);
    
    // Distance attenuation - light shafts fade with distance
    float distance = length(worldPos - cameraPos);
    float distanceAttenuation = 1.0 / (1.0 + distance * 0.01);
    
    // Height-based density - more visible near ground where smog is denser
    float heightFactor = 1.0 - clamp((worldPos.y - cameraPos.y) * 0.003, 0.0, 1.0);
    
    // Combine factors
    vec3 shaftColor = ubo.fogColor * shaftFactor * distanceAttenuation * heightFactor;
    return shaftColor * ubo.skyLightIntensity * 0.8;
}

void main() {
    // Sample texture; UVs are already scaled in geometry creation
    vec2 uv = vUV * ubo.texTiling;
    int idx = vTexIndex % MAX_BUILDING_TEXTURES;
    vec3 textureColor = texture(buildingTextures[idx], uv).rgb;
    
    // Mix texture with building color for variation
    vec3 baseColor = mix(vColor, textureColor, 0.7);
    
    // Calculate directional lighting
    vec3 lightDir = normalize(-ubo.skyLightDir);
    vec3 normal = normalize(vNormal);
    
    // Ambient + diffuse lighting
    float ambientStrength = 0.25; // Dim ambient light through pollution
    float diffuseStrength = max(dot(normal, lightDir), 0.0);
    
    // Apply shadow
    float shadow = calculateShadow(vWorldPos);
    
    // Combine lighting
    vec3 ambient = baseColor * ambientStrength;
    vec3 diffuse = baseColor * diffuseStrength * ubo.skyLightIntensity * shadow;
    
    vec3 litColor = ambient + diffuse;
    
    // Calculate fog
    float fogFactor = calculateFog(vWorldPos, ubo.cameraPos);
    
    // Calculate volumetric lighting (light shafts)
    vec3 volumetricLight = calculateVolumetricLight(vWorldPos, ubo.cameraPos);
    
    // Mix lit color with fog
    vec3 finalColor = mix(ubo.fogColor, litColor, fogFactor);
    
    // Add volumetric light shafts
    finalColor += volumetricLight;
    
    outColor = vec4(finalColor, 1.0);
}
