#version 450

layout(location=0) in vec3 vColor;
layout(location=1) in float vIntensity;
layout(location=2) in vec3 vWorldPos;
layout(location=3) in vec2 vUV;
layout(location=4) flat in int vTexIndex;
layout(set=0, binding=2) uniform sampler2DArray neonTex;

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

float calculateBloom(vec3 worldPos, vec3 cameraPos) {
    float distance = length(worldPos - cameraPos);
    // Reduce bloom effect - make it more subtle and distance-based
    float bloomFactor = 1.0 / (1.0 + distance * 0.05);
    return min(bloomFactor, 1.2); // Cap maximum bloom
}

float calculatePulse(float time) {
    return 0.9 + 0.1 * sin(time * 2.0); // More subtle pulsing
}

void main() {
    int idx = max(0, vTexIndex);
    vec4 tex = texture(neonTex, vec3(vUV, float(idx)));
    
    // Use texture pattern more prominently
    vec3 baseNeonColor = vColor * vIntensity * 0.7; // Reduce base intensity
    float pulse = calculatePulse(ubo.time);
    
    // Mix texture with color more evenly
    vec3 neonColor = mix(baseNeonColor, baseNeonColor * tex.rgb * 1.2, 0.6);
    neonColor *= pulse;
    
    // Sharper cutout - less volumetric glow
    float cut = step(0.1, max(max(tex.r, tex.g), tex.b)); // Higher threshold for cleaner edges
    float distance = length(vWorldPos - ubo.cameraPos);
    float distanceAlpha = 1.0 / (1.0 + distance * 0.02); // Subtle distance fade
    
    // Reduce alpha contribution from bloom - make them more visible as flat planes
    float alpha = cut * tex.a * (0.8 + distanceAlpha * 0.2);
    
    // Add slight glow only at edges, not entire quad
    float edgeGlow = 1.0 - smoothstep(0.3, 1.0, max(abs(vUV.x - 0.5), abs(vUV.y - 0.5))) * 2.0;
    alpha = mix(alpha, alpha * 1.15, edgeGlow * 0.3);
    
    vec3 finalColor = neonColor;
    outColor = vec4(finalColor, alpha);
}
