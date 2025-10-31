#include "Renderer.hpp"
#include "CityGenerator.hpp"
#include "VolumetricConfig.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace pcengine {

namespace {

float halton(uint32_t index, uint32_t base) {
    float result = 0.0f;
    float f = 1.0f;
    while (index > 0u) {
        f /= static_cast<float>(base);
        result += f * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

glm::vec3 computeMotionVector(const glm::vec3& worldPos,
                              const glm::mat4& prevViewProj,
                              const glm::mat4& currViewProj)
{
    glm::vec4 currentClip = currViewProj * glm::vec4(worldPos, 1.0f);
    glm::vec4 prevClip = prevViewProj * glm::vec4(worldPos, 1.0f);
    if (currentClip.w <= 0.0f || prevClip.w <= 0.0f) {
        return glm::vec3(0.0f);
    }

    glm::vec2 currentNDC = glm::vec2(currentClip) / currentClip.w;
    glm::vec2 prevNDC = glm::vec2(prevClip) / prevClip.w;
    glm::vec2 motion = currentNDC - prevNDC;
    return glm::vec3(motion, 0.0f);
}

float henyeGreenstein(float cosTheta, float g) {
    float denom = 1.0f + g * g - 2.0f * g * cosTheta;
    return (1.0f - g * g) / (denom * std::sqrt(denom));
}

constexpr VkFormat kFroxelDensityFormat = VK_FORMAT_R16_SFLOAT;
constexpr VkFormat kFroxelLightFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kScatteringFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kTransmittanceFormat = VK_FORMAT_R16_SFLOAT;

constexpr uint32_t kMaxVolumetricLights = 1024;
constexpr uint32_t kMaxClusterEntries = 512u * 1024u;
constexpr uint32_t kMaxDensityVolumes = 2048;
constexpr float kFroxelCellSizeXZ = 4.0f;
constexpr float kFroxelCellSizeY = 4.0f;

struct VolumetricPushConstants {
    glm::ivec4 dims{0};      // xyz = dimensions, w = history enabled flag (0/1)
    glm::vec4 scalars0{0.0f}; // x = time, y = step length, z = sigma_t, w = albedo
    glm::vec4 scalars1{0.0f}; // x = history alpha, y = history valid, z = light count, w = density count
    glm::vec4 scalars2{0.0f}; // light g (x), clamp min (y), clamp max (z), reserved
    glm::vec4 scalars3{0.0f}; // falloff multiplier (x), reserved
};

struct VolumetricConstantsGPU {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invView;
    glm::mat4 invProj;
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::mat4 prevViewProj;
    glm::mat4 invPrevViewProj;
    glm::vec4 cameraPos;      // xyz position, w = 1
    glm::vec4 prevCameraPos;  // xyz previous position, w = 1
    glm::vec4 lightDir;       // xyz normalized, w unused (legacy)
    glm::vec4 fogColorSigma;  // xyz fog color, w = base sigma_t
    glm::vec4 params;         // x = near, y = far, z = 1/width, w = 1/height
    glm::vec4 jitterFrameTime; // x = frame index, y = time, z/w reserved
    glm::vec4 skyLightDir;    // xyz normalized direction (points TO sun), w = intensity
    glm::vec4 skyLightColor;  // xyz color, w = scattering boost
};

static std::vector<char> readShaderFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        return {};
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> data(static_cast<size_t>(len));
    if (len > 0) {
        fread(data.data(), 1, data.size(), f);
    }
    fclose(f);
    return data;
}

} // namespace

bool Renderer::createVolumetricResources() {
    if (!volumetricsEnabled_) {
        volumetricsReady_ = false;
        return true;
    }

    destroyVolumetricResources();

    auto& v = volumetrics_;
    v.imagesInitialized = false;
    v.froxelGrid = {static_cast<uint32_t>(g_volumetricConfig.froxelGridX), 
                    static_cast<uint32_t>(g_volumetricConfig.froxelGridY), 
                    static_cast<uint32_t>(g_volumetricConfig.froxelGridZ)};
    v.raymarchExtent.width = swapchainExtent_.width;
    v.raymarchExtent.height = swapchainExtent_.height;

    auto create3DImage = [&](VkExtent3D extent, VkFormat format, VkImage& image, VkDeviceMemory& memory, VkImageView& view) -> bool {
        VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_3D;
        info.extent = extent;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        if (vkCreateImage(device_, &info, nullptr, &image) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, image, &req);
        VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device_, &alloc, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            image = VK_NULL_HANDLE;
            return false;
        }
        if (vkBindImageMemory(device_, image, memory, 0) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, memory, nullptr);
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            return false;
        }

        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &view) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, memory, nullptr);
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            return false;
        }
        return true;
    };

    auto create2DImage = [&](VkExtent2D extent, VkFormat format, VkImage& image, VkDeviceMemory& memory, VkImageView& view) -> bool {
        VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = { extent.width, extent.height, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        if (vkCreateImage(device_, &info, nullptr, &image) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, image, &req);
        VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device_, &alloc, nullptr, &memory) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            image = VK_NULL_HANDLE;
            return false;
        }
        if (vkBindImageMemory(device_, image, memory, 0) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, memory, nullptr);
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            return false;
        }

        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &view) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, memory, nullptr);
            image = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            return false;
        }
        return true;
    };

    if (!create3DImage(v.froxelGrid, kFroxelDensityFormat, v.densityImage, v.densityMemory, v.densityView)) return false;
    if (!create3DImage(v.froxelGrid, kFroxelLightFormat, v.lightImage, v.lightMemory, v.lightView)) return false;

    VkExtent2D scatterExtent{ std::max(1u, swapchainExtent_.width / 2), std::max(1u, swapchainExtent_.height / 2) };
    if (!create2DImage(scatterExtent, kScatteringFormat, v.scatteringImage, v.scatteringMemory, v.scatteringView)) return false;
    if (!create2DImage(scatterExtent, kTransmittanceFormat, v.transmittanceImage, v.transmittanceMemory, v.transmittanceView)) return false;
    if (!create2DImage(scatterExtent, kScatteringFormat, v.historyImage, v.historyMemory, v.historyView)) return false;
    
    // Anamorphic bloom images (same resolution as scattering buffer)
    if (!create2DImage(scatterExtent, kScatteringFormat, v.anamorphicBloomImage, v.anamorphicBloomMemory, v.anamorphicBloomView)) return false;
    if (!create2DImage(scatterExtent, kScatteringFormat, v.anamorphicTempImage, v.anamorphicTempMemory, v.anamorphicTempView)) return false;

    v.historyInitialized = false;

    const VkDeviceSize constantsSize = sizeof(VolumetricConstantsGPU);
    if (!createBuffer(v.constantsBuffer, constantsSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true)) {
        return false;
    }

    const VkDeviceSize lightBufferSize = static_cast<VkDeviceSize>(sizeof(VolumetricLightRecord) * kMaxVolumetricLights);
    if (!createBuffer(v.lightRecordsBuffer, lightBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true)) {
        return false;
    }
    if (v.lightRecordsBuffer.mapped) {
        std::memset(v.lightRecordsBuffer.mapped, 0, static_cast<size_t>(lightBufferSize));
    }

    const VkDeviceSize clusterIndexSize = static_cast<VkDeviceSize>(sizeof(uint32_t) * kMaxClusterEntries);
    if (!createBuffer(v.clusterIndicesBuffer, clusterIndexSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        return false;
    }

    const VkDeviceSize froxelCount = static_cast<VkDeviceSize>(v.froxelGrid.width) * v.froxelGrid.height * v.froxelGrid.depth;
    const VkDeviceSize clusterOffsetSize = static_cast<VkDeviceSize>(sizeof(uint32_t) * (froxelCount + 1));
    if (!createBuffer(v.clusterOffsetsBuffer, clusterOffsetSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        return false;
    }

    const VkDeviceSize densityBufferSize = static_cast<VkDeviceSize>(sizeof(VolumetricDensityRecord) * kMaxDensityVolumes);
    if (!createBuffer(v.densityVolumesBuffer, densityBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true)) {
        return false;
    }
    if (v.densityVolumesBuffer.mapped) {
        std::memset(v.densityVolumesBuffer.mapped, 0, static_cast<size_t>(densityBufferSize));
    }

    if (!createVolumetricDescriptorSets()) {
        return false;
    }
    if (!createVolumetricPipelines()) {
        return false;
    }

    volumetricsReady_ = true;
    volumetricFrameIndex_ = 0;
    volumetricLightCount_ = 0;
    volumetricDensityCount_ = 0;

    if (postProcessingDescriptorSet_ != VK_NULL_HANDLE) {
        updatePostProcessingDescriptors();
    }
    return true;
}

void Renderer::destroyVolumetricResources() {
    auto& v = volumetrics_;

    if (!device_) {
        volumetricsReady_ = false;
        return;
    }

    if (v.clusterPipeline) { vkDestroyPipeline(device_, v.clusterPipeline, nullptr); v.clusterPipeline = VK_NULL_HANDLE; }
    if (v.densityPipeline) { vkDestroyPipeline(device_, v.densityPipeline, nullptr); v.densityPipeline = VK_NULL_HANDLE; }
    if (v.lightPipeline) { vkDestroyPipeline(device_, v.lightPipeline, nullptr); v.lightPipeline = VK_NULL_HANDLE; }
    if (v.raymarchPipeline) { vkDestroyPipeline(device_, v.raymarchPipeline, nullptr); v.raymarchPipeline = VK_NULL_HANDLE; }
    if (v.temporalPipeline) { vkDestroyPipeline(device_, v.temporalPipeline, nullptr); v.temporalPipeline = VK_NULL_HANDLE; }
    if (v.anamorphicBloomPipeline) { vkDestroyPipeline(device_, v.anamorphicBloomPipeline, nullptr); v.anamorphicBloomPipeline = VK_NULL_HANDLE; }
    if (v.pipelineLayout) { vkDestroyPipelineLayout(device_, v.pipelineLayout, nullptr); v.pipelineLayout = VK_NULL_HANDLE; }
    if (v.anamorphicBloomPipelineLayout) { vkDestroyPipelineLayout(device_, v.anamorphicBloomPipelineLayout, nullptr); v.anamorphicBloomPipelineLayout = VK_NULL_HANDLE; }

    if (v.descriptorPool) { vkDestroyDescriptorPool(device_, v.descriptorPool, nullptr); v.descriptorPool = VK_NULL_HANDLE; }
    if (v.anamorphicBloomDescriptorPool) { vkDestroyDescriptorPool(device_, v.anamorphicBloomDescriptorPool, nullptr); v.anamorphicBloomDescriptorPool = VK_NULL_HANDLE; }
    for (auto& layout : v.descriptorSetLayouts) {
        if (layout) {
            vkDestroyDescriptorSetLayout(device_, layout, nullptr);
            layout = VK_NULL_HANDLE;
        }
    }
    if (v.anamorphicBloomDescriptorLayout) { vkDestroyDescriptorSetLayout(device_, v.anamorphicBloomDescriptorLayout, nullptr); v.anamorphicBloomDescriptorLayout = VK_NULL_HANDLE; }

    destroyBuffer(v.constantsBuffer);
    destroyBuffer(v.clusterOffsetsBuffer);
    destroyBuffer(v.clusterIndicesBuffer);
    destroyBuffer(v.lightRecordsBuffer);
    destroyBuffer(v.densityVolumesBuffer);

    if (v.transmittanceView) { vkDestroyImageView(device_, v.transmittanceView, nullptr); v.transmittanceView = VK_NULL_HANDLE; }
    if (v.transmittanceImage) { vkDestroyImage(device_, v.transmittanceImage, nullptr); v.transmittanceImage = VK_NULL_HANDLE; }
    if (v.transmittanceMemory) { vkFreeMemory(device_, v.transmittanceMemory, nullptr); v.transmittanceMemory = VK_NULL_HANDLE; }

    if (v.historyView) { vkDestroyImageView(device_, v.historyView, nullptr); v.historyView = VK_NULL_HANDLE; }
    if (v.historyImage) { vkDestroyImage(device_, v.historyImage, nullptr); v.historyImage = VK_NULL_HANDLE; }
    if (v.historyMemory) { vkFreeMemory(device_, v.historyMemory, nullptr); v.historyMemory = VK_NULL_HANDLE; }

    if (v.scatteringView) { vkDestroyImageView(device_, v.scatteringView, nullptr); v.scatteringView = VK_NULL_HANDLE; }
    if (v.scatteringImage) { vkDestroyImage(device_, v.scatteringImage, nullptr); v.scatteringImage = VK_NULL_HANDLE; }
    if (v.scatteringMemory) { vkFreeMemory(device_, v.scatteringMemory, nullptr); v.scatteringMemory = VK_NULL_HANDLE; }

    if (v.lightView) { vkDestroyImageView(device_, v.lightView, nullptr); v.lightView = VK_NULL_HANDLE; }
    if (v.lightImage) { vkDestroyImage(device_, v.lightImage, nullptr); v.lightImage = VK_NULL_HANDLE; }
    if (v.lightMemory) { vkFreeMemory(device_, v.lightMemory, nullptr); v.lightMemory = VK_NULL_HANDLE; }

    if (v.densityView) { vkDestroyImageView(device_, v.densityView, nullptr); v.densityView = VK_NULL_HANDLE; }
    if (v.densityImage) { vkDestroyImage(device_, v.densityImage, nullptr); v.densityImage = VK_NULL_HANDLE; }
    if (v.densityMemory) { vkFreeMemory(device_, v.densityMemory, nullptr); v.densityMemory = VK_NULL_HANDLE; }

    if (v.anamorphicBloomView) { vkDestroyImageView(device_, v.anamorphicBloomView, nullptr); v.anamorphicBloomView = VK_NULL_HANDLE; }
    if (v.anamorphicBloomImage) { vkDestroyImage(device_, v.anamorphicBloomImage, nullptr); v.anamorphicBloomImage = VK_NULL_HANDLE; }
    if (v.anamorphicBloomMemory) { vkFreeMemory(device_, v.anamorphicBloomMemory, nullptr); v.anamorphicBloomMemory = VK_NULL_HANDLE; }

    if (v.anamorphicTempView) { vkDestroyImageView(device_, v.anamorphicTempView, nullptr); v.anamorphicTempView = VK_NULL_HANDLE; }
    if (v.anamorphicTempImage) { vkDestroyImage(device_, v.anamorphicTempImage, nullptr); v.anamorphicTempImage = VK_NULL_HANDLE; }
    if (v.anamorphicTempMemory) { vkFreeMemory(device_, v.anamorphicTempMemory, nullptr); v.anamorphicTempMemory = VK_NULL_HANDLE; }

    volumetricsReady_ = false;
    v.imagesInitialized = false;
    v.historyInitialized = false;

    if (postProcessingDescriptorSet_ != VK_NULL_HANDLE) {
        updatePostProcessingDescriptors();
    }
}

bool Renderer::createVolumetricDescriptorSets() {
    auto& v = volumetrics_;

    for (auto& layout : v.descriptorSetLayouts) {
        if (layout) {
            vkDestroyDescriptorSetLayout(device_, layout, nullptr);
            layout = VK_NULL_HANDLE;
        }
    }
    if (v.descriptorPool) {
        vkDestroyDescriptorPool(device_, v.descriptorPool, nullptr);
        v.descriptorPool = VK_NULL_HANDLE;
    }

    VkDescriptorSetLayoutBinding uniformBinding{};
    uniformBinding.binding = 0;
    uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBinding.descriptorCount = 1;
    uniformBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo uniformLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    uniformLayoutInfo.bindingCount = 1;
    uniformLayoutInfo.pBindings = &uniformBinding;
    if (vkCreateDescriptorSetLayout(device_, &uniformLayoutInfo, nullptr, &v.descriptorSetLayouts[0]) != VK_SUCCESS) {
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 6> imageBindings{};
    for (uint32_t i = 0; i < 5; ++i) {
        imageBindings[i].binding = i;
        imageBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        imageBindings[i].descriptorCount = 1;
        imageBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    imageBindings[5].binding = 5;
    imageBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    imageBindings[5].descriptorCount = 1;
    imageBindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo imageLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    imageLayoutInfo.bindingCount = static_cast<uint32_t>(imageBindings.size());
    imageLayoutInfo.pBindings = imageBindings.data();
    if (vkCreateDescriptorSetLayout(device_, &imageLayoutInfo, nullptr, &v.descriptorSetLayouts[1]) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayoutBinding bufferBindings[4]{};
    bufferBindings[0].binding = 0;
    bufferBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bufferBindings[0].descriptorCount = 1;
    bufferBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bufferBindings[1].binding = 1;
    bufferBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bufferBindings[1].descriptorCount = 1;
    bufferBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bufferBindings[2].binding = 2;
    bufferBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bufferBindings[2].descriptorCount = 1;
    bufferBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bufferBindings[3].binding = 3;
    bufferBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bufferBindings[3].descriptorCount = 1;
    bufferBindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo bufferLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    bufferLayoutInfo.bindingCount = 4;
    bufferLayoutInfo.pBindings = bufferBindings;
    if (vkCreateDescriptorSetLayout(device_, &bufferLayoutInfo, nullptr, &v.descriptorSetLayouts[2]) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize poolSizes[4]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; poolSizes[1].descriptorCount = 5;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; poolSizes[2].descriptorCount = 4;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; poolSizes[3].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 3;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &v.descriptorPool) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayout layouts[3] = { v.descriptorSetLayouts[0], v.descriptorSetLayouts[1], v.descriptorSetLayouts[2] };
    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = v.descriptorPool;
    allocInfo.descriptorSetCount = 3;
    allocInfo.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device_, &allocInfo, v.descriptorSets) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorBufferInfo constantsInfo{};
    constantsInfo.buffer = v.constantsBuffer.buffer;
    constantsInfo.offset = 0;
    constantsInfo.range = sizeof(VolumetricConstantsGPU);

    VkWriteDescriptorSet uniformWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    uniformWrite.dstSet = v.descriptorSets[0];
    uniformWrite.dstBinding = 0;
    uniformWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformWrite.descriptorCount = 1;
    uniformWrite.pBufferInfo = &constantsInfo;

    VkDescriptorImageInfo densityInfo{};
    densityInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    densityInfo.imageView = v.densityView;
    VkDescriptorImageInfo lightInfo{};
    lightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    lightInfo.imageView = v.lightView;
    VkDescriptorImageInfo scatteringInfo{};
    scatteringInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    scatteringInfo.imageView = v.scatteringView;
    VkDescriptorImageInfo transmittanceInfo{};
    transmittanceInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    transmittanceInfo.imageView = v.transmittanceView;
    VkDescriptorImageInfo historyInfo{};
    historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    historyInfo.imageView = v.historyView ? v.historyView : v.scatteringView;

    if (textureSampler_ == VK_NULL_HANDLE) {
        if (!createTextureSampler()) {
            return false;
        }
    }

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthInfo.imageView = depthImageView_ ? depthImageView_ : hdrColorView_;
    depthInfo.sampler = textureSampler_;

    VkWriteDescriptorSet imageWrites[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        imageWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imageWrites[i].dstSet = v.descriptorSets[1];
        imageWrites[i].dstBinding = i;
        imageWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        imageWrites[i].descriptorCount = 1;
    }
    imageWrites[0].pImageInfo = &densityInfo;
    imageWrites[1].pImageInfo = &lightInfo;
    imageWrites[2].pImageInfo = &scatteringInfo;
    imageWrites[3].pImageInfo = &transmittanceInfo;
    imageWrites[4].pImageInfo = &historyInfo;

    VkWriteDescriptorSet depthWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    depthWrite.dstSet = v.descriptorSets[1];
    depthWrite.dstBinding = 5;
    depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthWrite.descriptorCount = 1;
    depthWrite.pImageInfo = &depthInfo;

    VkDescriptorBufferInfo lightBufferInfo{};
    lightBufferInfo.buffer = v.lightRecordsBuffer.buffer;
    lightBufferInfo.offset = 0;
    lightBufferInfo.range = VK_WHOLE_SIZE;
    VkDescriptorBufferInfo clusterOffsetInfo{};
    clusterOffsetInfo.buffer = v.clusterOffsetsBuffer.buffer;
    clusterOffsetInfo.offset = 0;
    clusterOffsetInfo.range = VK_WHOLE_SIZE;
    VkDescriptorBufferInfo clusterIndexInfo{};
    clusterIndexInfo.buffer = v.clusterIndicesBuffer.buffer;
    clusterIndexInfo.offset = 0;
    clusterIndexInfo.range = VK_WHOLE_SIZE;
    VkDescriptorBufferInfo densityBufferInfo{};
    densityBufferInfo.buffer = v.densityVolumesBuffer.buffer;
    densityBufferInfo.offset = 0;
    densityBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet bufferWrites[4]{};
    bufferWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    bufferWrites[0].dstSet = v.descriptorSets[2];
    bufferWrites[0].dstBinding = 0;
    bufferWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bufferWrites[0].descriptorCount = 1;
    bufferWrites[0].pBufferInfo = &lightBufferInfo;

    bufferWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    bufferWrites[1].dstSet = v.descriptorSets[2];
    bufferWrites[1].dstBinding = 1;
    bufferWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bufferWrites[1].descriptorCount = 1;
    bufferWrites[1].pBufferInfo = &clusterOffsetInfo;

    bufferWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    bufferWrites[2].dstSet = v.descriptorSets[2];
    bufferWrites[2].dstBinding = 2;
    bufferWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bufferWrites[2].descriptorCount = 1;
    bufferWrites[2].pBufferInfo = &clusterIndexInfo;

    bufferWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    bufferWrites[3].dstSet = v.descriptorSets[2];
    bufferWrites[3].dstBinding = 3;
    bufferWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bufferWrites[3].descriptorCount = 1;
    bufferWrites[3].pBufferInfo = &densityBufferInfo;

    VkWriteDescriptorSet writes[10];
    uint32_t writeCount = 0;
    writes[writeCount++] = uniformWrite;
    for (uint32_t i = 0; i < 5; ++i) writes[writeCount++] = imageWrites[i];
    writes[writeCount++] = depthWrite;
    for (uint32_t i = 0; i < 4; ++i) writes[writeCount++] = bufferWrites[i];

    vkUpdateDescriptorSets(device_, writeCount, writes, 0, nullptr);

    return true;
}

bool Renderer::createVolumetricPipelines() {
    auto& v = volumetrics_;

    if (v.pipelineLayout) {
        vkDestroyPipelineLayout(device_, v.pipelineLayout, nullptr);
        v.pipelineLayout = VK_NULL_HANDLE;
    }
    if (v.clusterPipeline) { vkDestroyPipeline(device_, v.clusterPipeline, nullptr); v.clusterPipeline = VK_NULL_HANDLE; }
    if (v.densityPipeline) { vkDestroyPipeline(device_, v.densityPipeline, nullptr); v.densityPipeline = VK_NULL_HANDLE; }
    if (v.lightPipeline) { vkDestroyPipeline(device_, v.lightPipeline, nullptr); v.lightPipeline = VK_NULL_HANDLE; }
    if (v.raymarchPipeline) { vkDestroyPipeline(device_, v.raymarchPipeline, nullptr); v.raymarchPipeline = VK_NULL_HANDLE; }

    VkDescriptorSetLayout layouts[3] = {
        v.descriptorSetLayouts[0],
        v.descriptorSetLayouts[1],
        v.descriptorSetLayouts[2]
    };
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 3;
    layoutInfo.pSetLayouts = layouts;

    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.offset = 0;
    range.size = sizeof(VolumetricPushConstants);
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;

    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &v.pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    auto createPipeline = [&](const char* filename, VkPipeline& pipeline) -> bool {
        std::string base = std::string(PC_ENGINE_SHADER_DIR);
        std::vector<char> code = readShaderFile(base + "/" + filename);
        if (code.empty()) {
            return false;
        }

        VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleInfo.codeSize = code.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = module;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = v.pipelineLayout;

        VkResult res = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        vkDestroyShaderModule(device_, module, nullptr);
        return res == VK_SUCCESS;
    };

    if (!createPipeline("vol_cluster_build.comp.spv", v.clusterPipeline)) return false;
    if (!createPipeline("vol_density_inject.comp.spv", v.densityPipeline)) return false;
    if (!createPipeline("vol_light_inject.comp.spv", v.lightPipeline)) return false;
    if (!createPipeline("vol_raymarch.comp.spv", v.raymarchPipeline)) return false;
    if (!createPipeline("vol_temporal.comp.spv", v.temporalPipeline)) return false;

    // Create anamorphic bloom pipeline
    if (g_volumetricConfig.enableAnamorphicBloom) {
        // Create descriptor set layout for anamorphic bloom
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &v.anamorphicBloomDescriptorLayout) != VK_SUCCESS) {
            return false;
        }

        // Create pipeline layout with push constants
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(VolumetricPushConstants);

        VkDescriptorSetLayout bloomLayouts[2] = { v.descriptorSetLayouts[0], v.anamorphicBloomDescriptorLayout };
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayoutInfo.setLayoutCount = 2;
        pipelineLayoutInfo.pSetLayouts = bloomLayouts;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &v.anamorphicBloomPipelineLayout) != VK_SUCCESS) {
            return false;
        }

        // Create compute pipeline
        std::string base = std::string(PC_ENGINE_SHADER_DIR);
        std::vector<char> code = readShaderFile(base + "/vol_anamorphic_bloom.comp.spv");
        if (code.empty()) {
            return false;
        }

        VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleInfo.codeSize = code.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = module;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = v.anamorphicBloomPipelineLayout;

        VkResult res = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &v.anamorphicBloomPipeline);
        vkDestroyShaderModule(device_, module, nullptr);
        if (res != VK_SUCCESS) {
            return false;
        }

        // Create descriptor pool for bloom passes
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSize.descriptorCount = 4; // 2 sets * 2 images each

        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 2; // horizontal and vertical pass
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &v.anamorphicBloomDescriptorPool) != VK_SUCCESS) {
            return false;
        }

        // Allocate descriptor sets
        VkDescriptorSetLayout bloomSetLayouts[2] = { v.anamorphicBloomDescriptorLayout, v.anamorphicBloomDescriptorLayout };
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = v.anamorphicBloomDescriptorPool;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = bloomSetLayouts;
        if (vkAllocateDescriptorSets(device_, &allocInfo, v.anamorphicBloomDescriptorSets) != VK_SUCCESS) {
            return false;
        }

        // Update descriptor sets
        // Pass 0: scattering -> temp (horizontal blur)
        VkDescriptorImageInfo inputInfo0{};
        inputInfo0.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        inputInfo0.imageView = v.scatteringView;

        VkDescriptorImageInfo outputInfo0{};
        outputInfo0.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        outputInfo0.imageView = v.anamorphicTempView;

        VkWriteDescriptorSet writes0[2]{};
        writes0[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes0[0].dstSet = v.anamorphicBloomDescriptorSets[0];
        writes0[0].dstBinding = 0;
        writes0[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes0[0].descriptorCount = 1;
        writes0[0].pImageInfo = &inputInfo0;

        writes0[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes0[1].dstSet = v.anamorphicBloomDescriptorSets[0];
        writes0[1].dstBinding = 1;
        writes0[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes0[1].descriptorCount = 1;
        writes0[1].pImageInfo = &outputInfo0;

        vkUpdateDescriptorSets(device_, 2, writes0, 0, nullptr);

        // Pass 1: temp -> bloom (vertical blur)
        VkDescriptorImageInfo inputInfo1{};
        inputInfo1.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        inputInfo1.imageView = v.anamorphicTempView;

        VkDescriptorImageInfo outputInfo1{};
        outputInfo1.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        outputInfo1.imageView = v.anamorphicBloomView;

        VkWriteDescriptorSet writes1[2]{};
        writes1[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes1[0].dstSet = v.anamorphicBloomDescriptorSets[1];
        writes1[0].dstBinding = 0;
        writes1[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes1[0].descriptorCount = 1;
        writes1[0].pImageInfo = &inputInfo1;

        writes1[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes1[1].dstSet = v.anamorphicBloomDescriptorSets[1];
        writes1[1].dstBinding = 1;
        writes1[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes1[1].descriptorCount = 1;
        writes1[1].pImageInfo = &outputInfo1;

        vkUpdateDescriptorSets(device_, 2, writes1, 0, nullptr);
    }

    return true;
}

void Renderer::recordVolumetricPasses(VkCommandBuffer cmd) {
    if (!volumetricsEnabled_ || !volumetricsReady_) {
        return;
    }

    auto& v = volumetrics_;

    bool depthAvailable = depthImage_ != VK_NULL_HANDLE && depthImageView_ != VK_NULL_HANDLE;
    if (depthAvailable) {
        VkImageMemoryBarrier depthToRead{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        depthToRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToRead.image = depthImage_;
        depthToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthToRead.subresourceRange.baseMipLevel = 0;
        depthToRead.subresourceRange.levelCount = 1;
        depthToRead.subresourceRange.baseArrayLayer = 0;
        depthToRead.subresourceRange.layerCount = 1;
        depthToRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &depthToRead);
    }

    const bool firstUse = !v.imagesInitialized;

    std::vector<VkImageMemoryBarrier> beginBarriers;
    beginBarriers.reserve(5);

    auto pushBarrier = [&](VkImage image, VkImageLayout oldLayout) {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = oldLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        beginBarriers.push_back(barrier);
    };

    if (firstUse) {
        pushBarrier(v.densityImage, VK_IMAGE_LAYOUT_UNDEFINED);
        pushBarrier(v.lightImage, VK_IMAGE_LAYOUT_UNDEFINED);
        pushBarrier(v.scatteringImage, VK_IMAGE_LAYOUT_UNDEFINED);
        pushBarrier(v.transmittanceImage, VK_IMAGE_LAYOUT_UNDEFINED);
        pushBarrier(v.historyImage, VK_IMAGE_LAYOUT_UNDEFINED);
    } else {
        pushBarrier(v.scatteringImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pushBarrier(v.transmittanceImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    if (!beginBarriers.empty()) {
        vkCmdPipelineBarrier(cmd,
                             firstUse ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             static_cast<uint32_t>(beginBarriers.size()), beginBarriers.data());
    }

    if (firstUse) {
        v.imagesInitialized = true;
    }

    VkDescriptorSet sets[] = { v.descriptorSets[0], v.descriptorSets[1], v.descriptorSets[2] };

    VolumetricPushConstants constants{};
    constants.dims = glm::ivec4(
        static_cast<int32_t>(v.froxelGrid.width),
        static_cast<int32_t>(v.froxelGrid.height),
        static_cast<int32_t>(v.froxelGrid.depth),
        v.historyInitialized ? 1 : 0);
    constants.scalars0 = glm::vec4(time_, 1.0f, 
                                   g_volumetricConfig.baseFogDensity * volumetricFogDensityScale_, 
                                   g_volumetricConfig.fogAlbedo);
    constants.scalars1 = glm::vec4(g_volumetricConfig.temporalBlendAlpha,
                                   v.historyInitialized ? 1.0f : 0.0f,
                                   static_cast<float>(volumetricLightCount_),
                                   static_cast<float>(volumetricDensityCount_));
    constants.scalars2 = glm::vec4(g_volumetricConfig.phaseG, 0.8f, 1.2f, 0.0f);
    constants.scalars3 = glm::vec4(g_volumetricConfig.lightAttenuationFalloff, 0.0f, 0.0f, 0.0f);

    auto dispatch3D = [&](VkPipeline pipeline) {
        if (!pipeline) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, v.pipelineLayout, 0, 3, sets, 0, nullptr);
        vkCmdPushConstants(cmd, v.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);

        const uint32_t groupSizeX = 4;
        const uint32_t groupSizeY = 4;
        const uint32_t groupSizeZ = 4;
        uint32_t gx = (v.froxelGrid.width + groupSizeX - 1) / groupSizeX;
        uint32_t gy = (v.froxelGrid.height + groupSizeY - 1) / groupSizeY;
        uint32_t gz = (v.froxelGrid.depth + groupSizeZ - 1) / groupSizeZ;
        vkCmdDispatch(cmd, gx, gy, gz);

        VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             1, &barrier,
                             0, nullptr,
                             0, nullptr);
    };

    dispatch3D(v.clusterPipeline);
    dispatch3D(v.densityPipeline);
    dispatch3D(v.lightPipeline);

    const uint32_t localSize = 8;
    uint32_t gx = (v.raymarchExtent.width + localSize - 1) / localSize;
    uint32_t gy = (v.raymarchExtent.height + localSize - 1) / localSize;

    if (v.raymarchPipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, v.raymarchPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, v.pipelineLayout, 0, 3, sets, 0, nullptr);
        vkCmdPushConstants(cmd, v.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(cmd, gx, gy, 1);
    }

    if (v.temporalPipeline) {
        VolumetricPushConstants temporalConstants = constants;
        temporalConstants.dims = glm::ivec4(
            static_cast<int32_t>(v.raymarchExtent.width),
            static_cast<int32_t>(v.raymarchExtent.height),
            1,
            v.historyInitialized ? 1 : 0);
        temporalConstants.scalars1 = glm::vec4(constants.scalars1.x,
                                               v.historyInitialized ? 1.0f : 0.0f,
                                               static_cast<float>(volumetricLightCount_),
                                               static_cast<float>(volumetricDensityCount_));

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, v.temporalPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, v.pipelineLayout, 0, 3, sets, 0, nullptr);
        vkCmdPushConstants(cmd, v.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(temporalConstants), &temporalConstants);
        vkCmdDispatch(cmd, gx, gy, 1);

        v.historyInitialized = true;
    } else {
        v.historyInitialized = true;
    }

    // Anamorphic bloom passes (if enabled)
    if (g_volumetricConfig.enableAnamorphicBloom && v.anamorphicBloomPipeline) {
        // Transition bloom images to GENERAL layout
        VkImageMemoryBarrier bloomBarriers[2]{};
        bloomBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bloomBarriers[0].oldLayout = firstUse ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        bloomBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bloomBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bloomBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bloomBarriers[0].image = v.anamorphicTempImage;
        bloomBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bloomBarriers[0].subresourceRange.levelCount = 1;
        bloomBarriers[0].subresourceRange.layerCount = 1;
        bloomBarriers[0].srcAccessMask = 0;
        bloomBarriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        bloomBarriers[1] = bloomBarriers[0];
        bloomBarriers[1].image = v.anamorphicBloomImage;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             2, bloomBarriers);

        VolumetricPushConstants bloomConstants{};
        bloomConstants.dims = glm::ivec4(
            static_cast<int32_t>(v.raymarchExtent.width),
            static_cast<int32_t>(v.raymarchExtent.height),
            0, // Pass 0: horizontal
            0);
        bloomConstants.scalars0 = glm::vec4(
            g_volumetricConfig.anamorphicThreshold,
            g_volumetricConfig.anamorphicIntensity,
            g_volumetricConfig.anamorphicBlurRadius,
            g_volumetricConfig.anamorphicAspectRatio);
        bloomConstants.scalars1 = glm::vec4(
            static_cast<float>(g_volumetricConfig.anamorphicSampleCount),
            0.0f, 0.0f, 0.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, v.anamorphicBloomPipeline);

        // Pass 0: Horizontal blur (scattering -> temp)
        VkDescriptorSet bloomSets0[2] = { v.descriptorSets[0], v.anamorphicBloomDescriptorSets[0] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, v.anamorphicBloomPipelineLayout, 0, 2, bloomSets0, 0, nullptr);
        vkCmdPushConstants(cmd, v.anamorphicBloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bloomConstants), &bloomConstants);
        vkCmdDispatch(cmd, gx, gy, 1);

        // Barrier between passes
        VkMemoryBarrier memBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             1, &memBarrier,
                             0, nullptr,
                             0, nullptr);

        // Pass 1: Vertical blur (temp -> bloom)
        bloomConstants.dims.z = 1; // Pass 1: vertical
        VkDescriptorSet bloomSets1[2] = { v.descriptorSets[0], v.anamorphicBloomDescriptorSets[1] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, v.anamorphicBloomPipelineLayout, 0, 2, bloomSets1, 0, nullptr);
        vkCmdPushConstants(cmd, v.anamorphicBloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bloomConstants), &bloomConstants);
        vkCmdDispatch(cmd, gx, gy, 1);

        // Transition bloom result to shader read for compositing
        VkImageMemoryBarrier bloomReadBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        bloomReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        bloomReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bloomReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bloomReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bloomReadBarrier.image = v.anamorphicBloomImage;
        bloomReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bloomReadBarrier.subresourceRange.levelCount = 1;
        bloomReadBarrier.subresourceRange.layerCount = 1;
        bloomReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bloomReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &bloomReadBarrier);
    }

    VkImageMemoryBarrier endBarriers[2]{};
    for (int i = 0; i < 2; ++i) {
        endBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        endBarriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        endBarriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        endBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        endBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        endBarriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        endBarriers[i].subresourceRange.levelCount = 1;
        endBarriers[i].subresourceRange.layerCount = 1;
        endBarriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        endBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    endBarriers[0].image = v.scatteringImage;
    endBarriers[1].image = v.transmittanceImage;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         2, endBarriers);

    if (depthAvailable) {
        VkImageMemoryBarrier depthToAttachment{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        depthToAttachment.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthToAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToAttachment.image = depthImage_;
        depthToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthToAttachment.subresourceRange.baseMipLevel = 0;
        depthToAttachment.subresourceRange.levelCount = 1;
        depthToAttachment.subresourceRange.baseArrayLayer = 0;
        depthToAttachment.subresourceRange.layerCount = 1;
        depthToAttachment.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthToAttachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &depthToAttachment);
    }

    ++volumetricFrameIndex_;
}

void Renderer::updateVolumetricConstants(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane) {
    if (!volumetricsEnabled_ || !volumetricsReady_) {
        return;
    }

    auto& v = volumetrics_;
    if (!v.constantsBuffer.mapped) {
        return;
    }

    glm::mat4 viewProj = proj * view;
    glm::mat4 invView = glm::inverse(view);
    glm::mat4 invProj = glm::inverse(proj);
    glm::mat4 invViewProj = glm::inverse(viewProj);

    glm::mat4 prevViewProj = hasPrevViewProj_ ? prevViewProj_ : viewProj;
    glm::mat4 invPrevViewProj = glm::inverse(prevViewProj);
    glm::vec3 prevCamPos = hasPrevViewProj_ ? prevCameraPos_ : cameraPos_;

    float jitterX = halton(frameCounter_, 2u) - 0.5f;
    float jitterY = halton(frameCounter_, 3u) - 0.5f;

    VolumetricConstantsGPU gpu{};
    gpu.view = view;
    gpu.proj = proj;
    gpu.invView = invView;
    gpu.invProj = invProj;
    gpu.viewProj = viewProj;
    gpu.invViewProj = invViewProj;
    gpu.prevViewProj = prevViewProj;
    gpu.invPrevViewProj = invPrevViewProj;
    gpu.cameraPos = glm::vec4(cameraPos_, 1.0f);
    gpu.prevCameraPos = glm::vec4(prevCamPos, 1.0f);
    glm::vec3 lightDir = glm::normalize(skyLightDir_);
    gpu.lightDir = glm::vec4(lightDir, 0.0f);
    gpu.fogColorSigma = glm::vec4(fogColor_, fogDensity_);
    gpu.params = glm::vec4(nearPlane, farPlane,
                           v.raymarchExtent.width > 0 ? 1.0f / static_cast<float>(v.raymarchExtent.width) : 0.0f,
                           v.raymarchExtent.height > 0 ? 1.0f / static_cast<float>(v.raymarchExtent.height) : 0.0f);
    gpu.jitterFrameTime = glm::vec4(static_cast<float>(frameCounter_), time_, jitterX, jitterY);
    
    // Sky light (sun/moon) - independent directional light for atmosphere
    if (g_volumetricConfig.enableSkyLight) {
        glm::vec3 skyDir = glm::normalize(glm::vec3(
            g_volumetricConfig.skyLightDirectionX,
            g_volumetricConfig.skyLightDirectionY,
            g_volumetricConfig.skyLightDirectionZ
        ));
        gpu.skyLightDir = glm::vec4(skyDir, g_volumetricConfig.skyLightIntensity);
        gpu.skyLightColor = glm::vec4(
            g_volumetricConfig.skyLightColorR,
            g_volumetricConfig.skyLightColorG,
            g_volumetricConfig.skyLightColorB,
            g_volumetricConfig.skyLightScatteringBoost
        );
    } else {
        gpu.skyLightDir = glm::vec4(0.0f);
        gpu.skyLightColor = glm::vec4(0.0f);
    }

    std::memcpy(v.constantsBuffer.mapped, &gpu, sizeof(gpu));
}

void Renderer::updateVolumetricLights() {
    if (!volumetricsEnabled_ || !volumetricsReady_) {
        volumetricLightCount_ = 0;
        return;
    }

    auto& v = volumetrics_;
    if (!v.lightRecordsBuffer.mapped) {
        volumetricLightCount_ = 0;
        return;
    }

    CityGenerator* gen = static_cast<CityGenerator*>(cityGenerator_);
    if (!gen) {
        volumetricLightCount_ = 0;
        return;
    }

    const auto& neonLights = gen->getNeonLights();
    volumetricLights_.clear();
    volumetricLights_.reserve(kMaxVolumetricLights);

    // Smart light registration: only add lights that are:
    // 1. Within reasonable distance (distance culling)
    // 2. In or near the view frustum (with margin for off-screen influence)
    // 3. Within the froxel grid bounds
    
    const float maxDistance = g_volumetricConfig.maxLightDistance;
    const float maxDistSq = maxDistance * maxDistance;
    
    // Frustum margin: how far outside the frustum should we keep lights?
    // Larger margin = smoother when turning camera, but more lights
    const float frustumMargin = g_volumetricConfig.frustumMargin;
    const float nearKeepDistance = g_volumetricConfig.nearCameraAlwaysKeep;
    
    // Two-pass approach for better quality:
    // Pass 1: Add lights in frustum (high priority)
    // Pass 2: Add nearby lights outside frustum (lower priority, budget remaining)
    
    struct LightCandidate {
        glm::vec3 position;
        glm::vec3 color;
        float intensity;
        float radius;
        float distanceSq;
        bool inFrustum;
    };
    
    std::vector<LightCandidate> candidates;
    candidates.reserve(std::min(neonLights.size(), size_t(2048)));
    
    // Gather candidate lights
    for (const auto& light : neonLights) {
        glm::vec3 toLight = light.position - cameraPos_;
        float distSq = glm::dot(toLight, toLight);
        
        // Distance cull - skip if too far
        if (distSq > maxDistSq) {
            continue;
        }
        
        float radius = light.radius * g_volumetricConfig.neonRadiusMultiplier * volumetricLightRadiusScale_;
        float influenceRadius = radius * 20.0f; // Volumetric effect extends far
        
        // Check if light influence intersects frustum (with margin)
        bool inFrustum = viewFrustum_.intersectsSphere(light.position, influenceRadius + frustumMargin);
        
        // Only consider lights that are in frustum OR very close to camera
        if (!inFrustum && distSq > (nearKeepDistance * nearKeepDistance)) {
            continue;
        }
        
        LightCandidate candidate;
        candidate.position = light.position;
        candidate.color = light.color;
        candidate.intensity = light.intensity * g_volumetricConfig.neonIntensityMultiplier * volumetricLightIntensityScale_;
        candidate.radius = radius;
        candidate.distanceSq = distSq;
        candidate.inFrustum = inFrustum;
        
        candidates.push_back(candidate);
    }
    
    // Sort candidates: in-frustum first, then by distance
    std::sort(candidates.begin(), candidates.end(), 
              [](const LightCandidate& a, const LightCandidate& b) {
                  if (a.inFrustum != b.inFrustum) return a.inFrustum; // Prioritize in-frustum
                  return a.distanceSq < b.distanceSq; // Then by distance
              });
    
    // Add sorted candidates up to budget
    for (const auto& candidate : candidates) {
        volumetricLights_.push_back({ 
            glm::vec4(candidate.color, candidate.intensity), 
            glm::vec4(candidate.position, -candidate.radius) 
        });
        
        if (volumetricLights_.size() >= kMaxVolumetricLights) {
            break;
        }
    }

    // Add light volumes with same prioritization strategy
    if (volumetricLights_.size() < kMaxVolumetricLights) {
        const auto& lightVolumes = gen->getLightVolumes();
        
        struct VolumeCandidate {
            const LightVolume* volumePtr;
            float distanceSq;
            bool inFrustum;
            
            // Make this move/copy assignable for std::sort
            VolumeCandidate() = default;
            VolumeCandidate(const VolumeCandidate&) = default;
            VolumeCandidate& operator=(const VolumeCandidate&) = default;
        };
        
        std::vector<VolumeCandidate> volumeCandidates;
        volumeCandidates.reserve(std::min(lightVolumes.size(), size_t(512)));
        
        for (const auto& volume : lightVolumes) {
            glm::vec3 toVolume = volume.basePosition - cameraPos_;
            float distSq = glm::dot(toVolume, toVolume);
            
            // Distance cull
            if (distSq > maxDistSq) {
                continue;
            }
            
            float volumeRadius = volume.baseRadius * volumetricLightRadiusScale_;
            float volumeHeight = volume.height;
            glm::vec3 volumeCenter = volume.basePosition + glm::vec3(0, volumeHeight * 0.5f, 0);
            float influenceRadius = std::max(volumeRadius, volumeHeight * 0.5f) * 5.0f;
            
            // Frustum cull with margin
            bool inFrustum = viewFrustum_.intersectsSphere(volumeCenter, influenceRadius + frustumMargin);
            
            // Keep if in frustum or very close
            if (!inFrustum && distSq > (nearKeepDistance * nearKeepDistance)) {
                continue;
            }
            
            VolumeCandidate candidate;
            candidate.volumePtr = &volume;
            candidate.distanceSq = distSq;
            candidate.inFrustum = inFrustum;
            
            volumeCandidates.push_back(candidate);
        }
        
        // Sort: in-frustum first, then by distance
        std::sort(volumeCandidates.begin(), volumeCandidates.end(),
                  [](const VolumeCandidate& a, const VolumeCandidate& b) {
                      if (a.inFrustum != b.inFrustum) return a.inFrustum;
                      return a.distanceSq < b.distanceSq;
                  });
        
        // Add volume lights up to budget
        for (const auto& candidate : volumeCandidates) {
            if (volumetricLights_.size() >= kMaxVolumetricLights) {
                break;
            }
            
            const auto& volume = *candidate.volumePtr;
            
            if (volume.isCone) {
                // Cone/cylinder - sample vertically
                const int samples = 8;
                for (int i = 0; i < samples; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(samples - 1);
                    glm::vec3 pos = volume.basePosition + glm::vec3(0.0f, volume.height * t, 0.0f);
                    float radius = volume.baseRadius * (1.0f + t * 1.2f) * volumetricLightRadiusScale_;
                    float intensity = volume.intensity * (1.0f - t * 0.15f) * volumetricLightIntensityScale_;
                    volumetricLights_.push_back({ glm::vec4(volume.color, intensity), glm::vec4(pos, radius) });
                    if (volumetricLights_.size() >= kMaxVolumetricLights) {
                        break;
                    }
                }
            } else {
                // Cube - single centered light with negative radius to indicate box shape
                glm::vec3 pos = volume.basePosition + glm::vec3(0.0f, volume.height * 0.5f, 0.0f);
                float boxSize = volume.baseRadius * volumetricLightRadiusScale_;
                float intensity = volume.intensity * volumetricLightIntensityScale_;
                // Negative radius signals box shape in shader
                volumetricLights_.push_back({ glm::vec4(volume.color, intensity), glm::vec4(pos, -boxSize) });
            }
            if (volumetricLights_.size() >= kMaxVolumetricLights) {
                break;
            }
        }
    }

    volumetricLightCount_ = static_cast<uint32_t>(volumetricLights_.size());
    std::size_t bytesToCopy = volumetricLightCount_ * sizeof(VolumetricLightRecord);

    static bool printed = false;
    static int frameCount = 0;
    frameCount++;
    
    if (!printed && volumetricLightCount_ > 0) {
        printf("Volumetric lights: %u (neons: %zu, volumes: %zu)\n", 
               volumetricLightCount_, 
               gen->getNeonLights().size(),
               gen->getLightVolumes().size());
        
        // Print first few light records to verify box lights
        int boxCount = 0;
        int beamCount = 0;
        for (size_t i = 0; i < std::min(size_t(10), volumetricLights_.size()); ++i) {
            const auto& light = volumetricLights_[i];
            bool isBox = light.positionRadius.w < 0;
            if (isBox) {
                printf("  Light #%zu: BOX at (%.1f, %.1f, %.1f) size=%.1f intensity=%.1f\n",
                       i, light.positionRadius.x, light.positionRadius.y, light.positionRadius.z,
                       -light.positionRadius.w, light.colorIntensity.w);
                boxCount++;
            } else {
                beamCount++;
            }
        }
        printf("  Total: %d boxes, %d beams in first 10\n", boxCount, beamCount);
        printed = true;
    }
    
    // Print froxel grid bounds every 60 frames
    if (frameCount % 60 == 1) {
        const float cellSizeXZ = 4.0f;
        const float cellSizeY = 4.0f;
        glm::vec3 cellSize(cellSizeXZ, cellSizeY, cellSizeXZ);
        glm::vec3 gridExtent = glm::vec3(v.froxelGrid.width, v.froxelGrid.height, v.froxelGrid.depth) * cellSize;
        glm::vec3 gridCenter = glm::floor(cameraPos_ / cellSize) * cellSize;
        gridCenter.y = gridExtent.y * 0.5f;
        glm::vec3 gridMin = gridCenter - gridExtent * 0.5f;
        glm::vec3 gridMax = gridMin + gridExtent;
        
        printf("Froxel grid: camera=(%.1f,%.1f,%.1f) bounds=(%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n",
               cameraPos_.x, cameraPos_.y, cameraPos_.z,
               gridMin.x, gridMin.y, gridMin.z,
               gridMax.x, gridMax.y, gridMax.z);
    }

    if (bytesToCopy > 0) {
        std::memcpy(v.lightRecordsBuffer.mapped, volumetricLights_.data(), bytesToCopy);
    }
    if (volumetricLightCount_ < kMaxVolumetricLights) {
        std::size_t remaining = (kMaxVolumetricLights - volumetricLightCount_) * sizeof(VolumetricLightRecord);
        std::memset(static_cast<char*>(v.lightRecordsBuffer.mapped) + bytesToCopy, 0, remaining);
    }
}

void Renderer::updateVolumetricDensities() {
    if (!volumetricsEnabled_ || !volumetricsReady_) {
        volumetricDensityCount_ = 0;
        return;
    }

    auto& v = volumetrics_;
    if (!v.densityVolumesBuffer.mapped) {
        volumetricDensityCount_ = 0;
        return;
    }

    CityGenerator* gen = static_cast<CityGenerator*>(cityGenerator_);
    if (!gen) {
        volumetricDensityCount_ = 0;
        return;
    }

    volumetricDensities_.clear();
    volumetricDensities_.reserve(kMaxDensityVolumes);

    const auto& buildings = gen->getBuildings();
    const auto& lightVolumes = gen->getLightVolumes();
    const float halfWidth = (v.froxelGrid.width * kFroxelCellSizeXZ) * 0.5f;
    const float halfDepth = (v.froxelGrid.depth * kFroxelCellSizeXZ) * 0.5f;
    const float halfHeight = (v.froxelGrid.height * kFroxelCellSizeY) * 0.5f;

    auto worldToFroxel = [](float world, float camera, float halfExtent, float cellSize, int dim) {
        float local = world - camera + halfExtent;
        float coord = local / cellSize;
        return glm::clamp(coord, 0.0f, static_cast<float>(dim - 1));
    };

    for (const auto& building : buildings) {
        glm::vec3 minWorld = glm::vec3(
            building.position.x - building.size.x * 0.5f,
            building.position.y,
            building.position.z - building.size.z * 0.5f);
        glm::vec3 maxWorld = minWorld + building.size;

        float minX = worldToFroxel(minWorld.x, cameraPos_.x, halfWidth, kFroxelCellSizeXZ, v.froxelGrid.width);
        float maxX = worldToFroxel(maxWorld.x, cameraPos_.x, halfWidth, kFroxelCellSizeXZ, v.froxelGrid.width);
        float minY = worldToFroxel(minWorld.y, cameraPos_.y, halfHeight, kFroxelCellSizeY, v.froxelGrid.height);
        float maxY = worldToFroxel(maxWorld.y, cameraPos_.y, halfHeight, kFroxelCellSizeY, v.froxelGrid.height);
        float minZ = worldToFroxel(minWorld.z, cameraPos_.z, halfDepth, kFroxelCellSizeXZ, v.froxelGrid.depth);
        float maxZ = worldToFroxel(maxWorld.z, cameraPos_.z, halfDepth, kFroxelCellSizeXZ, v.froxelGrid.depth);

        if (minX >= v.froxelGrid.width || maxX <= 0.0f ||
            minY >= v.froxelGrid.height || maxY <= 0.0f ||
            minZ >= v.froxelGrid.depth || maxZ <= 0.0f) {
            continue;
        }

        VolumetricDensityRecord record;
        record.minBoundsSigma = glm::vec4(std::floor(minX), std::floor(minY), std::floor(minZ), 0.05f);
        record.maxBounds = glm::vec4(std::ceil(maxX), std::ceil(maxY), std::ceil(maxZ), 0.0f);
        record.albedo = glm::vec4(0.9f, 0.9f, 0.9f, 0.0f);

        volumetricDensities_.push_back(record);
        if (volumetricDensities_.size() >= kMaxDensityVolumes) {
            break;
        }
    }

    if (volumetricDensities_.size() < kMaxDensityVolumes) {
        for (const auto& volume : lightVolumes) {
            const int layers = volume.isCone ? 8 : 6;
            float stepHeight = volume.height / static_cast<float>(layers);
            for (int i = 0; i < layers && volumetricDensities_.size() < kMaxDensityVolumes; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(layers);
                float radiusScale = volume.isCone ? (1.0f - t * 0.4f) : 1.0f;
                float layerRadius = glm::max(0.8f, volume.baseRadius * radiusScale);
                glm::vec3 layerMinWorld = volume.basePosition + glm::vec3(-layerRadius, stepHeight * i, -layerRadius);
                glm::vec3 layerMaxWorld = layerMinWorld + glm::vec3(layerRadius * 2.0f, stepHeight + 2.0f, layerRadius * 2.0f);

                float minX = worldToFroxel(layerMinWorld.x, cameraPos_.x, halfWidth, kFroxelCellSizeXZ, v.froxelGrid.width);
                float maxX = worldToFroxel(layerMaxWorld.x, cameraPos_.x, halfWidth, kFroxelCellSizeXZ, v.froxelGrid.width);
                float minY = worldToFroxel(layerMinWorld.y, cameraPos_.y, halfHeight, kFroxelCellSizeY, v.froxelGrid.height);
                float maxY = worldToFroxel(layerMaxWorld.y, cameraPos_.y, halfHeight, kFroxelCellSizeY, v.froxelGrid.height);
                float minZ = worldToFroxel(layerMinWorld.z, cameraPos_.z, halfDepth, kFroxelCellSizeXZ, v.froxelGrid.depth);
                float maxZ = worldToFroxel(layerMaxWorld.z, cameraPos_.z, halfDepth, kFroxelCellSizeXZ, v.froxelGrid.depth);

                if (minX >= v.froxelGrid.width || maxX <= 0.0f ||
                    minY >= v.froxelGrid.height || maxY <= 0.0f ||
                    minZ >= v.froxelGrid.depth || maxZ <= 0.0f) {
                    continue;
                }

                VolumetricDensityRecord record;
                float sigmaBoost = 0.15f * (1.0f - t * 0.3f);
                record.minBoundsSigma = glm::vec4(std::floor(minX), std::floor(minY), std::floor(minZ), sigmaBoost);
                record.maxBounds = glm::vec4(std::ceil(maxX), std::ceil(maxY), std::ceil(maxZ), 0.0f);
                record.albedo = glm::vec4(volume.color * 1.2f, 0.0f);
                volumetricDensities_.push_back(record);
            }
            if (volumetricDensities_.size() >= kMaxDensityVolumes) {
                break;
            }
        }
    }

    volumetricDensityCount_ = static_cast<uint32_t>(volumetricDensities_.size());
    std::size_t bytesToCopy = volumetricDensityCount_ * sizeof(VolumetricDensityRecord);

    if (bytesToCopy > 0) {
        std::memcpy(v.densityVolumesBuffer.mapped, volumetricDensities_.data(), bytesToCopy);
    }
    if (volumetricDensityCount_ < kMaxDensityVolumes) {
        std::size_t remaining = (kMaxDensityVolumes - volumetricDensityCount_) * sizeof(VolumetricDensityRecord);
        std::memset(static_cast<char*>(v.densityVolumesBuffer.mapped) + bytesToCopy, 0, remaining);
    }
}

}


