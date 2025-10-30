#version 450

// Shadow volume fragment shader - does nothing, stencil ops happen automatically

void main() {
    // No color output - shadow volumes only affect stencil buffer
    // The pipeline has color writes disabled
    // Stencil operations are configured in pipeline state
}
