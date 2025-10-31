# Volumetric Lighting Configuration Guide

All volumetric lighting parameters are centralized in `src/VolumetricConfig.hpp` for easy tweaking.

## Quick Start

Edit `src/VolumetricConfig.hpp` and rebuild to adjust the volumetric fog and lighting behavior.

## Configuration Categories

### 🌫️ Fog Parameters

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `baseFogDensity` | 0.015 | 0.001 - 0.1 | Base atmospheric fog density (σ_t in m⁻¹). Higher = thicker fog. |
| `fogColorR/G/B` | 0.4, 0.5, 0.6 | 0.0 - 1.0 | Base fog color (RGB). |
| `fogAlbedo` | 0.92 | 0.0 - 1.0 | Scattering albedo. 1.0 = all light scattered, 0.0 = all absorbed. |
| `phaseG` | 0.7 | -1.0 to 1.0 | Henyey-Greenstein anisotropy. 0=isotropic, >0=forward scattering. |

**Tips:**
- Increase `baseFogDensity` for denser, moodier fog
- Lower `fogAlbedo` for darker, more absorptive fog
- Adjust `phaseG` closer to 1.0 for more directional light beams

---

### 💡 Light Parameters

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `lightIntensityScale` | 1.0 | 0.1 - 10.0 | Global multiplier for all volumetric light intensities. |
| `lightRadiusScale` | 1.0 | 0.1 - 5.0 | Global multiplier for all volumetric light radii. |
| `lightAttenuationFalloff` | 0.01 | 0.001 - 0.1 | Distance falloff rate. Lower = lights reach farther. |

**Runtime Controls:**
- Press `-` / `=`: Adjust scattering brightness
- Press `[` / `]`: Adjust light intensity scale
- Press `9` / `0`: Adjust fog density scale
- Press `7` / `8`: Adjust light radius scale

---

### 🏙️ Ground-Level Lights

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `groundLightAttempts` | 100 | 10 - 500 | Number of placement attempts per city generation. |
| `groundLightMaxCount` | 60 | 10 - 200 | Maximum ground lights to place. |
| `groundLightMinClearance` | 15.0 | 5.0 - 50.0 | Minimum distance from buildings (meters). |
| `groundLightMinHeight` | 3.0 | 1.0 - 10.0 | Minimum cube height (meters). |
| `groundLightMaxHeight` | 8.0 | 5.0 - 30.0 | Maximum cube height (meters). |
| `groundLightMinSize` | 3.0 | 1.0 - 10.0 | Minimum cube width/depth (meters). |
| `groundLightMaxSize` | 7.0 | 5.0 - 30.0 | Maximum cube width/depth (meters). |
| `groundLightMinIntensity` | 8.0 | 1.0 - 50.0 | Minimum light intensity. |
| `groundLightMaxIntensity` | 20.0 | 10.0 - 100.0 | Maximum light intensity. |

**Tips:**
- Increase `groundLightMaxCount` for a more populated street level
- Increase `groundLightMaxHeight` for taller "storefronts"
- Increase intensity range for brighter street-level fog

---

### 📐 Froxel Grid Dimensions

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `froxelGridX` | 160 | 64 - 256 | Horizontal resolution. |
| `froxelGridY` | 96 | 48 - 128 | Vertical resolution. |
| `froxelGridZ` | 160 | 64 - 256 | Depth resolution. |
| `froxelNear` | 0.5 | 0.1 - 5.0 | Near plane (meters). |
| `froxelFar` | 250.0 | 100.0 - 500.0 | Far plane (meters). |

**Performance Notes:**
- Lower grid dimensions = faster but blockier fog
- Higher grid dimensions = slower but smoother fog
- Froxel count = X × Y × Z (160×96×160 = 2.4M voxels)

---

### 🎬 Ray Marching

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `raymarchSteps` | 80 | 40 - 160 | Number of steps per ray. More = smoother but slower. |
| `stepSizeMultiplier` | 1.0 | 0.5 - 2.0 | Step size multiplier. >1.0 = skip space for speed. |
| `jitterAmount` | 1.0 | 0.0 - 1.0 | Temporal jitter for anti-aliasing. 1.0 = full jitter. |

---

### 🕰️ Temporal Reprojection

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `temporalBlendAlpha` | 0.0 | 0.0 - 0.95 | Blend factor for history. 0=disabled, 0.9=strong temporal smoothing. |

**Note:** Currently disabled (0.0) for cleaner per-frame results. Enable for temporal anti-aliasing.

---

### 🎨 Post-Processing

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `scatteringMultiplier` | 1.0 | 0.1 - 50.0 | Final scattering contribution. Runtime adjustable. |
| `transmittanceFloor` | 0.7 | 0.0 - 1.0 | Minimum transmittance (prevents pure black). |
| `transmittanceMix` | 0.5 | 0.0 - 1.0 | Mix with 1.0 to reduce darkening. |

---

## Example Configurations

### 🌃 Dense Cyberpunk Fog
```cpp
baseFogDensity = 0.03f;
fogAlbedo = 0.85f;
groundLightMaxCount = 80;
groundLightMaxIntensity = 30.0f;
```

### 🌌 Light, Airy Atmosphere
```cpp
baseFogDensity = 0.008f;
fogAlbedo = 0.95f;
phaseG = 0.5f;
lightAttenuationFalloff = 0.02f;
```

### ⚡ Performance Mode
```cpp
froxelGridX = 128;
froxelGridY = 64;
froxelGridZ = 128;
raymarchSteps = 60;
groundLightMaxCount = 30;
```

### 🎆 Maximum Visual Quality
```cpp
froxelGridX = 192;
froxelGridY = 128;
froxelGridZ = 192;
raymarchSteps = 100;
groundLightMaxCount = 100;
temporalBlendAlpha = 0.9f;
```

---

## Debug & Profiling

| Parameter | Default | Description |
|-----------|---------|-------------|
| `enableDebugOutput` | true | Print debug info to console. |
| `debugOutputFrameInterval` | 60 | Print every N frames. |

**Debug Keys:**
- `§` (backtick): Toggle debug overlay
- `L`: Toggle light marker visualization
- `P`: Cycle debug visualization modes

---

## Workflow

1. **Edit** `src/VolumetricConfig.hpp`
2. **Rebuild** with `cmake --build build`
3. **Run** `./build/procedural_city`
4. **Tweak** at runtime with keyboard controls (`-`, `=`, `[`, `]`, etc.)
5. **Fine-tune** in config file for permanent changes

---

## Technical Notes

- All fog/light parameters use **physical units** where possible (meters, m⁻¹)
- Froxel grid is **camera-relative** and **world-space aligned** to prevent "swimming"
- Ground lights are **cube volumes** (negative radius in shader signals box shape)
- Light beams (cylinders) are **currently disabled** but can be re-enabled via `enableLightBeams`

