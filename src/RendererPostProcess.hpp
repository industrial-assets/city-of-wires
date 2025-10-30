#pragma once
#include "Renderer.hpp"
#include <vulkan/vulkan.h>
#include <vector>

namespace pcengine {

struct BufferWithMemory {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

class RendererPostProcess {
public:
    RendererPostProcess(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t typeFilter, VkMemoryPropertyFlags properties));
    ~RendererPostProcess();

    bool createHDRRenderTarget(VkExtent2D swapchainExtent, VkImageView depthImageView, VkRenderPass& hdrRenderPass, VkFramebuffer& hdrFramebuffer);
    bool createBloomTextures(VkExtent2D swapchainExtent, VkRenderPass& bloomRenderPass);
    bool createPostProcessingPipeline(VkExtent2D swapchainExtent, VkRenderPass renderPass, VkSampler textureSampler, VkPipelineLayout& postProcessingLayout, VkDescriptorSetLayout& postProcessingDescriptorLayout, VkPipeline& postProcessingPipeline);
    bool createPostProcessingDescriptorSet(VkDescriptorSetLayout descriptorLayout, VkSampler textureSampler);
    
    void renderBloom(VkCommandBuffer cmd);
    void renderPostProcessing(VkCommandBuffer cmd);
    
    void updatePostProcessingUBO(const PostProcessingUBO& ubo);
    
    // Accessors for resources
    VkImage getHDRColorImage() const { return hdrColorImage_; }
    VkImageView getHDRColorView() const { return hdrColorView_; }
    BufferWithMemory getPostProcessingUBO() const { return postProcessingUBO_; }
    VkPipeline getPostProcessingPipeline() const { return postProcessingPipeline_; }
    VkPipelineLayout getPostProcessingLayout() const { return postProcessingLayout_; }
    VkDescriptorSet getPostProcessingDescriptorSet() const { return postProcessingDescriptorSet_; }
    VkBuffer getFullscreenQuadBuffer() const { return fullscreenQuadBuffer_; }
    
    // Bloom resources
    std::vector<VkFramebuffer>& getBloomFramebuffers() { return bloomFramebuffers_; }
    VkFramebuffer getBloomFramebuffer(size_t index) const { return index < bloomFramebuffers_.size() ? bloomFramebuffers_[index] : VK_NULL_HANDLE; }
    VkImageView getBloomView(size_t index) const { return index < bloomViews_.size() ? bloomViews_[index] : VK_NULL_HANDLE; }

private:
    VkDevice device_;
    VkPhysicalDevice physicalDevice_;
    uint32_t (*findMemoryType_)(uint32_t, VkMemoryPropertyFlags);
    
    // HDR render target
    VkImage hdrColorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory hdrColorMemory_ = VK_NULL_HANDLE;
    VkImageView hdrColorView_ = VK_NULL_HANDLE;
    VkRenderPass hdrRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer hdrFramebuffer_ = VK_NULL_HANDLE;
    
    // Bloom render pass
    VkRenderPass bloomRenderPass_ = VK_NULL_HANDLE;
    
    // Bloom textures
    std::vector<VkImage> bloomImages_;
    std::vector<VkDeviceMemory> bloomMemories_;
    std::vector<VkImageView> bloomViews_;
    std::vector<VkFramebuffer> bloomFramebuffers_;
    
    // Post-processing pipeline
    VkPipeline postProcessingPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout postProcessingLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout postProcessingDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool postProcessingDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet postProcessingDescriptorSet_ = VK_NULL_HANDLE;
    VkBuffer fullscreenQuadBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory fullscreenQuadBufferMemory_ = VK_NULL_HANDLE;
    
    // Post-processing uniform buffer
    BufferWithMemory postProcessingUBO_;
    
    static std::vector<char> readFile(const std::string& path);
};

} // namespace pcengine

