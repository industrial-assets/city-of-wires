#include "CityGenerator.hpp"
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
    
    printf("Generated %zu buildings with %zu neon lights\n", buildings_.size(), neonLights_.size());
}

void CityGenerator::generateChunk(int chunkX, int chunkZ, int baseSeed) {
    // Check if chunk already exists
    auto chunkKey = std::make_pair(chunkX, chunkZ);
    if (chunkData_.find(chunkKey) != chunkData_.end()) {
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
    
    // Generate buildings for this chunk
    float chunkWorldX = chunkX * chunkSize_;
    float chunkWorldZ = chunkZ * chunkSize_;
    
    // Generate buildings within this chunk
    for (int x = 0; x < buildingsPerChunk_; ++x) {
        for (int z = 0; z < buildingsPerChunk_; ++z) {
            // Skip some grid positions for density variation
            if (neonDist_(rng_) > buildingDensity_) continue;
            
            // Position relative to chunk center
            float localX = (x * gridSpacing_) - (chunkSize_ * 0.5f);
            float localZ = (z * gridSpacing_) - (chunkSize_ * 0.5f);
            
            // Calculate world position (add city center offset to match old generateCity behavior)
            float worldX = chunkWorldX + localX + cityWidth_ * 0.5f;
            float worldZ = chunkWorldZ + localZ + cityDepth_ * 0.5f;
            
            glm::vec2 gridPos(worldX, worldZ);
            generateBuilding(gridPos);
        }
    }
    
    // Store indices for this chunk
    ChunkData chunkData;
    for (size_t i = buildingStartIndex; i < buildings_.size(); ++i) {
        chunkData.buildingIndices.push_back(i);
    }
    for (size_t i = neonStartIndex; i < neonLights_.size(); ++i) {
        chunkData.neonIndices.push_back(i);
    }
    chunkData_[chunkKey] = chunkData;
    
    printf("Generated chunk (%d, %d): %zu buildings, %zu neon lights\n", 
           chunkX, chunkZ, chunkData.buildingIndices.size(), chunkData.neonIndices.size());
}

void CityGenerator::removeChunk(int chunkX, int chunkZ) {
    auto chunkKey = std::make_pair(chunkX, chunkZ);
    auto it = chunkData_.find(chunkKey);
    if (it == chunkData_.end()) {
        return; // Chunk doesn't exist
    }
    
    // Remove buildings and neons in reverse order to maintain indices
    ChunkData& chunkData = it->second;
    
    // Remove neon lights first (they come after buildings in vector)
    for (auto it = chunkData.neonIndices.rbegin(); it != chunkData.neonIndices.rend(); ++it) {
        size_t index = *it;
        if (index < neonLights_.size()) {
            neonLights_.erase(neonLights_.begin() + index);
        }
    }
    
    // Remove buildings
    for (auto it = chunkData.buildingIndices.rbegin(); it != chunkData.buildingIndices.rend(); ++it) {
        size_t index = *it;
        if (index < buildings_.size()) {
            buildings_.erase(buildings_.begin() + index);
        }
    }
    
    // Remove chunk from tracking
    chunkData_.erase(it);
    
    printf("Removed chunk (%d, %d)\n", chunkX, chunkZ);
    
    // Note: Removing chunks invalidates indices of later chunks
    // For simplicity, we'll rebuild all geometry when chunks change
    // A more efficient approach would update indices incrementally
}

void CityGenerator::generateBuilding(glm::vec2 gridPos) {
    Building building;
    
    // Position with some randomness
    float offsetX = (neonDist_(rng_) - 0.5f) * gridSpacing_ * 0.8f;
    float offsetZ = (neonDist_(rng_) - 0.5f) * gridSpacing_ * 0.8f;
    
    building.position = glm::vec3(
        gridPos.x + offsetX - cityWidth_ * 0.5f,
        0.0f,
        gridPos.y + offsetZ - cityDepth_ * 0.5f
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
    
    buildings_.push_back(building);
}

void CityGenerator::addNeonLights(Building& building) {
    int numLights = 2 + static_cast<int>(neonDist_(rng_) * 8);
    
    for (int i = 0; i < numLights; ++i) {
        NeonLight light;
        
        // Choose a random building part to attach the neon to (trunk or branch)
        // This ensures neons appear on all parts of the building, not just the trunk
        if (building.parts.empty()) continue;
        const BuildingPart& part = building.parts[static_cast<size_t>(neonDist_(rng_) * building.parts.size())];
        
        // Calculate absolute position of this part (matching how it's rendered)
        glm::vec3 partAbsPos = building.position + part.position;
        
        // Position lights on building facades
        float side = neonDist_(rng_);
        if (side < 0.25f) {
            // Front face (positive Z)
            light.position = glm::vec3(
                partAbsPos.x + (neonDist_(rng_) - 0.5f) * part.size.x,
                partAbsPos.y + neonDist_(rng_) * part.size.y,
                partAbsPos.z + part.size.z * 0.5f
            );
        } else if (side < 0.5f) {
            // Back face (negative Z)
            light.position = glm::vec3(
                partAbsPos.x + (neonDist_(rng_) - 0.5f) * part.size.x,
                partAbsPos.y + neonDist_(rng_) * part.size.y,
                partAbsPos.z - part.size.z * 0.5f
            );
        } else if (side < 0.75f) {
            // Left face (negative X)
            light.position = glm::vec3(
                partAbsPos.x - part.size.x * 0.5f,
                partAbsPos.y + neonDist_(rng_) * part.size.y,
                partAbsPos.z + (neonDist_(rng_) - 0.5f) * part.size.z
            );
        } else {
            // Right face (positive X)
            light.position = glm::vec3(
                partAbsPos.x + part.size.x * 0.5f,
                partAbsPos.y + neonDist_(rng_) * part.size.y,
                partAbsPos.z + (neonDist_(rng_) - 0.5f) * part.size.z
            );
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

}
