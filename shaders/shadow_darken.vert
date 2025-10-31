#version 450

// Full-screen quad vertex shader for shadow darkening pass

layout(location = 0) out vec2 outUV;

void main() {
    // Generate full-screen triangle
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}

