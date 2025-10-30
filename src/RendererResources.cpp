#include "Renderer.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <string>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace pcengine {

bool Renderer::createUniformBuffers() {
    uniformBuffers_.resize(1);
    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = sizeof(UniformBufferObject);
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bi, nullptr, &uniformBuffers_[0].buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device_, uniformBuffers_[0].buffer, &req);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size; ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &uniformBuffers_[0].memory) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, uniformBuffers_[0].buffer, uniformBuffers_[0].memory, 0);
    return vkMapMemory(device_, uniformBuffers_[0].memory, 0, bi.size, 0, &uniformBuffers_[0].mapped) == VK_SUCCESS;
}

bool Renderer::createDescriptorPoolAndSets() {
    VkDescriptorPoolSize sizes[4]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[1].descriptorCount = kMaxBuildingTextures;
    sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[2].descriptorCount = 1; // Neon array texture
    sizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[3].descriptorCount = 1; // Shadow map
    
    VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pci.maxSets = 1; pci.poolSizeCount = 4; pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device_, &pci, nullptr, &descriptorPool_) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = descriptorPool_;
    ai.descriptorSetCount = 1;
    VkDescriptorSetLayout layouts[] = { descriptorSetLayout_ };
    ai.pSetLayouts = layouts;
    descriptorSets_.resize(1);
    if (vkAllocateDescriptorSets(device_, &ai, descriptorSets_.data()) != VK_SUCCESS) return false;

    VkDescriptorBufferInfo bi{}; bi.buffer = uniformBuffers_[0].buffer; bi.offset = 0; bi.range = sizeof(UniformBufferObject);
    VkWriteDescriptorSet write[4]{};
    write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write[0].dstSet = descriptorSets_[0]; write[0].dstBinding = 0; write[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write[0].descriptorCount = 1; write[0].pBufferInfo = &bi;
    
    std::vector<VkDescriptorImageInfo> imageInfos(kMaxBuildingTextures);
    for (int i = 0; i < kMaxBuildingTextures; ++i) {
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView = (i < numBuildingTextures_ && buildingTextureViews_[i]) ? buildingTextureViews_[i] : (numBuildingTextures_ > 0 ? buildingTextureViews_[0] : VK_NULL_HANDLE);
        imageInfos[i].sampler = textureSampler_;
    }
    write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write[1].dstSet = descriptorSets_[0]; write[1].dstBinding = 1; write[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write[1].descriptorCount = kMaxBuildingTextures; write[1].pImageInfo = imageInfos.data();

    VkDescriptorImageInfo neonInfo{};
    neonInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    neonInfo.imageView = neonArrayView_;
    neonInfo.sampler = textureSampler_;
    write[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write[2].dstSet = descriptorSets_[0]; write[2].dstBinding = 2; write[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write[2].descriptorCount = 1; write[2].pImageInfo = &neonInfo;
    
    VkDescriptorImageInfo shadowMapInfo{};
    shadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadowMapInfo.imageView = shadowMapView_;
    shadowMapInfo.sampler = shadowMapSampler_;
    write[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write[3].dstSet = descriptorSets_[0]; write[3].dstBinding = 3; write[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write[3].descriptorCount = 1; write[3].pImageInfo = &shadowMapInfo;
    
    vkUpdateDescriptorSets(device_, 4, write, 0, nullptr);
    return true;
}

bool Renderer::loadTextures() {
    // Try to load multiple building textures
    std::vector<std::string> textureFiles = {
        "textures/buildings/building_01.png",
        "textures/buildings/building_02.png"
    };
    
    numBuildingTextures_ = 0;
    for (size_t i = 0; i < textureFiles.size() && i < kMaxBuildingTextures; ++i) {
        if (createTextureImage(textureFiles[i], buildingTextures_[numBuildingTextures_], buildingTextureMemories_[numBuildingTextures_])) {
            if (createTextureImageView(buildingTextures_[numBuildingTextures_], buildingTextureViews_[numBuildingTextures_])) {
                numBuildingTextures_++;
            }
        }
    }
    
    if (numBuildingTextures_ == 0) {
        printf("Warning: No building textures loaded\n");
        return false;
    }
    
    if (!createTextureSampler()) {
        printf("Failed to create texture sampler\n");
        return false;
    }
    
    printf("Loaded %d building texture(s)\n", numBuildingTextures_);
    return true;
}

bool Renderer::loadNeonTextures() {
    // Create a simple procedural neon texture array
    const int width = 64;
    const int height = 64;
    const int layers = 4;
    std::vector<uint8_t> pixels(width * height * layers * 4);
    
    for (int layer = 0; layer < layers; ++layer) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (layer * width * height + y * width + x) * 4;
                // Create a glowing pattern with different colors per layer
                float ratioX = (x / (float)width - 0.5f) * 2.0f;
                float ratioY = (y / (float)height - 0.5f) * 2.0f;
                float dist = sqrt(ratioX * ratioX + ratioY * ratioY);
                float intensity = std::max(0.0f, 1.0f - dist);
                intensity = pow(intensity, 2.0f);
                
                // Different colors per layer
                float r = (layer == 0) ? 1.0f : (layer == 1) ? 0.0f : (layer == 2) ? 0.5f : 1.0f;
                float g = (layer == 0) ? 0.0f : (layer == 1) ? 1.0f : (layer == 2) ? 0.5f : 0.5f;
                float b = (layer == 0) ? 0.0f : (layer == 1) ? 0.0f : (layer == 2) ? 1.0f : 0.0f;
                
                pixels[idx + 0] = (uint8_t)(r * intensity * 255);
                pixels[idx + 1] = (uint8_t)(g * intensity * 255);
                pixels[idx + 2] = (uint8_t)(b * intensity * 255);
                pixels[idx + 3] = (uint8_t)(intensity * 255);
            }
        }
    }
    
    // Create texture array image
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
    
    if (vkCreateImage(device_, &imageInfo, nullptr, &neonArrayImage_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, neonArrayImage_, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &neonArrayMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, neonArrayImage_, neonArrayMemory_, 0);
    
    // Create staging buffer to upload pixel data
    VkBufferCreateInfo stagingBufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingBufferInfo.size = pixels.size();
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer stagingBuffer;
    if (vkCreateBuffer(device_, &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) return false;
    
    VkMemoryRequirements stagingMemReq;
    vkGetBufferMemoryRequirements(device_, stagingBuffer, &stagingMemReq);
    VkMemoryAllocateInfo stagingAllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    stagingAllocInfo.allocationSize = stagingMemReq.size;
    stagingAllocInfo.memoryTypeIndex = findMemoryType(stagingMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMemory;
    if (vkAllocateMemory(device_, &stagingAllocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        return false;
    }
    vkBindBufferMemory(device_, stagingBuffer, stagingMemory, 0);
    
    // Copy pixel data to staging buffer
    void* data = nullptr;
    if (vkMapMemory(device_, stagingMemory, 0, pixels.size(), 0, &data) != VK_SUCCESS) {
        vkFreeMemory(device_, stagingMemory, nullptr);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        return false;
    }
    std::memcpy(data, pixels.data(), pixels.size());
    vkUnmapMemory(device_, stagingMemory);
    
    // Transfer image layout and copy data (using command buffer)
    VkCommandBufferAllocateInfo cmdAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        vkFreeMemory(device_, stagingMemory, nullptr);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        return false;
    }
    
    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Transition to transfer destination
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = neonArrayImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = layers;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layers;
    region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, neonArrayImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    vkFreeMemory(device_, stagingMemory, nullptr);
    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    
    // Create image view for array
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = neonArrayImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = layers;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &neonArrayView_) != VK_SUCCESS) return false;
    
    numNeonTextures_ = layers;
    return true;
}

bool Renderer::createTextureImage(const std::string& filename, VkImage& image, VkDeviceMemory& memory) {
#if defined(__APPLE__)
    // Load image using CoreGraphics
    CFStringRef cfPath = CFStringCreateWithCString(kCFAllocatorDefault, filename.c_str(), kCFStringEncodingUTF8);
    if (!cfPath) return false;
    
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfPath, kCFURLPOSIXPathStyle, false);
    CFRelease(cfPath);
    if (!url) return false;
    
    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!source) return false;
    
    CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!cgImage) return false;
    
    size_t width = CGImageGetWidth(cgImage);
    size_t height = CGImageGetHeight(cgImage);
    size_t bytesPerPixel = 4;
    size_t bytesPerRow = width * bytesPerPixel;
    std::vector<uint8_t> pixels(bytesPerRow * height);
    
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels.data(), width, height, 8, bytesPerRow, colorSpace, 
                                                  static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(colorSpace);
    
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
    CGContextRelease(context);
    CGImageRelease(cgImage);
    
    // Create Vulkan image
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        
    if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS) return false;
        
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, image, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, image, memory, 0);
    
    // Copy pixel data
    void* data = nullptr;
    if (vkMapMemory(device_, memory, 0, memReq.size, 0, &data) != VK_SUCCESS) return false;
    std::memcpy(data, pixels.data(), pixels.size());
    vkUnmapMemory(device_, memory);
    
    return true;
#else
    return false; // Not implemented for other platforms yet
#endif
}

bool Renderer::createTextureImageView(VkImage image, VkImageView& imageView) {
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    return vkCreateImageView(device_, &viewInfo, nullptr, &imageView) == VK_SUCCESS;
}

bool Renderer::createTextureSampler() {
    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    return vkCreateSampler(device_, &samplerInfo, nullptr, &textureSampler_) == VK_SUCCESS;
}

}

