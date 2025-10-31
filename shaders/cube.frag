#version 450

layout(location=0) in vec3 vColor;
layout(location=0) out vec4 outColor;

void main() {
    // Hot reload test - make colors more vibrant
    vec3 color = vColor * 1.5;
    outColor = vec4(color, 1.0);
}


