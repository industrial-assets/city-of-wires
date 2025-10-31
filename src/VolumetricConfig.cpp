#include "VolumetricConfig.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace pcengine {

// Global volumetric configuration instance
VolumetricConfig g_volumetricConfig;

// Simple JSON parser (no external dependencies)
static bool parseFloat(const std::string& json, const char* key, float& outValue) {
    std::string searchKey = std::string("\"") + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return false;
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    
    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) pos++;
    
    // Parse number
    size_t endPos = pos;
    while (endPos < json.size() && (isdigit(json[endPos]) || json[endPos] == '.' || json[endPos] == '-')) endPos++;
    
    if (endPos > pos) {
        outValue = static_cast<float>(std::atof(json.substr(pos, endPos - pos).c_str()));
        return true;
    }
    return false;
}

static bool parseInt(const std::string& json, const char* key, int& outValue) {
    std::string searchKey = std::string("\"") + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return false;
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) pos++;
    
    size_t endPos = pos;
    while (endPos < json.size() && (isdigit(json[endPos]) || json[endPos] == '-')) endPos++;
    
    if (endPos > pos) {
        outValue = std::atoi(json.substr(pos, endPos - pos).c_str());
        return true;
    }
    return false;
}

static bool parseVec3(const std::string& json, const char* key, float& r, float& g, float& b) {
    std::string searchKey = std::string("\"") + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return false;
    
    pos = json.find('[', pos);
    if (pos == std::string::npos) return false;
    
    pos++;
    sscanf(json.c_str() + pos, "%f, %f, %f", &r, &g, &b);
    return true;
}

static bool parseBool(const std::string& json, const char* key, bool& outValue) {
    std::string searchKey = std::string("\"") + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return false;
    
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) pos++;
    
    if (json.substr(pos, 4) == "true") {
        outValue = true;
        return true;
    } else if (json.substr(pos, 5) == "false") {
        outValue = false;
        return true;
    }
    return false;
}

static long getFileModTime(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return static_cast<long>(st.st_mtime);
}

static long g_lastModTime = 0;

bool VolumetricConfig::loadFromFile(const char* path) {
    printf("Attempting to load config from: %s\n", path);
    
    std::ifstream file(path);
    if (!file.is_open()) {
        printf("❌ Error: Could not open volumetric config file: %s\n", path);
        printf("   Current working directory might not contain the file.\n");
        printf("   Using default config values from VolumetricConfig.hpp\n");
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();
    
    if (json.empty()) {
        printf("❌ Error: Config file is empty: %s\n", path);
        return false;
    }
    
    printf("📄 Config file loaded (%zu bytes)\n", json.size());
    
    // Parse all fields (use current values as defaults if parsing fails)
    parseInt(json, "width", froxelGridX);
    parseInt(json, "height", froxelGridY);
    parseInt(json, "depth", froxelGridZ);
    parseFloat(json, "near", froxelNear);
    parseFloat(json, "far", froxelFar);
    
    parseFloat(json, "base_density", baseFogDensity);
    parseVec3(json, "color", fogColorR, fogColorG, fogColorB);
    parseFloat(json, "albedo", fogAlbedo);
    parseFloat(json, "phase_g", phaseG);
    
    parseFloat(json, "intensity_scale", lightIntensityScale);
    parseFloat(json, "radius_scale", lightRadiusScale);
    parseFloat(json, "attenuation_falloff", lightAttenuationFalloff);
    
    parseFloat(json, "intensity_multiplier", neonIntensityMultiplier);
    parseFloat(json, "radius_multiplier", neonRadiusMultiplier);
    
    parseBool(json, "enable_sky_light", enableSkyLight);
    parseFloat(json, "sky_light_direction_x", skyLightDirectionX);
    parseFloat(json, "sky_light_direction_y", skyLightDirectionY);
    parseFloat(json, "sky_light_direction_z", skyLightDirectionZ);
    parseFloat(json, "sky_light_color_r", skyLightColorR);
    parseFloat(json, "sky_light_color_g", skyLightColorG);
    parseFloat(json, "sky_light_color_b", skyLightColorB);
    parseFloat(json, "sky_light_intensity", skyLightIntensity);
    parseFloat(json, "sky_light_scattering_boost", skyLightScatteringBoost);
    
    parseFloat(json, "max_distance", maxLightDistance);
    parseFloat(json, "frustum_margin", frustumMargin);
    parseFloat(json, "near_camera_always_keep", nearCameraAlwaysKeep);
    
    parseInt(json, "attempts", groundLightAttempts);
    parseInt(json, "max_count", groundLightMaxCount);
    parseFloat(json, "min_clearance", groundLightMinClearance);
    parseFloat(json, "min_height", groundLightMinHeight);
    parseFloat(json, "max_height", groundLightMaxHeight);
    parseFloat(json, "min_size", groundLightMinSize);
    parseFloat(json, "max_size", groundLightMaxSize);
    parseFloat(json, "min_intensity", groundLightMinIntensity);
    parseFloat(json, "max_intensity", groundLightMaxIntensity);
    
    parseInt(json, "steps", raymarchSteps);
    parseFloat(json, "step_size_multiplier", stepSizeMultiplier);
    parseFloat(json, "jitter_amount", jitterAmount);
    
    parseFloat(json, "blend_alpha", temporalBlendAlpha);
    
    parseFloat(json, "scattering_multiplier", scatteringMultiplier);
    parseFloat(json, "transmittance_floor", transmittanceFloor);
    parseFloat(json, "transmittance_mix", transmittanceMix);
    parseFloat(json, "chromatic_aberration_strength", chromaticAberrationStrength);
    
    parseBool(json, "enable_anamorphic_bloom", enableAnamorphicBloom);
    parseFloat(json, "anamorphic_threshold", anamorphicThreshold);
    parseFloat(json, "anamorphic_intensity", anamorphicIntensity);
    parseFloat(json, "anamorphic_blur_radius", anamorphicBlurRadius);
    parseFloat(json, "anamorphic_aspect_ratio", anamorphicAspectRatio);
    parseInt(json, "anamorphic_sample_count", anamorphicSampleCount);
    
    g_lastModTime = getFileModTime(path);
    
    printf("✓ Loaded volumetric config from: %s\n", path);
    printf("   Light Intensity Scale: %.2f\n", lightIntensityScale);
    printf("   Light Radius Scale: %.2f\n", lightRadiusScale);
    printf("   Ground Lights Max: %d\n", groundLightMaxCount);
    printf("   Base Fog Density: %.4f\n", baseFogDensity);
    return true;
}

bool VolumetricConfig::checkAndReload(const char* path) {
    long currentModTime = getFileModTime(path);
    if (currentModTime > g_lastModTime) {
        printf("\n🔄 Config file changed, reloading...\n");
        if (loadFromFile(path)) {
            printf("✓ Config reloaded successfully!\n\n");
            return true;
        }
    }
    return false;
}

} // namespace pcengine

