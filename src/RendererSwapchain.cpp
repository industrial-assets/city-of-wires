#include "Renderer.hpp"
#include <vulkan/vulkan.h>
#include <vector>

namespace pcengine {

bool Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{}; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);
    uint32_t fmtCount=0; vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount); vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCount, fmts.data());
    VkSurfaceFormatKHR fmt = fmts[0];
    swapchainFormat_ = fmt.format;
    if (caps.currentExtent.width != UINT32_MAX) swapchainExtent_ = caps.currentExtent; else { swapchainExtent_.width = 800; swapchainExtent_.height = 600; }
    uint32_t imageCount = caps.minImageCount + 1; if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
    VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    ci.surface = surface_;
    ci.minImageCount = imageCount;
    ci.imageFormat = fmt.format;
    ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent = swapchainExtent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) return false;
    uint32_t count = 0; vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    swapchainImages_.resize(count); vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data());
    return true;
}

bool Renderer::createImageViews() {
    swapchainImageViews_.resize(swapchainImages_.size());
    for (size_t i = 0; i < swapchainImages_.size(); ++i) {
        VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ci.image = swapchainImages_[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = swapchainFormat_;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &ci, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

void Renderer::cleanupSwapchain() {
    destroyVolumetricResources();
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr); framebuffers_.clear();
    if (depthImageView_) vkDestroyImageView(device_, depthImageView_, nullptr); depthImageView_ = VK_NULL_HANDLE;
    if (depthImage_) vkDestroyImage(device_, depthImage_, nullptr); depthImage_ = VK_NULL_HANDLE;
    if (depthImageMemory_) vkFreeMemory(device_, depthImageMemory_, nullptr); depthImageMemory_ = VK_NULL_HANDLE;
    for (auto iv : swapchainImageViews_) vkDestroyImageView(device_, iv, nullptr); swapchainImageViews_.clear();
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr); renderPass_ = VK_NULL_HANDLE;
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE;
}

void Renderer::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    
    // Cleanup debug visualization pipelines (they have viewport tied to old extent)
    if (debugTextPipeline_) {
        vkDestroyPipeline(device_, debugTextPipeline_, nullptr);
        debugTextPipeline_ = VK_NULL_HANDLE;
    }
    if (debugChunkPipeline_) {
        vkDestroyPipeline(device_, debugChunkPipeline_, nullptr);
        debugChunkPipeline_ = VK_NULL_HANDLE;
    }
    
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createDepthResources();
    createFramebuffers();

    if (!createVolumetricResources()) {
        printf("Warning: Failed to recreate volumetric resources after swapchain resize\n");
    }
    hasPrevViewProj_ = false;
    prevView_ = glm::mat4(1.0f);
    prevProj_ = glm::mat4(1.0f);
    prevViewProj_ = glm::mat4(1.0f);
    prevCameraPos_ = cameraPos_;
    frameCounter_ = 0;
    updatePostProcessingDescriptors();
    
    // Recreate debug visualization pipelines with new extent
    // Note: These functions are idempotent - they'll skip creating buffers/layouts if they exist
    if (debugTextPipelineLayout_ != VK_NULL_HANDLE) {
        createDebugTextPipeline();
    }
    if (debugChunkPipelineLayout_ != VK_NULL_HANDLE) {
        createDebugChunkVisualization();
    }
    
    // Command buffers sized to framebuffers already
}

}

