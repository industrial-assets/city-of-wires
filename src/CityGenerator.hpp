#pragma once

#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <random>

namespace pcengine {

struct BuildingPart {
    glm::vec3 position;  // Position relative to building base
    glm::vec3 size;
    glm::vec3 color;
    int detailLevel;     // For recursive branch generation
};

struct Building {
    glm::vec3 position;
    glm::vec3 size;      // Main trunk size
    glm::vec3 color;
    std::vector<BuildingPart> parts;  // Trunk + branches
    std::vector<glm::vec3> neonLights;
    float heightVariation;
    bool hasAntenna;
};

struct NeonLight {
    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;
    float width;   // Horizontal size
    float height;  // Vertical size
    int face;      // 0=front(+Z), 1=back(-Z), 2=left(-X), 3=right(+X)
};

struct LightVolume {
    glm::vec3 basePosition;
    float height;
    float baseRadius;
    glm::vec3 color;
    float intensity;
    bool isCone;
};

class CityGenerator {
public:
    CityGenerator();
    ~CityGenerator() = default;

    void generateCity(int seed = 42);
    
    // Chunk-based generation for infinite city
    void generateChunk(int chunkX, int chunkZ, int baseSeed = 42);
    void removeChunk(int chunkX, int chunkZ);
    void clearAllChunks();
    
    const std::vector<Building>& getBuildings() const { return buildings_; }
    const std::vector<NeonLight>& getNeonLights() const { return neonLights_; }
    const std::vector<LightVolume>& getLightVolumes() const { return lightVolumes_; }
    
    // City parameters
    void setCitySize(float width, float depth) { cityWidth_ = width; cityDepth_ = depth; }
    void setBuildingDensity(float density) { buildingDensity_ = density; }
    void setMaxHeight(float height) { maxHeight_ = height; }
    void setHeightDistributionLambda(float lambda) { heightDistributionLambda_ = lambda; }
    void setGridSpacing(float spacing) { gridSpacing_ = spacing; }
    
    // Chunk parameters
    float getChunkSize() const { return chunkSize_; }
    void setChunkSize(float size) { chunkSize_ = size; }

private:
    void generateBuilding(glm::vec2 gridPos);
    void generateBranches(Building& building, BuildingPart& parent, int maxDepth, int currentDepth = 0);
    void addSymmetricBranches(Building& building, const BuildingPart& parent, int detailLevel);
    void addNeonLights(Building& building);
    void addLightVolumes(Building& building);
    void addCubeLightVolumes();
    glm::vec3 generateBuildingColor();
    float generateHeight(float baseHeight);
    
    std::vector<Building> buildings_;
    std::vector<NeonLight> neonLights_;
    std::vector<LightVolume> lightVolumes_;
    
    // City parameters
    float cityWidth_ = 200.0f;
    float cityDepth_ = 200.0f;
    float buildingDensity_ = 0.7f;
    float maxHeight_ = 50.0f;
    float minHeight_ = 10.0f;
    float heightDistributionLambda_ = 2.0f; // Controls exponential height distribution
    
    // Grid parameters
    int gridSize_ = 50;
    float gridSpacing_ = 4.0f;
    
    // Chunk parameters
    float chunkSize_ = 50.0f;  // Size of each chunk in world units
    int buildingsPerChunk_ = 8;  // Grid cells per chunk dimension
    
    // Random generation
    std::mt19937 rng_;
    std::uniform_real_distribution<float> heightDist_;
    std::uniform_real_distribution<float> colorDist_;
    std::uniform_real_distribution<float> neonDist_;
    
    // Track which buildings/neons belong to which chunk
    struct ChunkData {
        std::vector<size_t> buildingIndices;
        std::vector<size_t> neonIndices;
        std::vector<size_t> lightVolumeIndices;
    };
    std::map<std::pair<int, int>, ChunkData> chunkData_;
};

}
