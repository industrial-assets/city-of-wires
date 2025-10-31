# Volumetric Lighting Refactor Plan

This document captures the proposed architecture for upgrading the renderer to the camera-relative froxel-based volumetric lighting pipeline described in `.cursor/rules/lighting.mdc`. It translates the blueprint into concrete engine work items so that subsequent implementation steps (`todo-3`/`todo-4`) are scoped and unblocked.

## Goals
- Replace forward-fragment fog/light shafts with real single-scattering integration through a camera-relative froxel volume.
- Stream density/light data alongside city chunks with zero pre-bake requirements.
- Keep GPU work amortized per-frame via clustered light culling and temporal reprojection.
- Integrate cleanly with existing RAII/resource patterns in `pcengine::Renderer` without destabilizing the swapchain or post stack.

## Frame Graph Overview

```
ClusterBuildCS → DensityInjectionCS → LightInjectionCS → VolumetricRayMarchCS
          ↘                                               ↘
           (light cluster SSBO)                             TemporalReprojectionCS → CompositeVolumetrics
```

1. **ClusterBuildCS** (compute): consumes the frame light list and camera matrices, outputs froxel→light index ranges.
2. **DensityInjectionCS** (compute): clears density to base fog, splats chunk density primitives into `froxelDensity3D`.
3. **LightInjectionCS** (compute): accumulates per-froxel radiance from clustered lights into `froxelLightAccum3D`, flags shadowed hero lights for ray march sampling.
4. **VolumetricRayMarchCS** (compute): marches camera rays through the froxel grid at half-res XY, outputs scattering/transmittance slices.
5. **TemporalReprojectionCS** (compute): reprojects and denoises scattering history using motion vectors & depth.
6. **CompositeVolumetrics** (graphics): blends scattering/transmittance with HDR scene color before post-process.

## Resource Inventory

| Resource | Type | Format | Lifetime | Producer | Consumer |
| --- | --- | --- | --- | --- | --- |
| `froxelDensity3D` | 3D image | `R16F` | per-frame (ping-pong optional) | DensityInjectionCS | VolumetricRayMarchCS |
| `froxelLightAccum3D` | 3D image | `RGBA16F` (fallback `R11G11B10`) | per-frame | LightInjectionCS | VolumetricRayMarchCS |
| `froxelHistory3D` | 3D image | `RGBA16F` | persistent (double-buffered) | TemporalReprojectionCS | TemporalReprojectionCS/VolumetricRayMarchCS |
| `froxelScattering2D` | 2D image | `RGBA16F` | per-frame | VolumetricRayMarchCS | TemporalReprojectionCS/Composite |
| `froxelTransmittance2D` | 2D image | `R16F` | per-frame (optional) | VolumetricRayMarchCS | CompositeVolumetrics |
| `clusterLightList` | SSBO | `uint32` indices | per-frame | ClusterBuildCS | LightInjectionCS/VolumetricRayMarchCS |
| `lightRecords` | SSBO | SoA light params (`vec4` aligned) | persistent | CPU streaming | ClusterBuildCS/LightInjectionCS |
| `densityVolumes` | SSBO | struct {shape, transform, σ_t, albedo, g, flags} | streaming | CPU streaming | DensityInjectionCS |
| `blueNoiseTex` | 2D texture | `R8` | persistent | asset | VolumetricRayMarchCS |
| `motionVectors` | 2D texture | existing (needs addition) | per-frame | renderer | TemporalReprojectionCS |
| `depthPyramid` | 2D texture | existing depth | per-frame | renderer | TemporalReprojectionCS |

## Descriptor Set Layouts

### Set 0 – Per Frame
- `binding 0`: UBO/Push constants (camera matrices, froxel params, jitter).
- `binding 1`: `blueNoiseTex` (combined sampler, wrap repeat).

### Set 1 – Froxel Volumes & History
- `binding 0`: `froxelDensity3D` (storage image).
- `binding 1`: `froxelLightAccum3D` (storage image).
- `binding 2`: `froxelHistoryCurr` / `froxelHistoryPrev` (storage or sampled).
- `binding 3`: `froxelScattering2D` (storage image).
- `binding 4`: `froxelTransmittance2D` (storage image, optional).
- `binding 5`: depth buffer view (sampled).
- `binding 6`: motion vector texture (sampled RG16F/BA16F).

### Set 2 – Light & Density Lists
- `binding 0`: `lightRecords` SSBO.
- `binding 1`: `clusterLightList` SSBO.
- `binding 2`: `clusterCellOffsets` SSBO (prefix sums into `clusterLightList`).
- `binding 3`: `densityVolumes` SSBO.
- `binding 4`: hero light shadow atlas (sampled array).

### Graphics Composite (existing post pass)
Add scattering/transmittance samplers to the HDR compositing descriptor set so postprocess can mix `scene * T + L_s` before tone mapping.

## Data Flow & Synchronization

1. CPU writes fresh light/density data into persistently mapped SSBOs (double-buffer ring). Use fences to guard GPU reads.
2. `ClusterBuildCS`: dispatch `(froxelGridXYZ / workgroup)` with barriers transitioning `clusterLightList` to `VK_ACCESS_SHADER_WRITE_BIT` and `VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT`.
3. Barrier: `clusterLightList`, `clusterCellOffsets` → `VK_ACCESS_SHADER_READ_BIT` for Density/Light passes.
4. `DensityInjectionCS`: clear `froxelDensity3D` via `vkCmdFillImage` or compute, splat density shapes, barrier to `VK_ACCESS_SHADER_READ_BIT` before ray march.
5. `LightInjectionCS`: write `froxelLightAccum3D`, mark hero light indices in a small SSBO for ray march.
6. Barrier both froxel volumes to `VK_ACCESS_SHADER_READ_BIT`, scattering/transmittance images to `VK_ACCESS_SHADER_WRITE_BIT`.
7. `VolumetricRayMarchCS`: dispatch half-res XY grid with Z loops. Outputs scattering/transmittance.
8. Barrier scattering image to `VK_ACCESS_SHADER_READ_BIT` for reprojection; keep history as read/write (ping-pong).
9. `TemporalReprojectionCS`: read current scattering/transmittance + history, write blended history + final scattering. Barrier to `VK_ACCESS_SHADER_READ_BIT` for composite.
10. During graphics composite (HDR pass), bind scattering/transmittance as extra textures and blend into scene color before post-processing.

Use `vkCmdPipelineBarrier2` with `VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT` between compute passes, and `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT` when transitioning to graphics.

## Renderer Integration Plan

1. **Resource Management**
   - Introduce `VolumetricResources` struct owned by `Renderer` with RAII helpers for the new images/SSBOs.
   - Extend `cleanupSwapchain()`/`recreateSwapchain()` to rebuild froxel render targets when resolution or view changes.
   - Maintain camera-relative origin and log-Z parameters per frame (store in `VolumetricConstants`).

2. **Light & Density Streaming**
   - Extend chunk streaming flow to fill `lightRecords` and `densityVolumes`. Implement freelist/ring buffer for add/remove.
   - Provide `Renderer::appendVolumetricLight(...)` helpers for city generator / chunk loaders.

3. **Compute Pipelines**
   - Add GLSL/Spir-V for all five compute stages under `shaders/volumetrics/`.
   - Create pipeline layouts & descriptor sets at initialization; reuse across frames.

4. **Command Buffer Recording**
   - Before current HDR render pass, insert compute dispatch sequence with required barriers.
   - After `TemporalReprojectionCS`, record composite blit that mixes scattering/transmittance into HDR color attachment.
   - Ensure `renderShadowMap` runs before cluster build so atlas data is ready.

5. **Camera & Motion Data**
   - Generate per-pixel motion vectors (store in half-res buffer). Reuse existing view/proj matrices to compute reprojection.
   - Track previous frame view-proj matrix and camera position for reprojection UBO.

6. **Post-processing Update**
   - Extend post process descriptor set to sample scattering/transmittance buffers.
   - Adjust fragment shader to perform `color = hdrColor * T + scattering` before tone mapping.

7. **Debug Tooling**
   - Add toggleable froxel slice viewer and clustered light heatmap using storage buffer readbacks or dedicated compute writes.
   - Expose per-pass GPU timings in overlay.

## Implementation Phasing

1. **Infrastructure (todo-2 completion)**
   - Implement resource structs, descriptor layouts, and shader skeletons (no logic).
   - Integrate command-buffer scaffolding with placeholder dispatch (no-op shaders) to validate synchronization.

2. **Core Compute (todo-3)**
   - Fill in actual clustering, density injection, light accumulation, and ray marching logic.
   - Hook hero light shadow sampling and early-out conditions.

3. **Temporal/Composite (todo-4)**
   - Add reprojection, history clamp, jitter management, and HDR blending.
   - Extend debug overlay and tune performance knobs.

## Open Questions
- Motion vector source: reuse existing camera matrices or integrate a velocity buffer from the geometry pass?
- Shadow atlas format: reuse directional shadow map infrastructure or create dedicated atlas for hero spotlights?
- Memory budget: finalize froxel resolution vs. GPU limits on target hardware (MoltenVK on macOS).

These should be resolved before moving into implementation to avoid rework.


