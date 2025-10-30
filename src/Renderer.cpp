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
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPoolAndBuffers()) return false;
    
    // Create HDR render target before pipelines (pipelines need hdrRenderPass_)
    if (!createHDRRenderTarget()) return false;
    
    // Create pipelines (they use hdrRenderPass_)
    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createPipelineWireframe()) return false;  // Wireframe version for debug mode
    if (!createBloomTextures()) return false;
    if (!createPostProcessingPipeline()) return false;
    if (!createPostProcessingDescriptorSet()) return false;
    
    // Initialize city generation
    cityGenerator_ = new CityGenerator();
    CityGenerator* gen = static_cast<CityGenerator*>(cityGenerator_);
    gen->setGridSpacing(8.0f);
    gen->setChunkSize(50.0f);
    
    // Use the chunk system from the start - let updateChunks() generate initial chunks
    // based on the camera's starting position (0, 100, -150)
    updateChunks();
    
    if (!loadTextures()) return false;
    if (!loadNeonTextures()) return false;
    
    // Build geometry from the initial chunks
    if (!createCityGeometry()) return false;
    if (!createNeonGeometry()) return false;
    if (!createGroundGeometry()) return false;
    if (!createShadowVolumeGeometry()) return false;
    if (!createNeonPipeline()) return false;
    if (!createShadowVolumePipeline()) return false;
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
    
    // Initialize debug overlay resources
    if (!createDebugOverlayResources()) {
        printf("Warning: Failed to create debug overlay resources\n");
        // Don't fail initialization, just warn
    }
    
    // Initialize chunk visualization
    if (!createDebugChunkVisualization()) {
        printf("Warning: Failed to create debug chunk visualization\n");
        // Don't fail initialization, just warn
    }
    
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

        // Clean up debug overlay resources
        if (debugTextPipeline_) vkDestroyPipeline(device_, debugTextPipeline_, nullptr);
        if (debugTextPipelineLayout_) vkDestroyPipelineLayout(device_, debugTextPipelineLayout_, nullptr);
        if (debugTextDescriptorPool_) vkDestroyDescriptorPool(device_, debugTextDescriptorPool_, nullptr);
        if (debugTextDescriptorLayout_) vkDestroyDescriptorSetLayout(device_, debugTextDescriptorLayout_, nullptr);
        if (debugTextVertexBuffer_) vkDestroyBuffer(device_, debugTextVertexBuffer_, nullptr);
        if (debugTextVertexMemory_) vkFreeMemory(device_, debugTextVertexMemory_, nullptr);
        if (debugTextIndexBuffer_) vkDestroyBuffer(device_, debugTextIndexBuffer_, nullptr);
        if (debugTextIndexMemory_) vkFreeMemory(device_, debugTextIndexMemory_, nullptr);
        if (debugFontSampler_) vkDestroySampler(device_, debugFontSampler_, nullptr);
        if (debugFontView_) vkDestroyImageView(device_, debugFontView_, nullptr);
        if (debugFontImage_) vkDestroyImage(device_, debugFontImage_, nullptr);
        if (debugFontMemory_) vkFreeMemory(device_, debugFontMemory_, nullptr);
        
        // Clean up debug chunk visualization resources
        if (debugChunkPipeline_) vkDestroyPipeline(device_, debugChunkPipeline_, nullptr);
        if (debugChunkPipelineLayout_) vkDestroyPipelineLayout(device_, debugChunkPipelineLayout_, nullptr);
        if (debugChunkVertexBuffer_) vkDestroyBuffer(device_, debugChunkVertexBuffer_, nullptr);
        if (debugChunkVertexMemory_) vkFreeMemory(device_, debugChunkVertexMemory_, nullptr);

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
        
        // Clean up city pipelines
        if (graphicsPipeline_) vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        if (graphicsPipelineWireframe_) vkDestroyPipeline(device_, graphicsPipelineWireframe_, nullptr);
        if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);

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

    if (debugOverlayVisible_) {
        gatherDebugOverlayStats(deltaSeconds);
    }
    
    // Process movement based on current input
    processMovement(deltaSeconds);
    
    // Update chunks based on camera position
    updateChunks();
    
    // Rebuild geometry if chunks changed
    rebuildGeometryIfNeeded();
    
    // Update debug chunk visualization if debug mode is enabled
    if (debugVisualizationMode_ && debugChunkPipeline_) {
        updateDebugChunkGeometry();
    }
    
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
    ubo.fogDensity = debugVisualizationMode_ ? 0.0f : fogDensity_;  // Disable fog in debug mode
    
    ubo.skyLightDir[0] = skyLightDir_.x;
    ubo.skyLightDir[1] = skyLightDir_.y;
    ubo.skyLightDir[2] = skyLightDir_.z;
    ubo.skyLightIntensity = skyLightIntensity_;
    ubo.texTiling = 1.0f;
    ubo.textureCount = static_cast<float>(std::max(1, numBuildingTextures_));
    
    std::memcpy(uniformBuffers_[0].mapped, &ubo, sizeof(ubo));
    
    // Update post-processing UBO (disable effects in debug visualization mode for clearer wireframe view)
    if (postProcessingUBO_.mapped) {
        PostProcessingUBO ppUBO{};
        if (debugVisualizationMode_) {
            // Debug mode: bypass all effects for clear wireframe visualization
            ppUBO.exposure = 1.0f;  // No tone mapping
            ppUBO.bloomThreshold = 999.0f;  // No bloom
            ppUBO.bloomIntensity = 0.0f;
            ppUBO.fogHeightFalloff = 0.0f;  // No fog
            ppUBO.fogHeightOffset = 0.0f;
            ppUBO.vignetteStrength = 0.0f;  // No vignette
            ppUBO.vignetteRadius = 1.0f;
            ppUBO.grainStrength = 0.0f;  // No grain
            ppUBO.contrast = 1.0f;  // No contrast adjustment
            ppUBO.saturation = 1.0f;  // No saturation adjustment
            ppUBO.colorTemperature = 0.5f;  // Neutral temperature
            ppUBO.lightShaftIntensity = 0.0f;  // No light shafts
            ppUBO.lightShaftDensity = 0.0f;
        } else {
            // Normal mode: use configured effects
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
        }
        
        // Copy view and projection matrices for sun disk projection
        std::memcpy(ppUBO.view, &view[0][0], sizeof(float) * 16);
        std::memcpy(ppUBO.proj, &proj[0][0], sizeof(float) * 16);
        
        // Copy sun world-space direction as vector from scene to sun (invert light-to-scene dir)
        ppUBO.sunWorldDir[0] = -skyLightDir_.x;
        ppUBO.sunWorldDir[1] = -skyLightDir_.y;
        ppUBO.sunWorldDir[2] = -skyLightDir_.z;
        ppUBO._pad = 0.0f;  // Padding
        
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

    if (debugOverlayVisible_) {
        renderDebugOverlay();
    }
}

bool Renderer::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = depthFormat;
    ci.extent = { swapchainExtent_.width, swapchainExtent_.height, 1 };
    ci.mipLevels = 1; ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &bi);
    
    // Render shadow map first
    renderShadowMap(cmd);
    
    // Step 1: Render scene to HDR framebuffer
    // Transition HDR image to COLOR_ATTACHMENT layout if needed (for subsequent frames)
    // On first frame, the render pass handles UNDEFINED -> COLOR_ATTACHMENT automatically
    if (hdrImageInitialized_) {
        // Image is in SHADER_READ_ONLY from previous frame, transition to COLOR_ATTACHMENT
        VkImageMemoryBarrier hdrToAttachmentBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        hdrToAttachmentBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hdrToAttachmentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        hdrToAttachmentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hdrToAttachmentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hdrToAttachmentBarrier.image = hdrColorImage_;
        hdrToAttachmentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        hdrToAttachmentBarrier.subresourceRange.levelCount = 1;
        hdrToAttachmentBarrier.subresourceRange.layerCount = 1;
        hdrToAttachmentBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        hdrToAttachmentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &hdrToAttachmentBarrier);
    }
    hdrImageInitialized_ = true; // Mark as initialized after first use
    
    std::array<VkClearValue,2> clears{}; 
    clears[0].color = { {fogColor_.x, fogColor_.y, fogColor_.z, 1.0f} }; // Fog-colored background
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo hdrRP{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    hdrRP.renderPass = hdrRenderPass_;
    hdrRP.framebuffer = hdrFramebuffer_;
    hdrRP.renderArea.extent = swapchainExtent_;
    hdrRP.clearValueCount = (uint32_t)clears.size(); hdrRP.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmd, &hdrRP, VK_SUBPASS_CONTENTS_INLINE);
    
    // Render ground planes first (same pipeline as buildings)
    VkPipeline cityPipeline = debugVisualizationMode_ ? graphicsPipelineWireframe_ : graphicsPipeline_;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cityPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[0], 0, nullptr);
    
    if (groundIndexCount_ > 0) {
        VkDeviceSize offs = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &groundVertexBuffer_, &offs);
        vkCmdBindIndexBuffer(cmd, groundIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, groundIndexCount_, 1, 0, 0, 0);
    }
    
    // Render city buildings to HDR (wireframe in debug visualization mode)
    VkDeviceSize offs = 0; 
    vkCmdBindVertexBuffers(cmd, 0, 1, &cityVertexBuffer_, &offs);
    vkCmdBindIndexBuffer(cmd, cityIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);  // Changed from UINT16 to UINT32
    vkCmdDrawIndexed(cmd, cityIndexCount_, 1, 0, 0, 0);
    
    // Render shadow volumes (stencil-only rendering, skip in debug mode)
    if (!debugVisualizationMode_ && shadowVolumesEnabled_) {
        renderShadowVolumes(cmd);
    }
    
    // Render neon lights with premultiplied alpha blending to HDR (skip in debug visualization mode)
    if (!debugVisualizationMode_ && neonPipeline_ && neonIndexCount_ > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, neonPipeline_);
        vkCmdBindVertexBuffers(cmd, 0, 1, &neonVertexBuffer_, &offs);
        vkCmdBindIndexBuffer(cmd, neonIndexBuffer_, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[0], 0, nullptr);
        vkCmdDrawIndexed(cmd, neonIndexCount_, 1, 0, 0, 0);
    }
    
    // Render debug chunk boundaries in debug visualization mode
    if (debugVisualizationMode_) {
        renderDebugChunks(cmd);
    }
    
    vkCmdEndRenderPass(cmd);
    // HDR image is now in SHADER_READ_ONLY_OPTIMAL layout (via render pass finalLayout)
    // Add explicit barrier to ensure HDR writes are complete before post-processing reads
    VkImageMemoryBarrier hdrReadBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    hdrReadBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hdrReadBarrier.image = hdrColorImage_;
    hdrReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    hdrReadBarrier.subresourceRange.levelCount = 1;
    hdrReadBarrier.subresourceRange.layerCount = 1;
    hdrReadBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    hdrReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &hdrReadBarrier);
    
    // Step 2: Render post-processing to swapchain
    VkRenderPassBeginInfo swapRP{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    swapRP.renderPass = renderPass_;
    swapRP.framebuffer = framebuffers_[imageIndex];
    swapRP.renderArea.extent = swapchainExtent_;
    swapRP.clearValueCount = 1;
    VkClearValue swapClear{};
    swapClear.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    swapRP.pClearValues = &swapClear;
    vkCmdBeginRenderPass(cmd, &swapRP, VK_SUBPASS_CONTENTS_INLINE);
    
    // Render post-processing fullscreen quad
    renderPostProcessing(cmd, imageIndex);
    
    // Render debug overlay if enabled
    if (debugOverlayVisible_) {
        renderDebugOverlayGraphical(cmd);
    }
    
    vkCmdEndRenderPass(cmd);
    
    vkEndCommandBuffer(cmd);
}

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

void Renderer::toggleShaderReload() {
    shaderReloadEnabled_ = !shaderReloadEnabled_;
    printf("Shader hot reloading %s\n", shaderReloadEnabled_ ? "enabled" : "disabled");
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
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Start from UNDEFINED on first frame, render pass will transition
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // End in SHADER_READ_ONLY for post-processing
    
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();  // Use actual depth format (may have stencil)
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Store depth for potential later use
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear stencil for shadow volumes
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;  // Store stencil results
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    std::array<VkAttachmentDescription, 2> atts{ colorAttachment, depthAttachment };
    
    // Subpass dependency to handle layout transition: render pass automatically transitions UNDEFINED->COLOR_ATTACHMENT->SHADER_READ_ONLY
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    // Add second dependency for depth buffer access in post-processing
    VkSubpassDependency depthDependency{};
    depthDependency.srcSubpass = 0;
    depthDependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    depthDependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    depthDependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    std::array<VkSubpassDependency, 2> dependencies{ dependency, depthDependency };
    
    VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = atts.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies = dependencies.data();
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
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    
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
    
    VkDescriptorSetLayoutBinding depthTexBinding{};
    depthTexBinding.binding = 3;
    depthTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthTexBinding.descriptorCount = 1;
    depthTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding bindings[4] = { uboBinding, hdrTexBinding, bloomTexBinding, depthTexBinding };
    VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 4;
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
    // For TRIANGLE_STRIP, order matters: BL, BR, TL, TR creates two triangles covering the screen
    const float quadVertices[] = {
        -1.0f, -1.0f,  // Bottom left
         1.0f, -1.0f,  // Bottom right
        -1.0f,  1.0f,  // Top left
         1.0f,  1.0f   // Top right
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
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 3; // HDR + bloom + depth
    
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
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
    
    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthImageInfo.imageView = depthImageView_;
    depthImageInfo.sampler = textureSampler_;
    
    VkWriteDescriptorSet writes[4]{};
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
    
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = postProcessingDescriptorSet_;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &depthImageInfo;
    
    vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
    
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

void Renderer::updateChunks() {
    CityGenerator* gen = static_cast<CityGenerator*>(cityGenerator_);
    if (!gen) return;
    
    float chunkSize = gen->getChunkSize();
    
    // Calculate which chunk the camera is in
    // Account for city center offset (cityWidth_/2, cityDepth_/2)
    float camX = cameraPos_.x;
    float camZ = cameraPos_.z;
    
    // Convert world position to chunk coordinates
    // Using floor division to get chunk indices
    int currentChunkX = static_cast<int>(std::floor(camX / chunkSize));
    int currentChunkZ = static_cast<int>(std::floor(camZ / chunkSize));
    
    // Debug: Print camera chunk changes
    static int lastChunkX = 999999;
    static int lastChunkZ = 999999;
    if (currentChunkX != lastChunkX || currentChunkZ != lastChunkZ) {
        printf("Camera chunk: (%d, %d)\n", currentChunkX, currentChunkZ);
        lastChunkX = currentChunkX;
        lastChunkZ = currentChunkZ;
    }
    
    // Determine chunks to load and unload
    std::set<std::pair<int, int>> chunksToLoad;
    std::set<std::pair<int, int>> chunksToUnload;
    
    // Calculate radius of chunks to keep loaded (in chunk units)
    int loadRadius = static_cast<int>(std::ceil(chunkLoadDistance_ / chunkSize));
    int unloadRadius = static_cast<int>(std::ceil(chunkUnloadDistance_ / chunkSize));
    
    // Find chunks within load distance
    for (int x = currentChunkX - loadRadius; x <= currentChunkX + loadRadius; ++x) {
        for (int z = currentChunkZ - loadRadius; z <= currentChunkZ + loadRadius; ++z) {
            auto chunkKey = std::make_pair(x, z);
            
            // Check if chunk is within load distance
            float chunkCenterX = (x + 0.5f) * chunkSize;
            float chunkCenterZ = (z + 0.5f) * chunkSize;
            float dx = camX - chunkCenterX;
            float dz = camZ - chunkCenterZ;
            float distSq = dx * dx + dz * dz;
            
            if (distSq <= chunkLoadDistance_ * chunkLoadDistance_) {
                if (activeChunks_.find(chunkKey) == activeChunks_.end()) {
                    chunksToLoad.insert(chunkKey);
                }
            }
        }
    }
    
    // Find chunks to unload (outside unload distance) - DON'T ERASE YET
    for (const auto& chunkKey : activeChunks_) {
        int x = chunkKey.first;
        int z = chunkKey.second;
        float chunkCenterX = (x + 0.5f) * chunkSize;
        float chunkCenterZ = (z + 0.5f) * chunkSize;
        float dx = camX - chunkCenterX;
        float dz = camZ - chunkCenterZ;
        float distSq = dx * dx + dz * dz;
        
        if (distSq > chunkUnloadDistance_ * chunkUnloadDistance_) {
            chunksToUnload.insert(chunkKey);
        }
    }
    
    // SIMPLIFIED APPROACH: Never do full regeneration, just keep accumulating chunks
    // and rebuild geometry as needed. This prevents thrashing and building loss.
    // We'll only clear chunks when memory becomes an actual issue (not implemented yet).
    static size_t lastUnloadCount = 0;
    if (!chunksToUnload.empty() && chunksToUnload.size() != lastUnloadCount) {
        printf("⚠️  Would unload %zu chunks, but keeping them to prevent thrashing (total active: %zu)\n", 
               chunksToUnload.size(), activeChunks_.size());
        lastUnloadCount = chunksToUnload.size();
        // Don't actually unload anything - just keep growing the city
        // Future optimization: implement proper per-chunk removal without full regeneration
    }
    
    // Load new chunks (simple case - no index corruption when only adding)
    if (!chunksToLoad.empty()) {
        printf("📍 Loading %zu new chunks (current total: %zu buildings, %zu active chunks)\n", 
               chunksToLoad.size(), gen->getBuildings().size(), activeChunks_.size());
    }
    for (const auto& chunkKey : chunksToLoad) {
        printf("+ Chunk (%d, %d)\n", chunkKey.first, chunkKey.second);
        gen->generateChunk(chunkKey.first, chunkKey.second, 42);
        activeChunks_.insert(chunkKey);
        geometryNeedsRebuild_ = true;
    }
    if (!chunksToLoad.empty()) {
        printf("✅ After loading: %zu buildings total\n", gen->getBuildings().size());
    }
}

void Renderer::rebuildGeometryIfNeeded() {
    if (!geometryNeedsRebuild_) return;
    
    geometryNeedsRebuild_ = false;
    
    CityGenerator* gen = static_cast<CityGenerator*>(cityGenerator_);
    printf("🔨 Rebuild: %zu buildings\n", gen->getBuildings().size());
    
    // Wait for GPU to finish before rebuilding
    vkDeviceWaitIdle(device_);
    
    // Clean up old geometry buffers
    if (cityVertexBuffer_) {
        vkDestroyBuffer(device_, cityVertexBuffer_, nullptr);
        cityVertexBuffer_ = VK_NULL_HANDLE;
    }
    if (cityVertexBufferMemory_) {
        vkFreeMemory(device_, cityVertexBufferMemory_, nullptr);
        cityVertexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (cityIndexBuffer_) {
        vkDestroyBuffer(device_, cityIndexBuffer_, nullptr);
        cityIndexBuffer_ = VK_NULL_HANDLE;
    }
    if (cityIndexBufferMemory_) {
        vkFreeMemory(device_, cityIndexBufferMemory_, nullptr);
        cityIndexBufferMemory_ = VK_NULL_HANDLE;
    }
    
    if (neonVertexBuffer_) {
        vkDestroyBuffer(device_, neonVertexBuffer_, nullptr);
        neonVertexBuffer_ = VK_NULL_HANDLE;
    }
    if (neonVertexBufferMemory_) {
        vkFreeMemory(device_, neonVertexBufferMemory_, nullptr);
        neonVertexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (neonIndexBuffer_) {
        vkDestroyBuffer(device_, neonIndexBuffer_, nullptr);
        neonIndexBuffer_ = VK_NULL_HANDLE;
    }
    if (neonIndexBufferMemory_) {
        vkFreeMemory(device_, neonIndexBufferMemory_, nullptr);
        neonIndexBufferMemory_ = VK_NULL_HANDLE;
    }
    
    if (groundVertexBuffer_) {
        vkDestroyBuffer(device_, groundVertexBuffer_, nullptr);
        groundVertexBuffer_ = VK_NULL_HANDLE;
    }
    if (groundVertexBufferMemory_) {
        vkFreeMemory(device_, groundVertexBufferMemory_, nullptr);
        groundVertexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (groundIndexBuffer_) {
        vkDestroyBuffer(device_, groundIndexBuffer_, nullptr);
        groundIndexBuffer_ = VK_NULL_HANDLE;
    }
    if (groundIndexBufferMemory_) {
        vkFreeMemory(device_, groundIndexBufferMemory_, nullptr);
        groundIndexBufferMemory_ = VK_NULL_HANDLE;
    }
    
    // Rebuild geometry
    if (!createCityGeometry()) {
        printf("Failed to rebuild city geometry\n");
    }
    if (!createNeonGeometry()) {
        printf("Failed to rebuild neon geometry\n");
    }
    if (!createGroundGeometry()) {
        printf("Failed to rebuild ground geometry\n");
    }
    if (!createShadowVolumeGeometry()) {
        printf("Failed to rebuild shadow volume geometry\n");
    }
}

void Renderer::gatherDebugOverlayStats(float deltaSeconds) {
    // Update frame times circular buffer and compute a smoothed FPS
    frameTimes_[frameTimeIndex_++ % 128] = deltaSeconds;
    if (frameTimeIndex_ > 128) frameTimeIndex_ = 0;
    float avg = 0.0f;
    for (float t : frameTimes_) avg += t;
    avg /= 128.0f;
    debug_fpsSmoothed_ = avg > 0.00001f ? 1.0f / avg : 0.0f;
}

void Renderer::renderDebugOverlay() {
    // This is now just a stub - actual rendering happens in renderDebugOverlayGraphical
    // which is called from the command buffer recording
}

void Renderer::renderDebugOverlayGraphical(VkCommandBuffer cmd) {
    if (!debugTextPipeline_) return; // Not initialized yet
    
    // Build overlay text
    char overlayText[2048];
    
    // Color code FPS (using special control characters)
    const char* fpsColor = debug_fpsSmoothed_ >= 60.0f ? "\x1F" : // Green
                          (debug_fpsSmoothed_ >= 30.0f ? "\x1E" : "\x1D"); // Yellow : Red
    
    snprintf(overlayText, sizeof(overlayText),
        "PROCEDURAL CITY - DEBUG\n"
        "=======================\n"
        "%sFPS: %.1f\x1C\n"  // Color coded FPS, then reset to white
        "Polygons: %u\n"
        "Chunks: %zu active\n"
        "Buildings: %zu\n"
        "Neon Lights: %zu\n"
        "Camera: (%.1f, %.1f, %.1f)\n"
        "Chunk: (%d, %d)\n",
        fpsColor,
        debug_fpsSmoothed_,
        cityIndexCount_,
        activeChunks_.size(),
        static_cast<CityGenerator*>(cityGenerator_)->getBuildings().size(),
        static_cast<CityGenerator*>(cityGenerator_)->getNeonLights().size(),
        cameraPos_.x, cameraPos_.y, cameraPos_.z,
        int(std::floor(cameraPos_.x / static_cast<CityGenerator*>(cityGenerator_)->getChunkSize())),
        int(std::floor(cameraPos_.z / static_cast<CityGenerator*>(cityGenerator_)->getChunkSize()))
    );
    
    // Update geometry for this frame's text
    updateDebugTextGeometry(overlayText, -0.95f, 0.95f, 0.04f);
    
    if (debugTextIndexCount_ == 0) return;
    
    // Render text
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugTextPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugTextPipelineLayout_, 
                           0, 1, &debugTextDescriptorSet_, 0, nullptr);
    
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &debugTextVertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, debugTextIndexBuffer_, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, debugTextIndexCount_, 1, 0, 0, 0);
}

void Renderer::updateDebugTextGeometry(const char* text, float startX, float startY, float scale) {
    std::vector<float> vertices;
    std::vector<uint16_t> indices;
    
    float x = startX;
    float y = startY;
    float charWidth = scale;
    float charHeight = scale * 1.5f; // Slightly taller for readability
    
    uint16_t vertexOffset = 0;
    glm::vec3 currentColor(1.0f, 1.0f, 1.0f); // Default white
    
    for (const char* c = text; *c != '\0'; ++c) {
        if (*c == '\n') {
            x = startX;
            y -= charHeight * 1.2f; // Line spacing
            continue;
        }
        
        // Handle color codes (special characters)
        if (*c == '\x1C') { // Reset to white
            currentColor = glm::vec3(1.0f, 1.0f, 1.0f);
            continue;
        } else if (*c == '\x1D') { // Red
            currentColor = glm::vec3(1.0f, 0.3f, 0.3f);
            continue;
        } else if (*c == '\x1E') { // Yellow
            currentColor = glm::vec3(1.0f, 1.0f, 0.3f);
            continue;
        } else if (*c == '\x1F') { // Green
            currentColor = glm::vec3(0.3f, 1.0f, 0.3f);
            continue;
        }
        
        // Calculate texture coordinates for this character in font atlas
        // Assuming 16x16 character grid (256 ASCII characters)
        int charCode = (unsigned char)*c;
        float texX = (charCode % 16) / 16.0f;
        float texY = (charCode / 16) / 16.0f;
        float texW = 1.0f / 16.0f;
        float texH = 1.0f / 16.0f;
        
        // Create quad for this character
        // Format: pos(2) + texCoord(2) + color(3) = 7 floats per vertex
        // Note: y decreases downward, charHeight is positive
        // Flip V coordinate: bottom of texture (texY + texH) goes to top of quad (y)
        // Flip U coordinate horizontally to fix mirrored text
        vertices.insert(vertices.end(), {
            x,             y,              texX + texW,  texY + texH,  currentColor.r, currentColor.g, currentColor.b,
            x + charWidth, y,              texX,         texY + texH,  currentColor.r, currentColor.g, currentColor.b,
            x + charWidth, y - charHeight, texX,         texY,         currentColor.r, currentColor.g, currentColor.b,
            x,             y - charHeight, texX + texW,  texY,         currentColor.r, currentColor.g, currentColor.b,
        });
        
        indices.insert(indices.end(), {
            static_cast<uint16_t>(vertexOffset + 0), static_cast<uint16_t>(vertexOffset + 1), static_cast<uint16_t>(vertexOffset + 2),
            static_cast<uint16_t>(vertexOffset + 2), static_cast<uint16_t>(vertexOffset + 3), static_cast<uint16_t>(vertexOffset + 0),
        });
        
        vertexOffset += 4;
        x += charWidth * 0.6f; // Character spacing (monospace)
    }
    
    debugTextIndexCount_ = static_cast<uint32_t>(indices.size());
    
    if (debugTextIndexCount_ == 0) return;
    
    // Update vertex buffer
    if (debugTextVertexBuffer_) {
        void* data;
        vkMapMemory(device_, debugTextVertexMemory_, 0, vertices.size() * sizeof(float), 0, &data);
        memcpy(data, vertices.data(), vertices.size() * sizeof(float));
        vkUnmapMemory(device_, debugTextVertexMemory_);
    }
    
    // Update index buffer  
    if (debugTextIndexBuffer_) {
        void* data;
        vkMapMemory(device_, debugTextIndexMemory_, 0, indices.size() * sizeof(uint16_t), 0, &data);
        memcpy(data, indices.data(), indices.size() * sizeof(uint16_t));
        vkUnmapMemory(device_, debugTextIndexMemory_);
    }
}

}
