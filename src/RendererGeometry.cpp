#include "Renderer.hpp"
#include "CityGenerator.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace pcengine {

bool Renderer::createCityGeometry() {
    const auto& buildings = static_cast<CityGenerator*>(cityGenerator_)->getBuildings();
    
    std::vector<float> vertices;
    std::vector<uint32_t> indices;  // Changed from uint16_t to support large cities
    
    uint32_t vertexOffset = 0;  // Changed from uint16_t to support large cities
    
    for (const auto& building : buildings) {
        // Iterate through all parts (trunk + branches)
        for (const auto& part : building.parts) {
            // Calculate absolute position
            float x = building.position.x + part.position.x;
            float y = building.position.y + part.position.y;
            float z = building.position.z + part.position.z;
            float w = part.size.x;
            float h = part.size.y;
            float d = part.size.z;
        
        // Create 24 vertices (4 per face) with proper UV coordinates scaled to building dimensions
        // Scale UV coordinates based on building dimensions to prevent stretching
        float uvScaleU = w / 4.0f; // Scale U coordinate by width
        float uvScaleV = h / 4.0f; // Scale V coordinate by height
        
        std::vector<float> boxVertices = {
            // Front face (z+d/2) - width x height
            x-w/2, y, z+d/2, part.color.x, part.color.y, part.color.z, 0.0f, 0.0f,
            x+w/2, y, z+d/2, part.color.x, part.color.y, part.color.z, uvScaleU, 0.0f,
            x+w/2, y+h, z+d/2, part.color.x, part.color.y, part.color.z, uvScaleU, uvScaleV,
            x-w/2, y+h, z+d/2, part.color.x, part.color.y, part.color.z, 0.0f, uvScaleV,
            
            // Back face (z-d/2) - width x height
            x+w/2, y, z-d/2, part.color.x, part.color.y, part.color.z, 0.0f, 0.0f,
            x-w/2, y, z-d/2, part.color.x, part.color.y, part.color.z, uvScaleU, 0.0f,
            x-w/2, y+h, z-d/2, part.color.x, part.color.y, part.color.z, uvScaleU, uvScaleV,
            x+w/2, y+h, z-d/2, part.color.x, part.color.y, part.color.z, 0.0f, uvScaleV,
            
            // Left face (x-w/2) - depth x height
            x-w/2, y, z-d/2, part.color.x, part.color.y, part.color.z, 0.0f, 0.0f,
            x-w/2, y, z+d/2, part.color.x, part.color.y, part.color.z, d/4.0f, 0.0f,
            x-w/2, y+h, z+d/2, part.color.x, part.color.y, part.color.z, d/4.0f, uvScaleV,
            x-w/2, y+h, z-d/2, part.color.x, part.color.y, part.color.z, 0.0f, uvScaleV,
            
            // Right face (x+w/2) - depth x height
            x+w/2, y, z+d/2, part.color.x, part.color.y, part.color.z, 0.0f, 0.0f,
            x+w/2, y, z-d/2, part.color.x, part.color.y, part.color.z, d/4.0f, 0.0f,
            x+w/2, y+h, z-d/2, part.color.x, part.color.y, part.color.z, d/4.0f, uvScaleV,
            x+w/2, y+h, z+d/2, part.color.x, part.color.y, part.color.z, 0.0f, uvScaleV,
            
            // Top face (y+h) - width x depth
            x-w/2, y+h, z+d/2, part.color.x, part.color.y, part.color.z, 0.0f, 0.0f,
            x+w/2, y+h, z+d/2, part.color.x, part.color.y, part.color.z, uvScaleU, 0.0f,
            x+w/2, y+h, z-d/2, part.color.x, part.color.y, part.color.z, uvScaleU, d/4.0f,
            x-w/2, y+h, z-d/2, part.color.x, part.color.y, part.color.z, 0.0f, d/4.0f,
            
            // Bottom face (y) - width x depth
            x-w/2, y, z-d/2, part.color.x, part.color.y, part.color.z, 0.0f, 0.0f,
            x+w/2, y, z-d/2, part.color.x, part.color.y, part.color.z, uvScaleU, 0.0f,
            x+w/2, y, z+d/2, part.color.x, part.color.y, part.color.z, uvScaleU, d/4.0f,
            x-w/2, y, z+d/2, part.color.x, part.color.y, part.color.z, 0.0f, d/4.0f,
        };
        // Expand to include per-vertex texture index and normals
        // Format: pos(3) + color(3) + uv(2) + texIndex(1) + normal(3) = 12 floats per vertex
        std::vector<float> boxVerticesComplete;
        boxVerticesComplete.reserve((boxVertices.size()/8) * 12);
            int texIndex = ((int)std::round(x + y + z)) & 1;
        
        // Face normals: front=0,0,1; back=0,0,-1; left=-1,0,0; right=1,0,0; top=0,1,0; bottom=0,-1,0
        struct FaceData { int startIdx; float nx, ny, nz; };
        FaceData faces[6] = {
            {0, 0.0f, 0.0f, 1.0f},   // Front (vertices 0-3)
            {32, 0.0f, 0.0f, -1.0f}, // Back (vertices 4-7, offset 32 floats = 4 vertices * 8 floats)
            {64, -1.0f, 0.0f, 0.0f}, // Left (vertices 8-11, offset 64 floats)
            {96, 1.0f, 0.0f, 0.0f},  // Right (vertices 12-15, offset 96 floats)
            {128, 0.0f, 1.0f, 0.0f}, // Top (vertices 16-19, offset 128 floats)
            {160, 0.0f, -1.0f, 0.0f} // Bottom (vertices 20-23, offset 160 floats)
        };
        
        for (size_t i = 0; i < boxVertices.size(); i += 8) {
            // Add position, color, uv
            boxVerticesComplete.insert(boxVerticesComplete.end(), boxVertices.begin() + i, boxVertices.begin() + i + 8);
            // Add texture index
            boxVerticesComplete.push_back((float)texIndex);
            
            // Find which face this vertex belongs to and add appropriate normal
            int currentOffset = (int)i;
            float nx = 0.0f, ny = 0.0f, nz = 0.0f;
            for (int f = 0; f < 6; f++) {
                if (currentOffset >= faces[f].startIdx && currentOffset < faces[f].startIdx + 32) {
                    nx = faces[f].nx;
                    ny = faces[f].ny;
                    nz = faces[f].nz;
                    break;
                }
            }
            boxVerticesComplete.push_back(nx);
            boxVerticesComplete.push_back(ny);
            boxVerticesComplete.push_back(nz);
        }
        
        // 12 triangles (2 per face) - each face uses 4 consecutive vertices
        std::vector<uint32_t> boxIndices = {
            // Front face
            vertexOffset+0, vertexOffset+1, vertexOffset+2,
            vertexOffset+2, vertexOffset+3, vertexOffset+0,
            // Back face
            vertexOffset+4, vertexOffset+5, vertexOffset+6,
            vertexOffset+6, vertexOffset+7, vertexOffset+4,
            // Left face
            vertexOffset+8, vertexOffset+9, vertexOffset+10,
            vertexOffset+10, vertexOffset+11, vertexOffset+8,
            // Right face
            vertexOffset+12, vertexOffset+13, vertexOffset+14,
            vertexOffset+14, vertexOffset+15, vertexOffset+12,
            // Top face
            vertexOffset+16, vertexOffset+17, vertexOffset+18,
            vertexOffset+18, vertexOffset+19, vertexOffset+16,
            // Bottom face
            vertexOffset+20, vertexOffset+21, vertexOffset+22,
            vertexOffset+22, vertexOffset+23, vertexOffset+20,
        };
        
            vertices.insert(vertices.end(), boxVerticesComplete.begin(), boxVerticesComplete.end());
            indices.insert(indices.end(), boxIndices.begin(), boxIndices.end());
            
            vertexOffset += 24;
        }
    }
    
    cityIndexCount_ = static_cast<uint32_t>(indices.size());
    
    // Create vertex buffer
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = vertices.size() * sizeof(float);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &cityVertexBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, cityVertexBuffer_, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &cityVertexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, cityVertexBuffer_, cityVertexBufferMemory_, 0);
    
    void* data;
    vkMapMemory(device_, cityVertexBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, vertices.data(), bufferInfo.size);
    vkUnmapMemory(device_, cityVertexBufferMemory_);
    
    // Create index buffer
    bufferInfo.size = indices.size() * sizeof(uint32_t);  // Changed from uint16_t to uint32_t
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &cityIndexBuffer_) != VK_SUCCESS) return false;
    
    vkGetBufferMemoryRequirements(device_, cityIndexBuffer_, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &cityIndexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, cityIndexBuffer_, cityIndexBufferMemory_, 0);
    
    vkMapMemory(device_, cityIndexBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, indices.data(), bufferInfo.size);
    vkUnmapMemory(device_, cityIndexBufferMemory_);
    
    size_t totalParts = 0;
    for (const auto& building : buildings) {
        totalParts += building.parts.size();
    }
    printf("Created city geometry: %zu vertices, %zu indices from %zu buildings with %zu total parts\n", 
           vertices.size() / 12, indices.size(), buildings.size(), totalParts);
    return true;
}

bool Renderer::createNeonGeometry() {
    const auto& neonLights = static_cast<CityGenerator*>(cityGenerator_)->getNeonLights();
    
    std::vector<float> vertices;
    std::vector<uint16_t> indices;
    
    uint16_t vertexOffset = 0;
    
    for (const auto& light : neonLights) {
        // Use the light's individual width and height
        float halfW = light.width * 0.5f;
        float halfH = light.height * 0.5f;
        float x = light.position.x;
        float y = light.position.y;
        float z = light.position.z;
        
        // Format: pos(3) + color(3) + intensity(1) + uv(2) + texIndex(1) = 10 floats per vertex
        int texIndex = 0;
        {
            int layerCount = (numNeonTextures_ > 0) ? numNeonTextures_ : 1;
            int seed = static_cast<int>(std::round(x + y + z));
            int mod = seed % layerCount;
            texIndex = (mod < 0) ? (mod + layerCount) : mod;
        }
        
        std::vector<float> quadVertices;
        
        // Create quad oriented based on wall face
        // face: 0=front(+Z), 1=back(-Z), 2=left(-X), 3=right(+X)
        switch (light.face) {
            case 0: // Front face (+Z) - facing forward
                quadVertices = {
                    x-halfW, y-halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 1.0f, (float)texIndex,
                    x+halfW, y-halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 1.0f, (float)texIndex,
                    x+halfW, y+halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 0.0f, (float)texIndex,
                    x-halfW, y+halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 0.0f, (float)texIndex,
                };
                break;
            case 1: // Back face (-Z) - facing backward (flip winding)
                quadVertices = {
                    x+halfW, y-halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 1.0f, (float)texIndex,
                    x-halfW, y-halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 1.0f, (float)texIndex,
                    x-halfW, y+halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 0.0f, (float)texIndex,
                    x+halfW, y+halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 0.0f, (float)texIndex,
                };
                break;
            case 2: // Left face (-X) - facing left
                quadVertices = {
                    x, y-halfH, z+halfW, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 1.0f, (float)texIndex,
                    x, y-halfH, z-halfW, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 1.0f, (float)texIndex,
                    x, y+halfH, z-halfW, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 0.0f, (float)texIndex,
                    x, y+halfH, z+halfW, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 0.0f, (float)texIndex,
                };
                break;
            case 3: // Right face (+X) - facing right (flip winding)
                quadVertices = {
                    x, y-halfH, z-halfW, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 1.0f, (float)texIndex,
                    x, y-halfH, z+halfW, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 1.0f, (float)texIndex,
                    x, y+halfH, z+halfW, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 0.0f, (float)texIndex,
                    x, y+halfH, z-halfW, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 0.0f, (float)texIndex,
                };
                break;
            default: // Should never happen, but use front face as fallback
                quadVertices = {
                    x-halfW, y-halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 1.0f, (float)texIndex,
                    x+halfW, y-halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 1.0f, (float)texIndex,
                    x+halfW, y+halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 0.0f, (float)texIndex,
                    x-halfW, y+halfH, z, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 0.0f, (float)texIndex,
                };
                break;
        }
        
        std::vector<uint16_t> quadIndices = {
            static_cast<uint16_t>(vertexOffset+0), static_cast<uint16_t>(vertexOffset+1), static_cast<uint16_t>(vertexOffset+2),
            static_cast<uint16_t>(vertexOffset+2), static_cast<uint16_t>(vertexOffset+3), static_cast<uint16_t>(vertexOffset+0),
        };
        
        vertices.insert(vertices.end(), quadVertices.begin(), quadVertices.end());
        indices.insert(indices.end(), quadIndices.begin(), quadIndices.end());
        
        vertexOffset += 4;
    }
    
    neonIndexCount_ = static_cast<uint32_t>(indices.size());
    
    // Create vertex buffer
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = vertices.size() * sizeof(float);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &neonVertexBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, neonVertexBuffer_, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &neonVertexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, neonVertexBuffer_, neonVertexBufferMemory_, 0);
    
    void* data;
    vkMapMemory(device_, neonVertexBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, vertices.data(), bufferInfo.size);
    vkUnmapMemory(device_, neonVertexBufferMemory_);
    
    // Create index buffer
    bufferInfo.size = indices.size() * sizeof(uint16_t);
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &neonIndexBuffer_) != VK_SUCCESS) return false;
    
    vkGetBufferMemoryRequirements(device_, neonIndexBuffer_, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &neonIndexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, neonIndexBuffer_, neonIndexBufferMemory_, 0);
    
    vkMapMemory(device_, neonIndexBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, indices.data(), bufferInfo.size);
    vkUnmapMemory(device_, neonIndexBufferMemory_);
    
    printf("Created neon geometry: %zu lights\n", neonLights.size());
    return true;
}

bool Renderer::createGroundGeometry() {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    
    CityGenerator* gen = static_cast<CityGenerator*>(cityGenerator_);
    float chunkSize = gen->getChunkSize();
    
    uint32_t vertexOffset = 0;
    
    // Create a ground quad for each active chunk
    for (const auto& chunk : activeChunks_) {
        int chunkX = chunk.first;
        int chunkZ = chunk.second;
        
        // Calculate chunk boundaries in world space
        float minX = chunkX * chunkSize;
        float maxX = (chunkX + 1) * chunkSize;
        float minZ = chunkZ * chunkSize;
        float maxZ = (chunkZ + 1) * chunkSize;
        float y = 0.0f;  // Ground level
        
        // Dark ground color (almost black with subtle blue tint for dystopian aesthetic)
        glm::vec3 groundColor(0.02f, 0.025f, 0.03f);
        
        // Add some variation based on chunk position
        int seed = chunkX * 73856093 ^ chunkZ * 19349663;
        float variation = ((seed % 100) / 100.0f) * 0.01f;
        groundColor += glm::vec3(variation);
        
        // UV coordinates for tiling texture (if we add ground textures later)
        float uvScale = 1.0f;  // 1:1 scale with world units
        
        // Format: pos(3) + color(3) + uv(2) + texIndex(1) + normal(3) = 12 floats per vertex
        // (Same format as building geometry for compatibility)
        int texIndex = 0;  // Use first texture or specific ground texture
        glm::vec3 normal(0.0f, -1.0f, 0.0f);  // Normal points down (flipped for proper lighting)
        
        // Create ground quad vertices (4 corners)
        std::vector<float> quadVertices = {
            // Bottom-left
            minX, y, minZ, groundColor.x, groundColor.y, groundColor.z, 
            0.0f, 0.0f, (float)texIndex, normal.x, normal.y, normal.z,
            
            // Bottom-right
            maxX, y, minZ, groundColor.x, groundColor.y, groundColor.z,
            uvScale, 0.0f, (float)texIndex, normal.x, normal.y, normal.z,
            
            // Top-right
            maxX, y, maxZ, groundColor.x, groundColor.y, groundColor.z,
            uvScale, uvScale, (float)texIndex, normal.x, normal.y, normal.z,
            
            // Top-left
            minX, y, maxZ, groundColor.x, groundColor.y, groundColor.z,
            0.0f, uvScale, (float)texIndex, normal.x, normal.y, normal.z,
        };
        
        // Two triangles for the quad
        std::vector<uint32_t> quadIndices = {
            vertexOffset+0, vertexOffset+1, vertexOffset+2,
            vertexOffset+2, vertexOffset+3, vertexOffset+0,
        };
        
        vertices.insert(vertices.end(), quadVertices.begin(), quadVertices.end());
        indices.insert(indices.end(), quadIndices.begin(), quadIndices.end());
        
        vertexOffset += 4;
    }
    
    groundIndexCount_ = static_cast<uint32_t>(indices.size());
    
    if (groundIndexCount_ == 0) {
        printf("No ground geometry to create (no active chunks)\n");
        return true;  // Not an error, just no chunks yet
    }
    
    // Create vertex buffer
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = vertices.size() * sizeof(float);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &groundVertexBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, groundVertexBuffer_, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &groundVertexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, groundVertexBuffer_, groundVertexBufferMemory_, 0);
    
    void* data;
    vkMapMemory(device_, groundVertexBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, vertices.data(), bufferInfo.size);
    vkUnmapMemory(device_, groundVertexBufferMemory_);
    
    // Create index buffer
    bufferInfo.size = indices.size() * sizeof(uint32_t);
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &groundIndexBuffer_) != VK_SUCCESS) return false;
    
    vkGetBufferMemoryRequirements(device_, groundIndexBuffer_, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &groundIndexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, groundIndexBuffer_, groundIndexBufferMemory_, 0);
    
    vkMapMemory(device_, groundIndexBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, indices.data(), bufferInfo.size);
    vkUnmapMemory(device_, groundIndexBufferMemory_);
    
    printf("Created ground geometry: %zu chunks (%u indices)\n", activeChunks_.size(), groundIndexCount_);
    return true;
}

bool Renderer::createShadowVolumeGeometry() {
    const auto& buildings = static_cast<CityGenerator*>(cityGenerator_)->getBuildings();
    
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    uint32_t vertexOffset = 0;
    
    // Get light direction from sky light (normalized)
    glm::vec3 lightDir = glm::normalize(skyLightDir_);
    
    // Shadow extrusion distance (very far to ensure shadows reach ground)
    const float shadowExtrusionDist = 500.0f;
    
    for (const auto& building : buildings) {
        for (const auto& part : building.parts) {
            // Calculate absolute position
            glm::vec3 partPos = building.position + part.position;
            float w = part.size.x;
            float h = part.size.y;
            float d = part.size.z;
            
            // Define the 8 corners of the building part
            glm::vec3 corners[8] = {
                partPos + glm::vec3(-w/2, 0, -d/2),  // 0: bottom-front-left
                partPos + glm::vec3( w/2, 0, -d/2),  // 1: bottom-front-right
                partPos + glm::vec3( w/2, 0,  d/2),  // 2: bottom-back-right
                partPos + glm::vec3(-w/2, 0,  d/2),  // 3: bottom-back-left
                partPos + glm::vec3(-w/2, h, -d/2),  // 4: top-front-left
                partPos + glm::vec3( w/2, h, -d/2),  // 5: top-front-right
                partPos + glm::vec3( w/2, h,  d/2),  // 6: top-back-right
                partPos + glm::vec3(-w/2, h,  d/2),  // 7: top-back-left
            };
            
            // Extrude corners away from light (opposite direction of sun position)
            glm::vec3 extrudedCorners[8];
            for (int i = 0; i < 8; ++i) {
                extrudedCorners[i] = corners[i] - lightDir * shadowExtrusionDist;
            }
            
            // Format: just position (3 floats) - shadow volumes don't need color/UV
            // We'll add all vertices for the shadow volume
            std::vector<glm::vec3> shadowVerts;
            
            // Add original corners
            for (int i = 0; i < 8; ++i) {
                shadowVerts.push_back(corners[i]);
            }
            // Add extruded corners
            for (int i = 0; i < 8; ++i) {
                shadowVerts.push_back(extrudedCorners[i]);
            }
            
            // Convert to float array
            for (const auto& v : shadowVerts) {
                vertices.push_back(v.x);
                vertices.push_back(v.y);
                vertices.push_back(v.z);
            }
            
            // Now create quads for the sides (silhouette edges)
            // We create quads between original and extruded edges
            // Original: 0-7, Extruded: 8-15
            
            // Side edges that form the shadow volume sides
            // Bottom edges (assuming light from above, these edges cast shadows down)
            struct Edge { int v0, v1; };
            Edge bottomEdges[] = {
                {0, 1}, {1, 2}, {2, 3}, {3, 0}  // Bottom square
            };
            
            for (const Edge& edge : bottomEdges) {
                int v0 = edge.v0;
                int v1 = edge.v1;
                int v0_ext = v0 + 8;  // Extruded version
                int v1_ext = v1 + 8;
                
                // Create quad: v0, v1, v1_ext, v0_ext
                // Two triangles with counter-clockwise winding
                indices.push_back(vertexOffset + v0);
                indices.push_back(vertexOffset + v1);
                indices.push_back(vertexOffset + v1_ext);
                
                indices.push_back(vertexOffset + v1_ext);
                indices.push_back(vertexOffset + v0_ext);
                indices.push_back(vertexOffset + v0);
            }
            
            // Front cap (light-facing, at building position)
            // Bottom face
            indices.push_back(vertexOffset + 0);
            indices.push_back(vertexOffset + 2);
            indices.push_back(vertexOffset + 1);
            
            indices.push_back(vertexOffset + 0);
            indices.push_back(vertexOffset + 3);
            indices.push_back(vertexOffset + 2);
            
            // Back cap (far end of shadow)
            // Bottom face (extruded)
            indices.push_back(vertexOffset + 8);
            indices.push_back(vertexOffset + 9);
            indices.push_back(vertexOffset + 10);
            
            indices.push_back(vertexOffset + 10);
            indices.push_back(vertexOffset + 11);
            indices.push_back(vertexOffset + 8);
            
            vertexOffset += 16;  // 8 original + 8 extruded
        }
    }
    
    shadowVolumeIndexCount_ = static_cast<uint32_t>(indices.size());
    
    if (shadowVolumeIndexCount_ == 0) {
        printf("No shadow volume geometry to create\n");
        return true;
    }
    
    // Create vertex buffer
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = vertices.size() * sizeof(float);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &shadowVolumeVertexBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, shadowVolumeVertexBuffer_, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &shadowVolumeVertexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, shadowVolumeVertexBuffer_, shadowVolumeVertexBufferMemory_, 0);
    
    void* data;
    vkMapMemory(device_, shadowVolumeVertexBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, vertices.data(), bufferInfo.size);
    vkUnmapMemory(device_, shadowVolumeVertexBufferMemory_);
    
    // Create index buffer
    bufferInfo.size = indices.size() * sizeof(uint32_t);
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &shadowVolumeIndexBuffer_) != VK_SUCCESS) return false;
    
    vkGetBufferMemoryRequirements(device_, shadowVolumeIndexBuffer_, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &shadowVolumeIndexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, shadowVolumeIndexBuffer_, shadowVolumeIndexBufferMemory_, 0);
    
    vkMapMemory(device_, shadowVolumeIndexBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, indices.data(), bufferInfo.size);
    vkUnmapMemory(device_, shadowVolumeIndexBufferMemory_);
    
    printf("Created shadow volume geometry: %u indices for %zu buildings\n", shadowVolumeIndexCount_, buildings.size());
    return true;
}

}

