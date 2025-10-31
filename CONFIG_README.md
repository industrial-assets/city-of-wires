# Hot-Reloadable Volumetric Configuration

## Overview

The volumetric lighting system now uses a **JSON configuration file** that can be **edited while the game is running** and will automatically reload!

## Quick Start

1. **Edit** `volumetric_config.json` in your favorite text editor
2. **Save** the file
3. Within ~1 second, the game will detect the change and reload the config
4. See the console output: `🔄 Config file changed, reloading...`

## Configuration File

Location: **`volumetric_config.json`** (in the project root, next to the executable)

```json
{
  "froxel_grid": {
    "width": 160,
    "height": 96,
    "depth": 160,
    "near": 0.5,
    "far": 250.0
  },
  "fog": {
    "base_density": 0.015,
    "color": [0.4, 0.5, 0.6],
    "albedo": 0.92,
    "phase_g": 0.7
  },
  "lights": {
    "intensity_scale": 0.10,
    "radius_scale": 5.0,
    "attenuation_falloff": 0.01
  },
  "ground_lights": {
    "attempts": 100,
    "max_count": 20,
    "min_clearance": 15.0,
    "min_height": 3.0,
    "max_height": 8.0,
    "min_size": 3.0,
    "max_size": 7.0,
    "min_intensity": 8.0,
    "max_intensity": 20.0
  },
  "ray_march": {
    "steps": 80,
    "step_size_multiplier": 1.0,
    "jitter_amount": 1.0
  },
  "temporal": {
    "blend_alpha": 0.0
  },
  "post_processing": {
    "scattering_multiplier": 1.0,
    "transmittance_floor": 0.7,
    "transmittance_mix": 0.5
  },
  "debug": {
    "enable_output": true,
    "frame_interval": 60
  }
}
```

## How It Works

- **On Startup**: Config is loaded from `volumetric_config.json`
- **During Runtime**: Every 60 frames (~1 second at 60 FPS), the engine checks if the file has been modified
- **On Change**: Config is automatically reloaded and applied immediately
- **Fallback**: If the file is missing or malformed, the engine uses default values from `VolumetricConfig.hpp`

## Common Tweaks

### Make Fog Thicker
```json
"fog": {
  "base_density": 0.03,  // was 0.015
  ...
}
```

### Make Lights Brighter
```json
"lights": {
  "intensity_scale": 0.5,  // was 0.10
  ...
}
```

### Add More Ground Lights
```json
"ground_lights": {
  "max_count": 50,  // was 20
  ...
}
```

### Performance Mode (Lower Quality, Faster)
```json
"froxel_grid": {
  "width": 128,
  "height": 64,
  "depth": 128,
  ...
},
"ray_march": {
  "steps": 60,  // was 80
  ...
}
```

## Tips

- **Start Small**: Make one change at a time to see its effect
- **Watch Console**: The console will print confirmation when config reloads
- **Use Runtime Controls**: Combine with keyboard controls (`-`, `=`, `[`, `]`, etc.) for fine-tuning
- **Backup**: Keep a backup of `volumetric_config.json` with your favorite settings!
- **Syntax Errors**: If JSON is malformed, the engine will print a warning and keep using the previous values

## Console Messages

```
✓ Loaded volumetric config from: volumetric_config.json
=== Volumetric Lighting Configuration ===
...
==========================================

[During gameplay, if you edit and save the file:]

🔄 Config file changed, reloading...
✓ Loaded volumetric config from: volumetric_config.json
✓ Config reloaded successfully!
```

## Workflow

**Old Way** (header file):
1. Edit `VolumetricConfig.hpp`
2. Quit game
3. Rebuild (`cmake --build build`)
4. Restart game
5. See changes

**New Way** (JSON):
1. Edit `volumetric_config.json`
2. Save file
3. Changes apply in ~1 second (no restart needed!) 🎉

## Notes

- Changes to `froxel_grid` dimensions require a restart (Vulkan resources need recreation)
- Most other parameters (fog, lights, ray marching) hot-reload perfectly
- The JSON parser is simple but robust - it ignores comments and extra whitespace

