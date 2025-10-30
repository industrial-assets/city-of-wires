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
    // Sample the neon texture array using vUV and vTexIndex
    int idx = max(0, vTexIndex);
    vec2 uv = clamp(vUV, 0.0, 1.0);
    vec4 tex = texture(neonTex, vec3(uv, float(idx)));

    // Base color from vertex, modulated by intensity and texture
    float pulse = calculatePulse(ubo.time);
    vec3 base = vColor * vIntensity * pulse;
    vec3 color = base * tex.rgb;

    // Alpha from texture with slight distance falloff
    float distance = length(vWorldPos - ubo.cameraPos);
    float distanceAlpha = 1.0 / (1.0 + distance * 0.02);
    float alpha = tex.a * distanceAlpha;

    outColor = vec4(color, alpha);
}
