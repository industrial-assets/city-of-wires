#version 450

layout(location=0) in vec2 vUV;

layout(location=0) out vec4 outColor;

layout(set=0, binding=0) uniform BloomUBO {
    float threshold;
    float intensity;
    float radius;
} bloomUBO;

layout(set=0, binding=1) uniform sampler2D hdrTexture;

// Gaussian blur kernel
vec4 gaussianBlur(sampler2D tex, vec2 uv, vec2 texelSize, float radius) {
    vec4 color = vec4(0.0);
    float totalWeight = 0.0;
    
    for(int x = -2; x <= 2; x++) {
        for(int y = -2; y <= 2; y++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize * radius;
            float weight = exp(-(x*x + y*y) / (2.0 * radius * radius));
            color += texture(tex, uv + offset) * weight;
            totalWeight += weight;
        }
    }
    
    return color / totalWeight;
}

void main() {
    vec2 texelSize = 1.0 / textureSize(hdrTexture, 0);
    
    // Sample HDR texture
    vec4 hdrColor = texture(hdrTexture, vUV);
    
    // Extract bright areas above threshold
    float brightness = dot(hdrColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec4 bloom = hdrColor * smoothstep(bloomUBO.threshold, bloomUBO.threshold + 0.1, brightness);
    
    // Apply Gaussian blur for bloom effect
    bloom = gaussianBlur(hdrTexture, vUV, texelSize, bloomUBO.radius);
    
    // Apply intensity
    bloom.rgb *= bloomUBO.intensity;
    
    outColor = bloom;
}
