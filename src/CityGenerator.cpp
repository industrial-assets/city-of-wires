#include "CityGenerator.hpp"
#include "VolumetricConfig.hpp"
#include <cmath>
#include <algorithm>

namespace pcengine {

CityGenerator::CityGenerator() 
    : rng_(42)
    , heightDist_(0.0f, 1.0f)
    , colorDist_(0.0f, 1.0f)
    , neonDist_(0.0f, 1.0f)
{
}

void CityGenerator::generateCity(int seed) {
    rng_.seed(seed);
    buildings_.clear();
    neonLights_.clear();
    lightVolumes_.clear();
    chunkData_.clear();
    
    // Generate buildings on a grid with some randomness
    for (int x = 0; x < gridSize_; ++x) {
        for (int z = 0; z < gridSize_; ++z) {
            // Skip some grid positions for density variation
            if (neonDist_(rng_) > buildingDensity_) continue;
            
            glm::vec2 gridPos(x * gridSpacing_, z * gridSpacing_);
            generateBuilding(gridPos);
        }
    }
    
    printf("Generated %zu buildings with %zu neon lights and %zu light volumes\n", buildings_.size(), neonLights_.size(), lightVolumes_.size());
}

void CityGenerator::generateChunk(int chunkX, int chunkZ, int baseSeed) {
    // Check if chunk already exists
    auto chunkKey = std::make_pair(chunkX, chunkZ);
    if (chunkData_.find(chunkKey) != chunkData_.end()) {
        printf("  [SKIP] Chunk (%d, %d) already exists\n", chunkX, chunkZ);
        return; // Chunk already generated
    }
    
    // Create a deterministic seed for this chunk based on its coordinates
    // Using a hash function to combine chunk coordinates and base seed
    int chunkSeed = baseSeed;
    chunkSeed = chunkSeed * 73856093 ^ chunkX * 19349663;
    chunkSeed = chunkSeed ^ chunkZ * 83492791;
    
    rng_.seed(chunkSeed);
    
    // Store starting indices
    size_t buildingStartIndex = buildings_.size();
    size_t neonStartIndex = neonLights_.size();
    size_t volumeStartIndex = lightVolumes_.size();
    
    // Generate buildings for this chunk
    float chunkWorldX = chunkX * chunkSize_;
    float chunkWorldZ = chunkZ * chunkSize_;
    
    // Generate buildings on a 5x5 grid with density variation
    // Guaranteed minimum: center building always spawns
    const int gridCount = 5;  // 5x5 grid
    float cellSize = chunkSize_ / gridCount;
    
    int skippedCount = 0;
    for (int x = 0; x < gridCount; ++x) {
        for (int z = 0; z < gridCount; ++z) {
            // Always place center building, others based on density
            bool isCenter = (x == 2 && z == 2);
            if (!isCenter && neonDist_(rng_) > buildingDensity_) {
                skippedCount++;
                continue;
            }
            
            // Position: center of cell with small random offset
            float localX = (x + 0.5f) * cellSize;
            float localZ = (z + 0.5f) * cellSize;
            
            // Add small random offset (±15% of cell size) for variety
            if (!isCenter) {  // Keep center building perfectly centered
                localX += (neonDist_(rng_) - 0.5f) * cellSize * 0.3f;
                localZ += (neonDist_(rng_) - 0.5f) * cellSize * 0.3f;
            }
            
            // World coordinates
            float worldX = chunkWorldX + localX;
            float worldZ = chunkWorldZ + localZ;
            
            glm::vec2 gridPos(worldX, worldZ);
            generateBuilding(gridPos);
        }
    }
    
    // Add cube light volumes for this chunk
    addCubeLightVolumes();
    
    // Store indices for this chunk
    ChunkData chunkData;
    for (size_t i = buildingStartIndex; i < buildings_.size(); ++i) {
        chunkData.buildingIndices.push_back(i);
    }
    for (size_t i = neonStartIndex; i < neonLights_.size(); ++i) {
        chunkData.neonIndices.push_back(i);
    }
    for (size_t i = volumeStartIndex; i < lightVolumes_.size(); ++i) {
        chunkData.lightVolumeIndices.push_back(i);
    }
    chunkData_[chunkKey] = chunkData;
    
    // Debug: Print how many buildings were generated in this chunk
    printf("  Chunk (%d, %d): %zu buildings, %zu neon lights, %zu light volumes\n", chunkX, chunkZ, chunkData.buildingIndices.size(), chunkData.neonIndices.size(), chunkData.lightVolumeIndices.size());
}

void CityGenerator::removeChunk(int chunkX, int chunkZ) {
    // Simply clear the chunk from tracking
    // The actual cleanup happens in clearAllChunks()
    auto chunkKey = std::make_pair(chunkX, chunkZ);
    chunkData_.erase(chunkKey);
}

void CityGenerator::clearAllChunks() {
    buildings_.clear();
    neonLights_.clear();
    lightVolumes_.clear();
    chunkData_.clear();
}

void CityGenerator::generateBuilding(glm::vec2 gridPos) {
    Building building;
    
    // For infinite world, use gridPos directly (chunk system handles world positioning)
    // No random offset for debugging - building should be exactly at gridPos
    building.position = glm::vec3(
        gridPos.x,
        0.0f,
        gridPos.y
    );
    
    // Generate asymmetrical size
    float baseWidth = 2.0f + neonDist_(rng_) * 4.0f;
    float baseDepth = 2.0f + neonDist_(rng_) * 4.0f;
    
    // Make buildings more rectangular and varied
    if (neonDist_(rng_) > 0.5f) {
        baseWidth *= 1.5f + neonDist_(rng_) * 2.0f;
    } else {
        baseDepth *= 1.5f + neonDist_(rng_) * 2.0f;
    }
    
    building.size = glm::vec3(baseWidth, generateHeight(minHeight_), baseDepth);
    building.color = generateBuildingColor();
    building.heightVariation = neonDist_(rng_) * 0.3f;
    building.hasAntenna = neonDist_(rng_) > 0.7f;
    
    // Create the trunk (main building)
    BuildingPart trunk;
    trunk.position = glm::vec3(0.0f, 0.0f, 0.0f); // Relative to building position
    trunk.size = building.size;
    trunk.color = building.color;
    trunk.detailLevel = 0;
    
    building.parts.push_back(trunk);
    
    // Generate branches from the trunk
    generateBranches(building, trunk, 2); // Max depth of 2 for branch recursion
    
    // Add neon lights to this building
    addNeonLights(building);
    addLightVolumes(building);
    
    buildings_.push_back(building);
}

void CityGenerator::addNeonLights(Building& building) {
    int numLights = 2 + static_cast<int>(neonDist_(rng_) * 8);
    
    for (int i = 0; i < numLights; ++i) {
        NeonLight light;
        
        // Choose a random building part to attach the neon to (trunk or branch)
        if (building.parts.empty()) continue;
        const BuildingPart& part = building.parts[static_cast<size_t>(neonDist_(rng_) * building.parts.size())];
        
        // Calculate absolute position of this part
        glm::vec3 partAbsPos = building.position + part.position;
        
        // Choose which face to place the neon on
        float side = neonDist_(rng_);
        float wallWidth, wallHeight;
        const float offset = 0.05f;  // Small offset to prevent z-fighting
        
        if (side < 0.25f) {
            // Front face (positive Z) - width x height
            light.face = 0;
            wallWidth = part.size.x;
            wallHeight = part.size.y;
        } else if (side < 0.5f) {
            // Back face (negative Z) - width x height
            light.face = 1;
            wallWidth = part.size.x;
            wallHeight = part.size.y;
        } else if (side < 0.75f) {
            // Left face (negative X) - depth x height
            light.face = 2;
            wallWidth = part.size.z;
            wallHeight = part.size.y;
        } else {
            // Right face (positive X) - depth x height
            light.face = 3;
            wallWidth = part.size.z;
            wallHeight = part.size.y;
        }
        
        // Generate base size with distribution (most small, some huge)
        float baseSize;
        float sizeRoll = neonDist_(rng_);
        if (sizeRoll < 0.6f) {
            baseSize = 0.3f + neonDist_(rng_) * 0.5f;  // 0.3-0.8
        } else if (sizeRoll < 0.85f) {
            baseSize = 0.8f + neonDist_(rng_) * 1.2f;  // 0.8-2.0
        } else if (sizeRoll < 0.95f) {
            baseSize = 2.0f + neonDist_(rng_) * 2.0f;  // 2.0-4.0
        } else {
            baseSize = 4.0f + neonDist_(rng_) * 4.0f;  // 4.0-8.0 (billboard)
        }
        
        // Random aspect ratio (some wide, some tall, some square)
        float aspectRatio = 0.5f + neonDist_(rng_) * 1.5f;  // 0.5 to 2.0
        light.width = baseSize * aspectRatio;
        light.height = baseSize;
        
        // Clamp to wall dimensions (leave 10% margin on each side)
        float maxWidth = wallWidth * 0.8f;
        float maxHeight = wallHeight * 0.8f;
        if (light.width > maxWidth) {
            float scale = maxWidth / light.width;
            light.width = maxWidth;
            light.height *= scale;
        }
        if (light.height > maxHeight) {
            float scale = maxHeight / light.height;
            light.height = maxHeight;
            light.width *= scale;
        }
        
        // Position on the wall (centered with some randomness)
        float xOffset = (neonDist_(rng_) - 0.5f) * (wallWidth - light.width);
        float yOffset = neonDist_(rng_) * (wallHeight - light.height) + light.height * 0.5f;
        
        // Set position based on face, with proper offset to avoid z-fighting
        switch (light.face) {
            case 0: // Front (+Z)
                light.position = glm::vec3(
                    partAbsPos.x + xOffset,
                    partAbsPos.y + yOffset,
                    partAbsPos.z + part.size.z * 0.5f + offset
                );
                break;
            case 1: // Back (-Z)
                light.position = glm::vec3(
                    partAbsPos.x + xOffset,
                    partAbsPos.y + yOffset,
                    partAbsPos.z - part.size.z * 0.5f - offset
                );
                break;
            case 2: // Left (-X)
                light.position = glm::vec3(
                    partAbsPos.x - part.size.x * 0.5f - offset,
                    partAbsPos.y + yOffset,
                    partAbsPos.z + xOffset
                );
                break;
            case 3: // Right (+X)
                light.position = glm::vec3(
                    partAbsPos.x + part.size.x * 0.5f + offset,
                    partAbsPos.y + yOffset,
                    partAbsPos.z + xOffset
                );
                break;
        }
        
        // Generate neon colors (warm tones with some blues)
        float colorChoice = neonDist_(rng_);
        if (colorChoice < 0.3f) {
            light.color = glm::vec3(1.0f, 0.3f, 0.1f); // Red
        } else if (colorChoice < 0.6f) {
            light.color = glm::vec3(1.0f, 0.8f, 0.2f); // Yellow/Orange
        } else if (colorChoice < 0.8f) {
            light.color = glm::vec3(0.2f, 0.8f, 1.0f); // Cyan
        } else {
            light.color = glm::vec3(0.8f, 0.2f, 1.0f); // Purple
        }
        
        light.intensity = 0.5f + neonDist_(rng_) * 1.5f;
        light.radius = 8.0f + neonDist_(rng_) * 12.0f;
        
        neonLights_.push_back(light);
    }
}

glm::vec3 CityGenerator::generateBuildingColor() {
    // Dark, industrial colors
    float baseGray = 0.1f + colorDist_(rng_) * 0.2f;
    float tint = colorDist_(rng_);
    
    if (tint < 0.3f) {
        return glm::vec3(baseGray, baseGray * 0.8f, baseGray * 0.6f); // Brownish
    } else if (tint < 0.6f) {
        return glm::vec3(baseGray * 0.7f, baseGray, baseGray * 0.8f); // Greenish
    } else {
        return glm::vec3(baseGray * 0.8f, baseGray * 0.8f, baseGray); // Bluish
    }
}

float CityGenerator::generateHeight(float baseHeight) {
    (void)baseHeight; // Unused parameter
    
    // Use exponential distribution for more realistic height distribution
    // This creates more buildings below half max height than above
    float exponentialFactor = heightDist_(rng_);
    
    // Apply exponential distribution: more buildings at lower heights
    // Using exponential function: exp(-λ * x) where λ controls the curve
    // This gives values from 1.0 (at x=0) down to exp(-λ) (at x=1)
    float heightFactor = std::exp(-heightDistributionLambda_ * exponentialFactor);
    
    // Add some spatial clustering - taller buildings tend to be in center
    float distanceFromCenter = std::sqrt(
        std::pow(heightDist_(rng_) - 0.5f, 2) + 
        std::pow(heightDist_(rng_) - 0.5f, 2)
    );
    
    // Center bias: buildings in center can be taller
    float centerBias = 1.0f - distanceFromCenter * 0.3f; // Reduced from 0.5f
    heightFactor = heightFactor * centerBias;
    
    // Ensure we don't exceed max height
    heightFactor = std::min(heightFactor, 1.0f);
    
    return minHeight_ + heightFactor * (maxHeight_ - minHeight_);
}

void CityGenerator::generateBranches(Building& building, BuildingPart& parent, int maxDepth, int currentDepth) {
    // Stop recursion if we've reached max depth
    if (currentDepth >= maxDepth) return;
    
    // Decide how many pairs of branches to add (always even number for symmetry)
    // 0, 2, 4, or 6 pairs (0, 2, 4, 6 total branches)
    int numBranchPairs = 0;
    float branchChance = neonDist_(rng_);
    
    if (currentDepth == 0) {
        // From trunk, more likely to have branches
        if (branchChance < 0.3f) {
            numBranchPairs = 0;
        } else if (branchChance < 0.6f) {
            numBranchPairs = 1; // 2 branches
        } else if (branchChance < 0.85f) {
            numBranchPairs = 2; // 4 branches
        } else {
            numBranchPairs = 3; // 6 branches
        }
    } else {
        // From branch, less likely to have sub-branches
        if (branchChance < 0.6f) {
            numBranchPairs = 0;
        } else if (branchChance < 0.9f) {
            numBranchPairs = 1; // 2 branches
        } else {
            numBranchPairs = 2; // 4 branches
        }
    }
    
    // Generate symmetric branch pairs
    for (int i = 0; i < numBranchPairs; ++i) {
        addSymmetricBranches(building, parent, currentDepth + 1);
    }
}

void CityGenerator::addSymmetricBranches(Building& building, const BuildingPart& parent, int detailLevel) {
    // Choose attachment direction: front/back, left/right, or top (for vertical extensions)
    float directionChoice = neonDist_(rng_);
    
    // Branch size relative to parent (smaller than parent)
    float sizeScale = 0.3f + neonDist_(rng_) * 0.4f; // 0.3 to 0.7 of parent size
    
    // Branch position along parent (how high up the parent)
    float attachmentHeight = 0.3f + neonDist_(rng_) * 0.5f; // 30% to 80% up parent
    
    // Extend amount (how far the branch extends from parent)
    float extendAmount = 0.4f + neonDist_(rng_) * 0.6f; // 40% to 100% of parent dimension
    
    if (directionChoice < 0.33f) {
        // Front/Back branches (along Z axis)
        float branchWidth = parent.size.x * sizeScale;
        float branchHeight = parent.size.y * sizeScale;
        float branchDepth = parent.size.z * extendAmount;
        
        float attachY = parent.position.y + parent.size.y * attachmentHeight;
        float attachX = (neonDist_(rng_) - 0.5f) * parent.size.x * 0.6f; // Random X position on face
        
        // Front branch
        BuildingPart frontBranch;
        frontBranch.position = glm::vec3(
            parent.position.x + attachX,
            attachY - branchHeight * 0.5f, // Center vertically at attachment point
            parent.position.z + parent.size.z * 0.5f + branchDepth * 0.5f
        );
        frontBranch.size = glm::vec3(branchWidth, branchHeight, branchDepth);
        frontBranch.color = building.color * (0.9f + neonDist_(rng_) * 0.2f); // Slight color variation
        frontBranch.detailLevel = detailLevel;
        building.parts.push_back(frontBranch);
        
        // Back branch (symmetric)
        BuildingPart backBranch;
        backBranch.position = glm::vec3(
            parent.position.x - attachX, // Mirror X for symmetry
            attachY - branchHeight * 0.5f,
            parent.position.z - parent.size.z * 0.5f - branchDepth * 0.5f
        );
        backBranch.size = frontBranch.size;
        backBranch.color = frontBranch.color; // Same color for symmetry
        backBranch.detailLevel = detailLevel;
        building.parts.push_back(backBranch);
        
        // Recursively generate branches from the new branches
        // Store indices instead of pointers to avoid invalidation
        size_t branch1Idx = building.parts.size() - 2;
        size_t branch2Idx = building.parts.size() - 1;
        if (detailLevel < 2) {
            generateBranches(building, building.parts[branch1Idx], 2, detailLevel);
            generateBranches(building, building.parts[branch2Idx], 2, detailLevel);
        }
        
    } else if (directionChoice < 0.66f) {
        // Left/Right branches (along X axis)
        float branchWidth = parent.size.x * extendAmount;
        float branchHeight = parent.size.y * sizeScale;
        float branchDepth = parent.size.z * sizeScale;
        
        float attachY = parent.position.y + parent.size.y * attachmentHeight;
        float attachZ = (neonDist_(rng_) - 0.5f) * parent.size.z * 0.6f; // Random Z position on face
        
        // Right branch
        BuildingPart rightBranch;
        rightBranch.position = glm::vec3(
            parent.position.x + parent.size.x * 0.5f + branchWidth * 0.5f,
            attachY - branchHeight * 0.5f,
            parent.position.z + attachZ
        );
        rightBranch.size = glm::vec3(branchWidth, branchHeight, branchDepth);
        rightBranch.color = building.color * (0.9f + neonDist_(rng_) * 0.2f);
        rightBranch.detailLevel = detailLevel;
        building.parts.push_back(rightBranch);
        
        // Left branch (symmetric)
        BuildingPart leftBranch;
        leftBranch.position = glm::vec3(
            parent.position.x - parent.size.x * 0.5f - branchWidth * 0.5f,
            attachY - branchHeight * 0.5f,
            parent.position.z - attachZ // Mirror Z for symmetry
        );
        leftBranch.size = rightBranch.size;
        leftBranch.color = rightBranch.color;
        leftBranch.detailLevel = detailLevel;
        building.parts.push_back(leftBranch);
        
        // Recursively generate branches
        size_t branch1Idx = building.parts.size() - 2;
        size_t branch2Idx = building.parts.size() - 1;
        if (detailLevel < 2) {
            generateBranches(building, building.parts[branch1Idx], 2, detailLevel);
            generateBranches(building, building.parts[branch2Idx], 2, detailLevel);
        }
        
    } else {
        // Top branches (vertical extensions, smaller on top)
        float branchWidth = parent.size.x * (0.7f + neonDist_(rng_) * 0.3f); // 70-100% of parent width
        float branchHeight = parent.size.y * sizeScale;
        float branchDepth = parent.size.z * (0.7f + neonDist_(rng_) * 0.3f); // 70-100% of parent depth
        
        // Top-left branch
        BuildingPart topLeftBranch;
        topLeftBranch.position = glm::vec3(
            parent.position.x - parent.size.x * 0.15f,
            parent.position.y + parent.size.y * 0.5f + branchHeight * 0.5f,
            parent.position.z + parent.size.z * 0.15f
        );
        topLeftBranch.size = glm::vec3(branchWidth, branchHeight, branchDepth);
        topLeftBranch.color = building.color * (0.9f + neonDist_(rng_) * 0.2f);
        topLeftBranch.detailLevel = detailLevel;
        building.parts.push_back(topLeftBranch);
        
        // Top-right branch (symmetric)
        BuildingPart topRightBranch;
        topRightBranch.position = glm::vec3(
            parent.position.x + parent.size.x * 0.15f,
            parent.position.y + parent.size.y * 0.5f + branchHeight * 0.5f,
            parent.position.z - parent.size.z * 0.15f // Mirror Z
        );
        topRightBranch.size = topLeftBranch.size;
        topRightBranch.color = topLeftBranch.color;
        topRightBranch.detailLevel = detailLevel;
        building.parts.push_back(topRightBranch);
        
        // Recursively generate branches
        size_t branch1Idx = building.parts.size() - 2;
        size_t branch2Idx = building.parts.size() - 1;
        if (detailLevel < 2) {
            generateBranches(building, building.parts[branch1Idx], 2, detailLevel);
            generateBranches(building, building.parts[branch2Idx], 2, detailLevel);
        }
    }
}

void CityGenerator::addLightVolumes(Building& building) {
    // DISABLED: Focus on box lights for now
    return;
    
    // Only add to taller buildings with some probability
    if (building.size.y < 40.0f) {
        return;
    }

    float spawnChance = 0.45f; // 45% chance per building
    if (neonDist_(rng_) > spawnChance) {
        return;
    }

    int beamCount = 1;
    if (building.size.y > 80.0f && neonDist_(rng_) < 0.5f) {
        beamCount = 2;
    }

    for (int i = 0; i < beamCount; ++i) {
        LightVolume volume;
        float radius = 3.0f + neonDist_(rng_) * 4.0f;
        float height = 180.0f + neonDist_(rng_) * 220.0f;

        volume.basePosition = building.position + glm::vec3(
            (neonDist_(rng_) - 0.5f) * building.size.x * 0.3f,
            building.size.y,
            (neonDist_(rng_) - 0.5f) * building.size.z * 0.3f);
        volume.height = height;
        volume.baseRadius = radius;
        float colorRoll = neonDist_(rng_);
        if (colorRoll < 0.5f) {
            volume.color = glm::vec3(0.3f, 1.2f, 1.5f);
        } else if (colorRoll < 0.8f) {
            volume.color = glm::vec3(0.5f, 0.9f, 1.4f);
        } else {
            volume.color = glm::vec3(0.8f, 1.0f, 1.5f);
        }
        volume.intensity = 8.0f + neonDist_(rng_) * 12.0f;
        volume.isCone = neonDist_(rng_) > 0.3f;

        lightVolumes_.push_back(volume);
    }
}

void CityGenerator::addCubeLightVolumes() {
    // Generate floating cube light volumes between buildings
    // These create atmospheric depth and fill negative space
    
    if (buildings_.empty()) return;
    
    // Calculate city bounds
    glm::vec2 minBounds(FLT_MAX);
    glm::vec2 maxBounds(-FLT_MAX);
    for (const auto& building : buildings_) {
        minBounds.x = std::min(minBounds.x, building.position.x - building.size.x);
        minBounds.y = std::min(minBounds.y, building.position.z - building.size.z);
        maxBounds.x = std::max(maxBounds.x, building.position.x + building.size.x);
        maxBounds.y = std::max(maxBounds.y, building.position.z + building.size.z);
    }
    
    // Try to place cube volumes in gaps between buildings
    int attempts = g_volumetricConfig.groundLightAttempts;
    int placed = 0;
    
    for (int i = 0; i < attempts && placed < g_volumetricConfig.groundLightMaxCount; ++i) {
        // Random position in city bounds
        glm::vec3 pos(
            minBounds.x + neonDist_(rng_) * (maxBounds.x - minBounds.x),
            0.0f, // Ground level
            minBounds.y + neonDist_(rng_) * (maxBounds.y - minBounds.y)
        );
        
        // Check if position is clear of buildings
        bool clear = true;
        float minClearance = g_volumetricConfig.groundLightMinClearance;
        for (const auto& building : buildings_) {
            float dx = std::abs(pos.x - building.position.x);
            float dz = std::abs(pos.z - building.position.z);
            float clearanceX = dx - building.size.x * 0.5f;
            float clearanceZ = dz - building.size.z * 0.5f;
            
            if (clearanceX < minClearance && clearanceZ < minClearance && pos.y < building.size.y) {
                clear = false;
                break;
            }
        }
        
        if (!clear) continue;
        
        // Create a cube volume
        LightVolume volume;
        volume.basePosition = pos;
        volume.height = g_volumetricConfig.groundLightMinHeight + 
                       neonDist_(rng_) * (g_volumetricConfig.groundLightMaxHeight - g_volumetricConfig.groundLightMinHeight);
        volume.baseRadius = g_volumetricConfig.groundLightMinSize + 
                           neonDist_(rng_) * (g_volumetricConfig.groundLightMaxSize - g_volumetricConfig.groundLightMinSize);
        
        // Random colors - more varied palette
        float colorRoll = neonDist_(rng_);
        if (colorRoll < 0.2f) {
            // Cyan
            volume.color = glm::vec3(0.2f, 1.0f, 1.2f);
        } else if (colorRoll < 0.4f) {
            // Purple/Magenta
            volume.color = glm::vec3(1.0f, 0.3f, 1.0f);
        } else if (colorRoll < 0.6f) {
            // Orange/Amber
            volume.color = glm::vec3(1.2f, 0.6f, 0.2f);
        } else if (colorRoll < 0.8f) {
            // Blue
            volume.color = glm::vec3(0.3f, 0.5f, 1.3f);
        } else {
            // Pink
            volume.color = glm::vec3(1.0f, 0.4f, 0.8f);
        }
        
        volume.intensity = g_volumetricConfig.groundLightMinIntensity + 
                          neonDist_(rng_) * (g_volumetricConfig.groundLightMaxIntensity - g_volumetricConfig.groundLightMinIntensity);
        volume.isCone = false; // Mark as cube (not cone)
        
        lightVolumes_.push_back(volume);
        placed++;
        
        // Debug: Print first few positions
        if (placed <= 3) {
            printf("  Cube light #%d at (%.1f, %.1f, %.1f) size=%.1f intensity=%.1f\n", 
                   placed, pos.x, pos.y, pos.z, volume.baseRadius, volume.intensity);
        }
    }
    
    printf("  Added %d cube light volumes\n", placed);
}

}
