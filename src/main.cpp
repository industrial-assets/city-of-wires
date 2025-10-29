#include "Engine.hpp"
#include <cstdio>

int main() {
    pcengine::Engine engine;
    if (!engine.initialize(1280, 720, "Procedural City")) {
        std::fprintf(stderr, "Failed to initialize engine\n");
        return 1;
    }
    engine.run();
    engine.shutdown();
    return 0;
}


