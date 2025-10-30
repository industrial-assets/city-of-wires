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
    std::vector<uint16_t> indices;
    
    uint16_t vertexOffset = 0;
    
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
        
        // 12 triangles (2 unit per face) - each face uses 4 consecutive vertices
        std::vector<uint16_t> boxIndices = {
            // Front face
            static_cast<uint16_t>(vertexOffset+0), static_cast<uint16_t>(vertexOffset+1), static_cast<uint16_t>(vertexOffset+2),
            static_cast<uint16_t>(vertexOffset+2), static_cast<uint16_t>(vertexOffset+3), static_cast<uint16_t>(vertexOffset+0),
            // Back face
            static_cast<uint16_t>(vertexOffset+4), static_cast<uint16_t>(vertexOffset+5), static_cast<uint16_t>(vertexOffset+6),
            static_cast<uint16_t>(vertexOffset+6), static_cast<uint16_t>(vertexOffset+7), static_cast<uint16_t>(vertexOffset+4),
            // Left face
            static_cast<uint16_t>(vertexOffset+8), static_cast<uint16_t>(vertexOffset+9), static_cast<uint16_t>(vertexOffset+10),
            static_cast<uint16_t>(vertexOffset+10), static_cast<uint16_t>(vertexOffset+11), static_cast<uint16_t>(vertexOffset+8),
            // Right face
            static_cast<uint16_t>(vertexOffset+12), static_cast<uint16_t>(vertexOffset+13), static_cast<uint16_t>(vertexOffset+14),
            static_cast<uint16_t>(vertexOffset+14), static_cast<uint16_t>(vertexOffset+15), static_cast<uint16_t>(vertexOffset+12),
            // Top face
            static_cast<uint16_t>(vertexOffset+16), static_cast<uint16_t>(vertexOffset+17), static_cast<uint16_t>(vertexOffset+18),
            static_cast<uint16_t>(vertexOffset+18), static_cast<uint16_t>(vertexOffset+19), static_cast<uint16_t>(vertexOffset+16),
            // Bottom face
            static_cast<uint16_t>(vertexOffset+20), static_cast<uint16_t>(vertexOffset+21), static_cast<uint16_t>(vertexOffset+22),
            static_cast<uint16_t>(vertexOffset+22), static_cast<uint16_t>(vertexOffset+23), static_cast<uint16_t>(vertexOffset+20),
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
    bufferInfo.size = indices.size() * sizeof(uint16_t);
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
        // Create a smaller, sharper quad for each neon light
        float size = 0.3f; // Reduced from 0.5f for sharper appearance
        float x = light.position.x;
        float y = light.position.y;
        float z = light.position.z;
        
        // Format: pos(3) + color(3) + intensity(1) + uv(2) + texIndex(1) = 10 floats per vertex
        int texIndex = (static_cast<int>(std::round(x + y + z)) % 4);
        std::vector<float> quadVertices = {
            // Bottom-left
            x-size, y-size, z, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 0.0f, (float)texIndex,
            // Bottom-right
            x+size, y-size, z, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 0.0f, (float)texIndex,
            // Top-right
            x+size, y+size, z, light.color.x, light.color.y, light.color.z, light.intensity, 1.0f, 1.0f, (float)texIndex,
            // Top-left
            x-size, y+size, z, light.color.x, light.color.y, light.color.z, light.intensity, 0.0f, 1.0f, (float)texIndex,
        };
        
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

}

