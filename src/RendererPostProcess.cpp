#include "RendererPostProcess.hpp"
#include "Renderer.hpp"
#include <cstdio>
#include <cstring>
#include <array>

namespace pcengine {

RendererPostProcess::RendererPostProcess(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t (*findMemoryType)(uint32_t, VkMemoryPropertyFlags))
    : device_(device), physicalDevice_(physicalDevice), findMemoryType_(findMemoryType) {
}

RendererPostProcess::~RendererPostProcess() {
    if (fullscreenQuadBuffer_) vkDestroyBuffer(device_, fullscreenQuadBuffer_, nullptr);
    if (fullscreenQuadBufferMemory_) vkFreeMemory(device_, fullscreenQuadBufferMemory_, nullptr);
    if (postProcessingUBO_.buffer) vkDestroyBuffer(device_, postProcessingUBO_.buffer, nullptr);
    if (postProcessingUBO_.memory) vkFreeMemory(device_, postProcessingUBO_.memory, nullptr);
    if (postProcessingDescriptorPool_) vkDestroyDescriptorPool(device_, postProcessingDescriptorPool_, nullptr);
    if (postProcessingPipeline_) vkDestroyPipeline(device_, postProcessingPipeline_, nullptr);
    if (postProcessingLayout_) vkDestroyPipelineLayout(device_, postProcessingLayout_, nullptr);
    if (postProcessingDescriptorLayout_) vkDestroyDescriptorSetLayout(device_, postProcessingDescriptorLayout_, nullptr);
    
    for (size_t i = 0; i < bloomFramebuffers_.size(); ++i) {
        if (bloomFramebuffers_[i]) vkDestroyFramebuffer(device_, bloomFramebuffers_[i], nullptr);
    }
    for (size_t i = 0; i < bloomViews_.size(); ++i) {
        if (bloomViews_[i]) vkDestroyImageView(device_, bloomViews_[i], nullptr);
    }
    for (size_t i = 0; i < bloomImages_.size(); ++i) {
        if (bloomImages_[i]) vkDestroyImage(device_, bloomImages_[i], nullptr);
        if (i < bloomMemories_.size() && bloomMemories_[i]) vkFreeMemory(device_, bloomMemories_[i], nullptr);
    }
    if (bloomRenderPass_) vkDestroyRenderPass(device_, bloomRenderPass_, nullptr);
    
    if (hdrFramebuffer_) vkDestroyFramebuffer(device_, hdrFramebuffer_, nullptr);
    if (hdrRenderPass_) vkDestroyRenderPass(device_, hdrRenderPass_, nullptr);
    if (hdrColorView_) vkDestroyImageView(device_, hdrColorView_, nullptr);
    if (hdrColorImage_) vkDestroyImage(device_, hdrColorImage_, nullptr);
    if (hdrColorMemory_) vkFreeMemory(device_, hdrColorMemory_, nullptr);
}

bool RendererPostProcess::createHDRRenderTarget(VkExtent2D swapchainExtent, VkImageView depthImageView, VkFormat depthFormat, VkRenderPass& hdrRenderPass, VkFramebuffer& hdrFramebuffer) {
    // Create HDR color image (RGBA16F for HDR)
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent.width;
    imageInfo.extent.height = swapchainExtent.height;
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
    allocInfo.memoryTypeIndex = findMemoryType_(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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
    depthAttachment.format = depthFormat;  // Use actual depth format (may have stencil)
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Store depth for potential later use
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear stencil for shadow volumes
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;  // Store stencil results
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    std::array<VkAttachmentDescription, 2> atts{ colorAttachment, depthAttachment };
    
    // Subpass dependency to handle layout transition
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = atts.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;
    if (vkCreateRenderPass(device_, &rpInfo, nullptr, &hdrRenderPass_) != VK_SUCCESS) return false;
    
    // Create framebuffer (reuse depth buffer from main render)
    std::array<VkImageView, 2> attachments{ hdrColorView_, depthImageView };
    VkFramebufferCreateInfo fbInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbInfo.renderPass = hdrRenderPass_;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = swapchainExtent.width;
    fbInfo.height = swapchainExtent.height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &hdrFramebuffer_) != VK_SUCCESS) return false;
    
    hdrRenderPass = hdrRenderPass_;
    hdrFramebuffer = hdrFramebuffer_;
    return true;
}

bool RendererPostProcess::createBloomTextures(VkExtent2D swapchainExtent, VkRenderPass& bloomRenderPass) {
    // Create a single bloom texture for simplicity
    bloomImages_.resize(1);
    bloomMemories_.resize(1);
    bloomViews_.resize(1);
    bloomFramebuffers_.resize(1);
    
    // Create bloom image (half resolution)
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapchainExtent.width / 2;
    imageInfo.extent.height = swapchainExtent.height / 2;
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
    allocInfo.memoryTypeIndex = findMemoryType_(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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
    fbInfo.width = swapchainExtent.width / 2;
    fbInfo.height = swapchainExtent.height / 2;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &bloomFramebuffers_[0]) != VK_SUCCESS) return false;
    
    bloomRenderPass = bloomRenderPass_;
    return true;
}

std::vector<char> RendererPostProcess::readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<char> data((size_t)len);
    fread(data.data(), 1, data.size(), f); fclose(f);
    return data;
}

bool RendererPostProcess::createPostProcessingPipeline(VkExtent2D swapchainExtent, VkRenderPass renderPass, VkSampler textureSampler, VkPipelineLayout& postProcessingLayout, VkDescriptorSetLayout& postProcessingDescriptorLayout, VkPipeline& postProcessingPipeline) {
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
    vp.width = (float)swapchainExtent.width;
    vp.height = (float)swapchainExtent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    VkRect2D scissor{}; scissor.extent = swapchainExtent;
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
    
    VkDescriptorSetLayoutBinding scatteringTexBinding{};
    scatteringTexBinding.binding = 4;
    scatteringTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    scatteringTexBinding.descriptorCount = 1;
    scatteringTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding transmittanceTexBinding{};
    transmittanceTexBinding.binding = 5;
    transmittanceTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    transmittanceTexBinding.descriptorCount = 1;
    transmittanceTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding anamorphicBloomTexBinding{};
    anamorphicBloomTexBinding.binding = 6;
    anamorphicBloomTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    anamorphicBloomTexBinding.descriptorCount = 1;
    anamorphicBloomTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding bindings[7] = { uboBinding, hdrTexBinding, bloomTexBinding, depthTexBinding, scatteringTexBinding, transmittanceTexBinding, anamorphicBloomTexBinding };
    VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 7;
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
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &postLayout) != VK_SUCCESS) return false;
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
    pci.renderPass = renderPass;
    pci.subpass = 0;
    
    bool ok = (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &postProcessingPipeline_) == VK_SUCCESS);
    if (!ok) {
        vkDestroyPipelineLayout(device_, postProcessingLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, postProcessingDescriptorLayout_, nullptr);
    }
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    
    postProcessingLayout = postProcessingLayout_;
    postProcessingDescriptorLayout = postProcessingDescriptorLayout_;
    postProcessingPipeline = postProcessingPipeline_;
    return ok;
}

bool RendererPostProcess::createPostProcessingDescriptorSet(VkDescriptorSetLayout descriptorLayout, VkSampler textureSampler) {
    // Create fullscreen quad vertex buffer
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
    allocInfo.memoryTypeIndex = findMemoryType_(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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
    poolSizes[1].descriptorCount = 6; // HDR, bloom, depth, scattering, transmittance, anamorphic bloom
    
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &postProcessingDescriptorPool_) != VK_SUCCESS) return false;
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo descAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    descAllocInfo.descriptorPool = postProcessingDescriptorPool_;
    descAllocInfo.descriptorSetCount = 1;
    descAllocInfo.pSetLayouts = &descriptorLayout;
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
    memAlloc.memoryTypeIndex = findMemoryType_(uboMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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
    hdrImageInfo.sampler = textureSampler;
    
    VkDescriptorImageInfo bloomImageInfo{};
    bloomImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bloomImageInfo.imageView = bloomViews_[0];
    bloomImageInfo.sampler = textureSampler;
    
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

void RendererPostProcess::renderBloom(VkCommandBuffer cmd) {
    // Simplified bloom - just copy bright areas from HDR to bloom texture
    // For now, this is a placeholder - full bloom would need multiple passes
}

void RendererPostProcess::renderPostProcessing(VkCommandBuffer cmd) {
    // Render fullscreen quad with post-processing shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postProcessingPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postProcessingLayout_, 0, 1, &postProcessingDescriptorSet_, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fullscreenQuadBuffer_, &offset);
    vkCmdDraw(cmd, 4, 1, 0, 0); // Draw quad (2 triangles)
}

void RendererPostProcess::updatePostProcessingUBO(const PostProcessingUBO& ubo) {
    if (postProcessingUBO_.mapped) {
        std::memcpy(postProcessingUBO_.mapped, &ubo, sizeof(PostProcessingUBO));
    }
}

} // namespace pcengine

