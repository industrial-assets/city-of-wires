#pragma once

namespace pcengine {

// ============================================================================
// VOLUMETRIC LIGHTING CONFIGURATION
// ============================================================================
// All parameters for the volumetric fog/lighting system in one place.
// Tweak these to adjust the look and feel of the atmospheric lighting.

struct VolumetricConfig {
    // ========================================================================
    // FROXEL GRID DIMENSIONS
    // ========================================================================
    // 3D grid resolution for volumetric calculations
    // Higher = more detail but slower performance
    int froxelGridX = 160;
    int froxelGridY = 96;
    int froxelGridZ = 160;
    
    // Near/far plane for froxel volume (in meters)
    float froxelNear = 0.5f;
    float froxelFar = 250.0f;
    
    // ========================================================================
    // FOG PARAMETERS
    // ========================================================================
    // Base atmospheric fog density (extinction coefficient σ_t)
    // Higher = thicker fog, lower visibility
    float baseFogDensity = 0.015f;          // Base sigma_t (m⁻¹)
    float fogDensityScale = 1.0f;           // Runtime multiplier
    
    // Fog color and scattering
    float fogColorR = 0.4f;
    float fogColorG = 0.5f;
    float fogColorB = 0.6f;
    float fogAlbedo = 0.92f;                // Scattering albedo (0-1)
    
    // Phase function (Henyey-Greenstein)
    float phaseG = 0.7f;                    // Anisotropy (-1 to 1, 0=isotropic, >0=forward)
    
    // ========================================================================
    // LIGHT PARAMETERS
    // ========================================================================
    // Volumetric light intensity and falloff
    float lightIntensityScale = 0.10f;       // Runtime multiplier for all lights
    float lightRadiusScale = 5.0f;          // Runtime multiplier for light radii
    float lightAttenuationFalloff = 0.01f;  // Distance falloff rate
    
    // Neon light specific multipliers
    float neonIntensityMultiplier = 2.0f;   // Multiplier for neon light intensity
    float neonRadiusMultiplier = 1.0f;      // Multiplier for neon light radius
    
    // ========================================================================
    // SKY LIGHT (Sun/Moon)
    // ========================================================================
    // Infinite directional light for atmospheric scattering
    bool enableSkyLight = true;             // Enable/disable sky light
    float skyLightDirectionX = 0.3f;        // Direction vector X (normalized in code)
    float skyLightDirectionY = -0.6f;       // Direction vector Y (points down = sun above)
    float skyLightDirectionZ = 0.4f;        // Direction vector Z
    float skyLightColorR = 1.0f;            // Sky light color R
    float skyLightColorG = 0.95f;           // Sky light color G
    float skyLightColorB = 0.85f;           // Sky light color B
    float skyLightIntensity = 0.8f;         // Sky light intensity (0-5+)
    float skyLightScatteringBoost = 2.0f;   // Extra boost for volumetric scattering
    
    // ========================================================================
    // CULLING PARAMETERS
    // ========================================================================
    // Smart light registration and culling distances
    float maxLightDistance = 320.0f;        // Maximum distance to consider lights (half froxel extent)
    float frustumMargin = 50.0f;            // Extra margin outside frustum to keep lights (meters)
    float nearCameraAlwaysKeep = 100.0f;    // Distance within which lights are always kept
    
    // ========================================================================
    // RAY MARCHING
    // ========================================================================
    // Number of steps to march through the volume
    // More steps = smoother but slower
    int raymarchSteps = 80;
    
    // Step size multiplier (1.0 = tight sampling, >1.0 = skip space)
    float stepSizeMultiplier = 1.0f;
    
    // Jitter amount for temporal anti-aliasing (0-1)
    float jitterAmount = 1.0f;
    
    // ========================================================================
    // TEMPORAL REPROJECTION
    // ========================================================================
    // Blend factor for temporal history (0-1)
    // 0 = no history (all current frame), 1 = all history (no current)
    // Sweet spot usually 0.85-0.95
    float temporalBlendAlpha = 0.0f;        // Currently disabled
    
    // ========================================================================
    // POST-PROCESSING / COMPOSITING
    // ========================================================================
    // Final scattering contribution multiplier
    float scatteringMultiplier = 1.0f;      // Runtime adjustable with -/= keys
    
    // Transmittance blending
    float transmittanceFloor = 0.7f;        // Minimum transmittance (prevents pure black)
    float transmittanceMix = 0.5f;          // Mix with 1.0 to reduce darkening
    
    // Chromatic aberration (lens distortion effect)
    float chromaticAberrationStrength = 0.002f; // Chromatic aberration intensity (0.001-0.005)
    
    // ========================================================================
    // ANAMORPHIC BLOOM (Scattering Buffer)
    // ========================================================================
    // Anamorphic bloom creates horizontal streaking for cinematic lens flare effect
    bool enableAnamorphicBloom = true;      // Enable anamorphic bloom on scattering
    float anamorphicThreshold = 0.3f;       // Brightness threshold to bloom (0-1)
    float anamorphicIntensity = 0.8f;       // Bloom intensity multiplier
    float anamorphicBlurRadius = 3.0f;      // Blur radius in pixels
    float anamorphicAspectRatio = 3.0f;     // Horizontal stretch (1.0=circular, >1=anamorphic)
    int anamorphicSampleCount = 8;          // Samples per direction (higher=smoother)
    
    // ========================================================================
    // GROUND-LEVEL LIGHTS (Cube Volumes)
    // ========================================================================
    // Parameters for ground-level atmospheric lights
    int groundLightAttempts = 100;          // Number of placement attempts
    int groundLightMaxCount = 20;           // Maximum lights to place
    float groundLightMinClearance = 15.0f;  // Distance from buildings (meters)
    
    // Size ranges
    float groundLightMinHeight = 3.0f;      // Minimum cube height (meters)
    float groundLightMaxHeight = 8.0f;      // Maximum cube height (meters)
    float groundLightMinSize = 3.0f;        // Minimum cube width/depth (meters)
    float groundLightMaxSize = 7.0f;        // Maximum cube width/depth (meters)
    
    // Intensity range
    float groundLightMinIntensity = 8.0f;
    float groundLightMaxIntensity = 20.0f;
    
    // ========================================================================
    // LIGHT BEAM VOLUMES (Tall Buildings)
    // ========================================================================
    // Currently disabled, but parameters for future use
    bool enableLightBeams = false;
    float beamMinHeight = 180.0f;           // Beam height (meters)
    float beamMaxHeight = 400.0f;
    float beamMinRadius = 3.0f;             // Beam base radius (meters)
    float beamMaxRadius = 7.0f;
    float beamMinIntensity = 8.0f;
    float beamMaxIntensity = 20.0f;
    float beamSpawnChance = 0.45f;          // Probability per tall building
    float beamMinBuildingHeight = 40.0f;    // Only add to buildings taller than this
    
    // ========================================================================
    // DEBUG
    // ========================================================================
    bool enableDebugOutput = true;          // Print debug info to console
    int debugOutputFrameInterval = 60;      // Print every N frames
    
    // ========================================================================
    // METHODS
    // ========================================================================
    // Load configuration from JSON file
    bool loadFromFile(const char* path);
    
    // Check if file has been modified and reload if needed
    // Returns true if config was reloaded
    bool checkAndReload(const char* path);
};

// Global config instance (defined in VolumetricConfig.cpp)
extern VolumetricConfig g_volumetricConfig;

} // namespace pcengine

