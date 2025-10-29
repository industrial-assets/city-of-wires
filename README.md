## City of Wires

City of Wires is a procedural cityscape tech demo — a tribute to the rain-slick streets and glowing spires of retro-futurist noir.

Fly freely between monolithic towers, each one built from layered detail: flickering neon signs, holographic adverts, rusted vents, and apartment windows pulsing with life.
There are no missions, no enemies — only atmosphere.

Let the sound of the rain, the hum of the power grid, and the flicker of distant lights pull you into the mood of a world forever caught between night and neon.

### Features

- **Procedural City Generation**: Creates a dense urban landscape with varied building heights and asymmetrical designs
- **Atmospheric Rendering**: Heavy fog with volumetric lighting streaming from distant sky sources
- **Neon Lighting System**: Pulsing neon lights with bloom effects scattered across building facades
- **Night Scene**: Dark, industrial color palette with warm neon accents
- **Cinematic Camera**: Slow-moving camera for atmospheric exploration
- **Hot Reloading**: Real-time shader editing for rapid visual development

### Prerequisites

- CMake 3.20+
- A C++20 compiler
- Vulkan SDK installed
  - macOS: Install the LunarG Vulkan SDK which includes MoltenVK. Ensure `VULKAN_SDK` env var is set.
  - Windows/Linux: Install Vulkan SDK from LunarG and ensure `VULKAN_SDK` is set or Vulkan is discoverable by CMake.

### macOS notes (MoltenVK)

- Portability extensions required by MoltenVK are enabled (`VK_KHR_portability_enumeration`, `VK_KHR_portability_subset`).
- Ensure `glslc` is available at `$VULKAN_SDK/bin/glslc`.

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Run

```bash
./build/procedural_city
```

**Or use the convenient script:**

```bash
# Build and run
./run.sh

# Clean build and run
./run.sh clean

# Rebuild only (don't run)
./run.sh rebuild
```

You should see a window displaying a procedurally generated dystopian cityscape with:

- Towering asymmetrical skyscrapers in dark industrial colors
- Heavy atmospheric fog creating depth and mystery
- Volumetric lighting streaming from distant sky sources
- Pulsing neon lights (red, orange, cyan, purple) with bloom effects
- Slow cinematic camera movement for atmospheric exploration
- Night-time color palette with fog-colored background

### Hot Reloading

The engine supports hot reloading of shaders for rapid development:

- **Automatic**: Shaders are automatically reloaded when you save changes to `shaders/city.vert`, `shaders/city.frag`, `shaders/neon.vert`, or `shaders/neon.frag`
- **Manual toggle**: Press `R` key to enable/disable hot reloading
- **Console feedback**: Watch the console for reload status messages

### Project layout

- `src/` engine core sources
  - `CityGenerator.cpp/hpp` - Procedural city generation system
  - `Renderer.cpp/hpp` - Vulkan rendering with atmospheric effects
  - `Engine.cpp/hpp` - Main engine loop and window management
- `shaders/` GLSL shaders compiled to SPIR-V via CMake
  - `city.vert/frag` - Building rendering with fog and volumetric lighting
  - `neon.vert/frag` - Neon light rendering with bloom effects

### Troubleshooting

- If CMake fails with "Vulkan SDK not found", ensure `VULKAN_SDK` is set and points to your SDK root.
- If shader compilation fails, ensure `glslc` is on your PATH or at `$VULKAN_SDK/bin/glslc`.
- The engine builds and runs successfully on macOS with MoltenVK support enabled.

