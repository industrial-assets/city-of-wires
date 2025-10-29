#include "Renderer.hpp"
#include "CityGenerator.hpp"

#include <GLFW/glfw3.h>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <filesystem>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace pcengine {

// For brevity and to fit in a single implementation turn, this file provides
// a minimal Vulkan setup sufficient to present a rotating cube with a single
// uniform buffer and depth testing. Real engines would factor this by systems.

static VkBool32 debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                              VkDebugUtilsMessageTypeFlagsEXT type,
                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                              void* userData) {
    (void)severity; (void)type; (void)userData;
    (void)data;
    return VK_FALSE;
}

bool Renderer::initialize(GLFWwindow* window) {
    if (!createInstance()) return false;

    if (!createSurface(window)) return false;
    if (!pickPhysicalDevice()) return false;
    if (!createDevice()) return false;
    if (!createSwapchain()) return false;
    if (!createImageViews()) return false;
    if (!createRenderPass()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPoolAndBuffers()) return false;
    
    // Create post-processing resources
    if (!createHDRRenderTarget()) return false;
    if (!createBloomTextures()) return false;
    if (!createPostProcessingPipeline()) return false;
    if (!createPostProcessingDescriptorSet()) return false;
    
    // Initialize city generation
    cityGenerator_ = new CityGenerator();
    static_cast<CityGenerator*>(cityGenerator_)->setGridSpacing(8.0f);
    static_cast<CityGenerator*>(cityGenerator_)->generateCity(42);
    
    if (!loadTextures()) return false;
    if (!loadNeonTextures()) return false;
    if (!createCityGeometry()) return false;
    if (!createNeonGeometry()) return false;
    if (!createNeonPipeline()) return false;
    if (!createShadowMapResources()) return false;
    if (!createUniformBuffers()) return false;
    if (!createDescriptorPoolAndSets()) return false;
    if (!createSyncObjects()) return false;
    
    // Initialize hot reloading
    lastShaderCheck_ = std::chrono::steady_clock::now();
    std::string shaderDir = std::string(PC_ENGINE_SHADER_DIR);
    std::filesystem::path vertPath = shaderDir + "/city.vert";
    std::filesystem::path fragPath = shaderDir + "/city.frag";
    
    if (std::filesystem::exists(vertPath)) {
        lastVertTime_ = std::filesystem::last_write_time(vertPath);
    }
    if (std::filesystem::exists(fragPath)) {
        lastFragTime_ = std::filesystem::last_write_time(fragPath);
    }
    
    // Initialize camera vectors based on initial yaw and pitch
    updateCameraVectors();
    
    return true;
}

void Renderer::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    // Clean up city generator
    if (cityGenerator_) {
        delete static_cast<CityGenerator*>(cityGenerator_);
        cityGenerator_ = nullptr;
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, inFlightFence_, nullptr);
        vkDestroySemaphore(device_, renderFinishedSemaphore_, nullptr);
        vkDestroySemaphore(device_, imageAvailableSemaphore_, nullptr);

        for (auto& ub : uniformBuffers_) {
            if (ub.mapped) { vkUnmapMemory(device_, ub.memory); }
            if (ub.buffer) vkDestroyBuffer(device_, ub.buffer, nullptr);
            if (ub.memory) vkFreeMemory(device_, ub.memory, nullptr);
        }
        uniformBuffers_.clear();

        if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (descriptorSetLayout_) vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);

        // Clean up shadow map resources
        if (shadowMapFramebuffer_) vkDestroyFramebuffer(device_, shadowMapFramebuffer_, nullptr);
        if (shadowMapRenderPass_) vkDestroyRenderPass(device_, shadowMapRenderPass_, nullptr);
        if (shadowMapSampler_) vkDestroySampler(device_, shadowMapSampler_, nullptr);
        if (shadowMapView_) vkDestroyImageView(device_, shadowMapView_, nullptr);
        if (shadowMapImage_) vkDestroyImage(device_, shadowMapImage_, nullptr);
        if (shadowMapMemory_) vkFreeMemory(device_, shadowMapMemory_, nullptr);

        // Clean up post-processing resources
        if (postProcessingPipeline_) vkDestroyPipeline(device_, postProcessingPipeline_, nullptr);
        if (postProcessingLayout_) vkDestroyPipelineLayout(device_, postProcessingLayout_, nullptr);
        if (postProcessingDescriptorPool_) vkDestroyDescriptorPool(device_, postProcessingDescriptorPool_, nullptr);
        if (postProcessingDescriptorLayout_) vkDestroyDescriptorSetLayout(device_, postProcessingDescriptorLayout_, nullptr);
        
        if (postProcessingUBO_.mapped) vkUnmapMemory(device_, postProcessingUBO_.memory);
        if (postProcessingUBO_.buffer) vkDestroyBuffer(device_, postProcessingUBO_.buffer, nullptr);
        if (postProcessingUBO_.memory) vkFreeMemory(device_, postProcessingUBO_.memory, nullptr);
        
        // Clean up bloom resources
        for (auto& fb : bloomFramebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
        for (auto& view : bloomViews_) vkDestroyImageView(device_, view, nullptr);
        for (auto& memory : bloomMemories_) vkFreeMemory(device_, memory, nullptr);
        for (auto& image : bloomImages_) vkDestroyImage(device_, image, nullptr);
        if (bloomRenderPass_) vkDestroyRenderPass(device_, bloomRenderPass_, nullptr);
        
        // Clean up HDR resources
        if (hdrFramebuffer_) vkDestroyFramebuffer(device_, hdrFramebuffer_, nullptr);
        if (hdrColorView_) vkDestroyImageView(device_, hdrColorView_, nullptr);
        if (hdrColorImage_) vkDestroyImage(device_, hdrColorImage_, nullptr);
        if (hdrColorMemory_) vkFreeMemory(device_, hdrColorMemory_, nullptr);
        if (hdrRenderPass_) vkDestroyRenderPass(device_, hdrRenderPass_, nullptr);

        // Clean up texture resources
        if (textureSampler_) vkDestroySampler(device_, textureSampler_, nullptr);
        for (int i = 0; i < kMaxBuildingTextures; ++i) {
            if (buildingTextureViews_[i]) vkDestroyImageView(device_, buildingTextureViews_[i], nullptr);
            if (buildingTextures_[i]) vkDestroyImage(device_, buildingTextures_[i], nullptr);
            if (buildingTextureMemories_[i]) vkFreeMemory(device_, buildingTextureMemories_[i], nullptr);
            buildingTextureViews_[i] = VK_NULL_HANDLE;
            buildingTextures_[i] = VK_NULL_HANDLE;
            buildingTextureMemories_[i] = VK_NULL_HANDLE;
        }
        if (neonArrayView_) vkDestroyImageView(device_, neonArrayView_, nullptr);
        if (neonArrayImage_) vkDestroyImage(device_, neonArrayImage_, nullptr);
        if (neonArrayMemory_) vkFreeMemory(device_, neonArrayMemory_, nullptr);
        if (neonPipeline_) vkDestroyPipeline(device_, neonPipeline_, nullptr);

        if (indexBuffer_) vkDestroyBuffer(device_, indexBuffer_, nullptr);
        if (indexBufferMemory_) vkFreeMemory(device_, indexBufferMemory_, nullptr);
        if (vertexBuffer_) vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        if (vertexBufferMemory_) vkFreeMemory(device_, vertexBufferMemory_, nullptr);

        if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);

        cleanupSwapchain();

        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void Renderer::waitIdle() {
    if (device_) vkDeviceWaitIdle(device_);
}

void Renderer::update(float deltaSeconds) {
    time_ += deltaSeconds;
    
    // Process movement based on current input
    processMovement(deltaSeconds);
    
    checkShaderReload();
}

void Renderer::drawFrame() {
    vkWaitForFences(device_, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &inFlightFence_);

    uint32_t imageIndex = 0;
    VkResult acquireRes = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailableSemaphore_, VK_NULL_HANDLE, &imageIndex);
    if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }

    // Update UBO
    UniformBufferObject ubo{};
    
    // Identity model matrix (city is already positioned)
    glm::mat4 model = glm::mat4(1.0f);
    
    // View matrix - use camera look direction
    glm::vec3 target = cameraPos_ + cameraFront_;
    glm::mat4 view = glm::lookAt(cameraPos_, target, cameraUp_);
    
    // Projection matrix - flip Y for Vulkan coordinate system
    float aspect = (float)swapchainExtent_.width / (float)swapchainExtent_.height;
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
    proj[1][1] *= -1.0f; // Flip Y coordinate for Vulkan
    
    // Calculate light space matrix for shadow mapping
    // Use sun direction to position light looking at scene
    glm::vec3 lightDir = glm::normalize(-skyLightDir_); // Direction from light to scene
    glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 0.0f) - lightDir * 300.0f; // Position light far away in opposite direction
    glm::vec3 lightTarget = glm::vec3(0.0f, 0.0f, 0.0f); // Center of city
    glm::vec3 lightUp = glm::abs(lightDir.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, lightTarget, lightUp);
    
    // Orthographic projection for directional light
    float lightSize = 200.0f; // Size of shadow map coverage
    glm::mat4 lightProj = glm::ortho(-lightSize, lightSize, -lightSize, lightSize, 1.0f, 500.0f);
    lightProj[1][1] *= -1.0f; // Flip Y for Vulkan
    
    glm::mat4 lightSpaceMatrix = lightProj * lightView;
    
    // Copy matrices to UBO
    std::memcpy(ubo.model, &model[0][0], sizeof(float) * 16);
    std::memcpy(ubo.view, &view[0][0], sizeof(float) * 16);
    std::memcpy(ubo.proj, &proj[0][0], sizeof(float) * 16);
    std::memcpy(ubo.lightSpaceMatrix, &lightSpaceMatrix[0][0], sizeof(float) * 16);
    
    // Atmospheric parameters
    ubo.cameraPos[0] = cameraPos_.x;
    ubo.cameraPos[1] = cameraPos_.y;
    ubo.cameraPos[2] = cameraPos_.z;
    ubo.time = time_;
    
    ubo.fogColor[0] = fogColor_.x;
    ubo.fogColor[1] = fogColor_.y;
    ubo.fogColor[2] = fogColor_.z;
    ubo.fogDensity = fogDensity_;
    
    ubo.skyLightDir[0] = skyLightDir_.x;
    ubo.skyLightDir[1] = skyLightDir_.y;
    ubo.skyLightDir[2] = skyLightDir_.z;
    ubo.skyLightIntensity = skyLightIntensity_;
    ubo.texTiling = 1.0f;
    ubo.textureCount = static_cast<float>(std::max(1, numBuildingTextures_));
    
    std::memcpy(uniformBuffers_[0].mapped, &ubo, sizeof(ubo));
    
    // Update post-processing UBO
    if (postProcessingUBO_.mapped) {
        PostProcessingUBO ppUBO{};
        ppUBO.exposure = exposure_;
        ppUBO.bloomThreshold = bloomThreshold_;
        ppUBO.bloomIntensity = bloomIntensity_;
        ppUBO.fogHeightFalloff = fogHeightFalloff_;
        ppUBO.fogHeightOffset = fogHeightOffset_;
        ppUBO.vignetteStrength = vignetteStrength_;
        ppUBO.vignetteRadius = vignetteRadius_;
        ppUBO.grainStrength = grainStrength_;
        ppUBO.contrast = contrast_;
        ppUBO.saturation = saturation_;
        ppUBO.colorTemperature = colorTemperature_;
        ppUBO.lightShaftIntensity = lightShaftIntensity_;
        ppUBO.lightShaftDensity = lightShaftDensity_;
        std::memcpy(postProcessingUBO_.mapped, &ppUBO, sizeof(ppUBO));
    }

    vkResetCommandBuffer(commandBuffers_[imageIndex], 0);
    recordCommandBuffer(commandBuffers_[imageIndex], imageIndex);

    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore_;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore_;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFence_);

    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore_;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentRes = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }
}

// ---- Vulkan setup helpers (implementations kept compact) ----

// Moved to RendererSwapchain.cpp


// Moved to RendererPipelines.cpp

// Moved to RendererPipelines.cpp

// Moved to RendererVulkanCore.cpp

bool Renderer::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = depthFormat;
    ci.extent = { swapchainExtent_.width, swapchainExtent_.height, 1 };
    ci.mipLevels = 1; ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (vkCreateImage(device_, &ci, nullptr, &depthImage_) != VK_SUCCESS) return false;
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device_, depthImage_, &req);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &depthImageMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, depthImage_, depthImageMemory_, 0);

    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = depthImage_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = depthFormat;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &vi, nullptr, &depthImageView_) != VK_SUCCESS) return false;
    return true;
}

bool Renderer::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
        std::array<VkImageView,2> atts{ swapchainImageViews_[i], depthImageView_ };
        VkFramebufferCreateInfo ci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        ci.renderPass = renderPass_;
        ci.attachmentCount = (uint32_t)atts.size(); ci.pAttachments = atts.data();
        ci.width = swapchainExtent_.width; ci.height = swapchainExtent_.height; ci.layers = 1;
        if (vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

bool Renderer::createCommandPoolAndBuffers() {
    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.queueFamilyIndex = graphicsQueueFamily_;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device_, &pci, nullptr, &commandPool_) != VK_SUCCESS) return false;
    commandBuffers_.resize(framebuffers_.size());
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = commandPool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = (uint32_t)commandBuffers_.size();
    return vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()) == VK_SUCCESS;
}

bool Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    return vkCreateSemaphore(device_, &si, nullptr, &imageAvailableSemaphore_) == VK_SUCCESS &&
           vkCreateSemaphore(device_, &si, nullptr, &renderFinishedSemaphore_) == VK_SUCCESS &&
           vkCreateFence(device_, &fi, nullptr, &inFlightFence_) == VK_SUCCESS;
}

// Moved to RendererVulkanCore.cpp

bool Renderer::createVertexIndexBuffers() {
    // Cube vertices: position(x,y,z) + color(r,g,b)
    const float v[] = {
        // Front
        -1,-1, 1, 1,0,0,
         1,-1, 1, 0,1,0,
         1, 1, 1, 0,0,1,
        -1, 1, 1, 1,1,0,
        // Back
        -1,-1,-1, 1,0,1,
         1,-1,-1, 0,1,1,
         1, 1,-1, 1,1,1,
        -1, 1,-1, 0.2f,0.2f,0.2f,
    };
    const uint16_t idx[] = {
        0,1,2, 2,3,0, // front
        1,5,6, 6,2,1, // right
        5,4,7, 7,6,5, // back
        4,0,3, 3,7,4, // left
        3,2,6, 6,7,3, // top
        4,5,1, 1,0,4  // bottom
    };

    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = sizeof(v);
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bi, nullptr, &vertexBuffer_) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device_, vertexBuffer_, &req);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size; ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &vertexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, vertexBuffer_, vertexBufferMemory_, 0);
    void* data = nullptr; vkMapMemory(device_, vertexBufferMemory_, 0, bi.size, 0, &data); std::memcpy(data, v, sizeof(v)); vkUnmapMemory(device_, vertexBufferMemory_);

    VkBufferCreateInfo ib{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    ib.size = sizeof(idx);
    ib.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &ib, nullptr, &indexBuffer_) != VK_SUCCESS) return false;
    vkGetBufferMemoryRequirements(device_, indexBuffer_, &req);
    ai.allocationSize = req.size; ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &ai, nullptr, &indexBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, indexBuffer_, indexBufferMemory_, 0);
    data = nullptr; vkMapMemory(device_, indexBufferMemory_, 0, ib.size, 0, &data); std::memcpy(data, idx, sizeof(idx)); vkUnmapMemory(device_, indexBufferMemory_);
    return true;
}

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

bool Renderer::createShadowMapResources() {
    // Create shadow map image
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = shadowMapSize_;
    imageInfo.extent.height = shadowMapSize_;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    if (vkCreateImage(device_, &imageInfo, nullptr, &shadowMapImage_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, shadowMapImage_, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &shadowMapMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, shadowMapImage_, shadowMapMemory_, 0);
    
    // Create shadow map image view
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = shadowMapImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &shadowMapView_) != VK_SUCCESS) return false;
    
    // Create shadow map sampler
    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &shadowMapSampler_) != VK_SUCCESS) return false;
    
    // Create shadow map render pass
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;
    
    VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &depthAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(device_, &rpInfo, nullptr, &shadowMapRenderPass_) != VK_SUCCESS) return false;
    
    // Create shadow map framebuffer
    VkFramebufferCreateInfo fbInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbInfo.renderPass = shadowMapRenderPass_;
        fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &shadowMapView_;
    fbInfo.width = shadowMapSize_;
    fbInfo.height = shadowMapSize_;
        fbInfo.layers = 1;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &shadowMapFramebuffer_) != VK_SUCCESS) return false;
    
    return true;
}

void Renderer::renderShadowMap(VkCommandBuffer cmd) {
    // Transition shadow map to depth attachment layout
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = shadowMapImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    VkRenderPassBeginInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpInfo.renderPass = shadowMapRenderPass_;
    rpInfo.framebuffer = shadowMapFramebuffer_;
    rpInfo.renderArea.extent = {shadowMapSize_, shadowMapSize_};
    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearValue;
    
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Render city geometry from light's perspective
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
    VkDeviceSize offs = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &cityVertexBuffer_, &offs);
    vkCmdBindIndexBuffer(cmd, cityIndexBuffer_, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[0], 0, nullptr);
    vkCmdDrawIndexed(cmd, cityIndexCount_, 1, 0, 0, 0);
    
    vkCmdEndRenderPass(cmd);
    
    // Transition shadow map back to read-only layout
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &bi);
    
    // Render shadow map first
    renderShadowMap(cmd);
    
    // Render directly to swapchain with enhanced atmospheric effects
    std::array<VkClearValue,2> clears{}; 
    clears[0].color = { {fogColor_.x, fogColor_.y, fogColor_.z, 1.0f} }; // Fog-colored background
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp.renderPass = renderPass_;
    rp.framebuffer = framebuffers_[imageIndex];
    rp.renderArea.extent = swapchainExtent_;
    rp.clearValueCount = (uint32_t)clears.size(); rp.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    
    // Render city buildings
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
    VkDeviceSize offs = 0; 
    vkCmdBindVertexBuffers(cmd, 0, 1, &cityVertexBuffer_, &offs);
    vkCmdBindIndexBuffer(cmd, cityIndexBuffer_, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[0], 0, nullptr);
    vkCmdDrawIndexed(cmd, cityIndexCount_, 1, 0, 0, 0);
    
    // Render neon lights with premultiplied alpha blending
    if (neonPipeline_ && neonIndexCount_ > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, neonPipeline_);
        vkCmdBindVertexBuffers(cmd, 0, 1, &neonVertexBuffer_, &offs);
        vkCmdBindIndexBuffer(cmd, neonIndexBuffer_, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[0], 0, nullptr);
        vkCmdDrawIndexed(cmd, neonIndexCount_, 1, 0, 0, 0);
    }
    
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

// Moved to RendererSwapchain.cpp

void Renderer::checkShaderReload() {
    if (!shaderReloadEnabled_) return;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastShaderCheck_);
    
    // Check every 100ms to avoid excessive file system calls
    if (elapsed.count() < 100) return;
    
    lastShaderCheck_ = now;
    
    std::string shaderDir = std::string(PC_ENGINE_SHADER_DIR);
    std::filesystem::path vertPath = shaderDir + "/city.vert";
    std::filesystem::path fragPath = shaderDir + "/city.frag";
    
    bool needsReload = false;
    
    if (std::filesystem::exists(vertPath)) {
        auto currentTime = std::filesystem::last_write_time(vertPath);
        if (currentTime > lastVertTime_) {
            lastVertTime_ = currentTime;
            needsReload = true;
        }
    }
    
    if (std::filesystem::exists(fragPath)) {
        auto currentTime = std::filesystem::last_write_time(fragPath);
        if (currentTime > lastFragTime_) {
            lastFragTime_ = currentTime;
            needsReload = true;
        }
    }
    
    if (needsReload) {
        printf("Hot reloading shaders...\n");
        if (reloadShaders()) {
            printf("Shader reload successful!\n");
        } else {
            printf("Shader reload failed!\n");
        }
    }
}

// Moved to RendererPipelines.cpp

void Renderer::toggleShaderReload() {
    shaderReloadEnabled_ = !shaderReloadEnabled_;
    printf("Shader hot reloading %s\n", shaderReloadEnabled_ ? "enabled" : "disabled");
}

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

// Moved to RendererCamera.cpp

// Moved to RendererCamera.cpp

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

bool Renderer::createHDRRenderTarget() {
    // Create HDR color image (RGBA16F for HDR)
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent_.width;
    imageInfo.extent.height = swapchainExtent_.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    if (vkCreateImage(device_, &imageInfo, nullptr, &hdrColorImage_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, hdrColorImage_, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &hdrColorMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, hdrColorImage_, hdrColorMemory_, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = hdrColorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &hdrColorView_) != VK_SUCCESS) return false;
    
    // Create render pass for HDR rendering
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    std::array<VkAttachmentDescription, 2> atts{ colorAttachment, depthAttachment };
    VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = atts.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(device_, &rpInfo, nullptr, &hdrRenderPass_) != VK_SUCCESS) return false;
    
    // Create framebuffer (reuse depth buffer from main render)
    std::array<VkImageView, 2> attachments{ hdrColorView_, depthImageView_ };
    VkFramebufferCreateInfo fbInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbInfo.renderPass = hdrRenderPass_;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = swapchainExtent_.width;
    fbInfo.height = swapchainExtent_.height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &hdrFramebuffer_) != VK_SUCCESS) return false;
    
    return true;
}

bool Renderer::createBloomTextures() {
    // Create a single bloom texture for simplicity
    bloomImages_.resize(1);
    bloomMemories_.resize(1);
    bloomViews_.resize(1);
    bloomFramebuffers_.resize(1);
    
    // Create bloom image (half resolution)
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent_.width / 2;
    imageInfo.extent.height = swapchainExtent_.height / 2;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    if (vkCreateImage(device_, &imageInfo, nullptr, &bloomImages_[0]) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, bloomImages_[0], &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &bloomMemories_[0]) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, bloomImages_[0], bloomMemories_[0], 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = bloomImages_[0];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &bloomViews_[0]) != VK_SUCCESS) return false;
    
    // Create bloom render pass
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    
    VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    if (vkCreateRenderPass(device_, &rpInfo, nullptr, &bloomRenderPass_) != VK_SUCCESS) return false;
    
    // Create framebuffer
    VkFramebufferCreateInfo fbInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbInfo.renderPass = bloomRenderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &bloomViews_[0];
    fbInfo.width = swapchainExtent_.width / 2;
    fbInfo.height = swapchainExtent_.height / 2;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &bloomFramebuffers_[0]) != VK_SUCCESS) return false;
    
    return true;
}

static std::vector<char> readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<char> data((size_t)len);
    fread(data.data(), 1, data.size(), f); fclose(f);
    return data;
}

bool Renderer::createPostProcessingPipeline() {
    std::string base = std::string(PC_ENGINE_SHADER_DIR);
    auto vertCode = readFile(base + "/postprocess.vert.spv");
    auto fragCode = readFile(base + "/postprocess.frag.spv");
    if (vertCode.empty() || fragCode.empty()) return false;
    
    auto createShader = [&](const std::vector<char>& code) {
        VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule m{}; vkCreateShaderModule(device_, &ci, nullptr, &m); return m;
    };
    VkShaderModule vert = createShader(vertCode);
    VkShaderModule frag = createShader(fragCode);
    
    VkPipelineShaderStageCreateInfo vs{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vs.stage = VK_SHADER_STAGE_VERTEX_BIT; vs.module = vert; vs.pName = "main";
    VkPipelineShaderStageCreateInfo fs{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = frag; fs.pName = "main";
    VkPipelineShaderStageCreateInfo stages[2] = { vs, fs };
    
    // Fullscreen quad - vec2 position input
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(float) * 2;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attr{};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32_SFLOAT;
    attr.offset = 0;
    
    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;
    
    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkViewport vp{};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width = (float)swapchainExtent_.width;
    vp.height = (float)swapchainExtent_.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    VkRect2D scissor{}; scissor.extent = swapchainExtent_;
    VkPipelineViewportStateCreateInfo vps{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1; vps.pViewports = &vp;
    vps.scissorCount = 1; vps.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    
    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cbAtt;
    
    // Create descriptor set layout for post-processing
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding hdrTexBinding{};
    hdrTexBinding.binding = 1;
    hdrTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hdrTexBinding.descriptorCount = 1;
    hdrTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding bloomTexBinding{};
    bloomTexBinding.binding = 2;
    bloomTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bloomTexBinding.descriptorCount = 1;
    bloomTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding bindings[3] = { uboBinding, hdrTexBinding, bloomTexBinding };
    VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 3;
    dslci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &dslci, nullptr, &postProcessingDescriptorLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return false;
    }
    
    VkDescriptorSetLayout setLayouts[] = { postProcessingDescriptorLayout_ };
    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = setLayouts;
    VkPipelineLayout postLayout;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &postLayout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device_, postProcessingDescriptorLayout_, nullptr);
        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return false;
    }
    postProcessingLayout_ = postLayout;
    
    VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vps;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.layout = postProcessingLayout_;
    pci.renderPass = renderPass_; // Render to swapchain
    pci.subpass = 0;
    
    // Need a sampler for the textures
    if (!textureSampler_) {
        if (!createTextureSampler()) {
            vkDestroyPipelineLayout(device_, postProcessingLayout_, nullptr);
            vkDestroyDescriptorSetLayout(device_, postProcessingDescriptorLayout_, nullptr);
            vkDestroyShaderModule(device_, vert, nullptr);
            vkDestroyShaderModule(device_, frag, nullptr);
            return false;
        }
    }
    
    bool ok = (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &postProcessingPipeline_) == VK_SUCCESS);
    if (!ok) {
        vkDestroyPipelineLayout(device_, postProcessingLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, postProcessingDescriptorLayout_, nullptr);
    }
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return ok;
}

bool Renderer::createPostProcessingDescriptorSet() {
    // Create fullscreen quad vertex buffer
    const float quadVertices[] = {
        -1.0f, -1.0f,  // Bottom left
         1.0f, -1.0f,  // Bottom right
         1.0f,  1.0f,  // Top right
        -1.0f,  1.0f   // Top left
    };
    
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = sizeof(quadVertices);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &fullscreenQuadBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, fullscreenQuadBuffer_, &memReq);
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &fullscreenQuadBufferMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, fullscreenQuadBuffer_, fullscreenQuadBufferMemory_, 0);
    
    void* data = nullptr;
    vkMapMemory(device_, fullscreenQuadBufferMemory_, 0, bufferInfo.size, 0, &data);
    std::memcpy(data, quadVertices, sizeof(quadVertices));
    vkUnmapMemory(device_, fullscreenQuadBufferMemory_);
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2; // HDR + bloom
    
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &postProcessingDescriptorPool_) != VK_SUCCESS) return false;
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo descAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descAllocInfo.descriptorPool = postProcessingDescriptorPool_;
    descAllocInfo.descriptorSetCount = 1;
    descAllocInfo.pSetLayouts = &postProcessingDescriptorLayout_;
    if (vkAllocateDescriptorSets(device_, &descAllocInfo, &postProcessingDescriptorSet_) != VK_SUCCESS) return false;
    
    // Create UBO for post-processing
    VkBufferCreateInfo uboBufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    uboBufferInfo.size = sizeof(PostProcessingUBO);
    uboBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (vkCreateBuffer(device_, &uboBufferInfo, nullptr, &postProcessingUBO_.buffer) != VK_SUCCESS) return false;
    
    VkMemoryRequirements uboMemReq;
    vkGetBufferMemoryRequirements(device_, postProcessingUBO_.buffer, &uboMemReq);
    VkMemoryAllocateInfo memAlloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    memAlloc.allocationSize = uboMemReq.size;
    memAlloc.memoryTypeIndex = findMemoryType(uboMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &memAlloc, nullptr, &postProcessingUBO_.memory) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, postProcessingUBO_.buffer, postProcessingUBO_.memory, 0);
    vkMapMemory(device_, postProcessingUBO_.memory, 0, uboMemReq.size, 0, &postProcessingUBO_.mapped);
    
    // Update descriptor set
    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = postProcessingUBO_.buffer;
    uboInfo.offset = 0;
    uboInfo.range = sizeof(PostProcessingUBO);
    
    VkDescriptorImageInfo hdrImageInfo{};
    hdrImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrImageInfo.imageView = hdrColorView_;
    hdrImageInfo.sampler = textureSampler_;
    
    VkDescriptorImageInfo bloomImageInfo{};
    bloomImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bloomImageInfo.imageView = bloomViews_[0];
    bloomImageInfo.sampler = textureSampler_;
    
    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = postProcessingDescriptorSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &uboInfo;
    
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = postProcessingDescriptorSet_;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &hdrImageInfo;
    
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = postProcessingDescriptorSet_;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &bloomImageInfo;
    
    vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
    
    return true;
}

void Renderer::renderBloom(VkCommandBuffer cmd) {
    // Simplified bloom - just copy bright areas from HDR to bloom texture
    // For now, this is a placeholder - full bloom would need multiple passes
}

void Renderer::renderPostProcessing(VkCommandBuffer cmd, uint32_t imageIndex) {
    // Render fullscreen quad with post-processing shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postProcessingPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postProcessingLayout_, 0, 1, &postProcessingDescriptorSet_, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fullscreenQuadBuffer_, &offset);
    vkCmdDraw(cmd, 4, 1, 0, 0); // Draw quad (2 triangles)
}

}
