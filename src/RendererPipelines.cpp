#include "Renderer.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <cstdio>

namespace pcengine {

static std::vector<char> readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<char> data((size_t)len);
    fread(data.data(), 1, data.size(), f); fclose(f);
    return data;
}

bool Renderer::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = swapchainFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    std::array<VkAttachmentDescription, 2> atts{ color, depth };
    VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    ci.attachmentCount = (uint32_t)atts.size();
    ci.pAttachments = atts.data();
    ci.subpassCount = 1; ci.pSubpasses = &sub;
    return vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) == VK_SUCCESS;
}

bool Renderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding ubo{};
    ubo.binding = 0; ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ubo.descriptorCount = 1;
    ubo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;  // Used in both stages
    
    VkDescriptorSetLayoutBinding texArr{};
    texArr.binding = 1; texArr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; texArr.descriptorCount = kMaxBuildingTextures;
    texArr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Neon texture array at binding 2 (single sampler)
    VkDescriptorSetLayoutBinding neonArr{};
    neonArr.binding = 2; neonArr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; neonArr.descriptorCount = 1; neonArr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Shadow map at binding 3
    VkDescriptorSetLayoutBinding shadowMap{};
    shadowMap.binding = 3; shadowMap.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; shadowMap.descriptorCount = 1; shadowMap.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding bindings[] = { ubo, texArr, neonArr, shadowMap };
    VkDescriptorSetLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    ci.bindingCount = 4; ci.pBindings = bindings;
    return vkCreateDescriptorSetLayout(device_, &ci, nullptr, &descriptorSetLayout_) == VK_SUCCESS;
}

bool Renderer::createPipeline() {
    std::string base = std::string(PC_ENGINE_SHADER_DIR);
    auto vertCode = readFile(base + "/city.vert.spv");
    auto fragCode = readFile(base + "/city.frag.spv");
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

    // Vertex format: position (vec3), color (vec3), uv (vec2), texIndex (float), normal (vec3) = 12 floats
    VkVertexInputBindingDescription binding{}; binding.binding = 0; binding.stride = sizeof(float)*12; binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = sizeof(float)*3;
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = sizeof(float)*6;
    attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32_SFLOAT; attrs[3].offset = sizeof(float)*8;
    attrs[4].location = 4; attrs[4].binding = 0; attrs[4].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[4].offset = sizeof(float)*9;
    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 5; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{ 0, 0, (float)swapchainExtent_.width, (float)swapchainExtent_.height, 0, 1 };
    VkRect2D scissor{ {0,0}, swapchainExtent_ };
    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1; vp.pViewports = &viewport; vp.scissorCount = 1; vp.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_BACK_BIT; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cbAtt{}; cbAtt.colorWriteMask = 0xF; cbAtt.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cbAtt;

    VkDescriptorSetLayout setLayouts[] = { descriptorSetLayout_ };
    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1; plci.pSetLayouts = setLayouts;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_) != VK_SUCCESS) return false;

    VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.layout = pipelineLayout_;
    pci.renderPass = hdrRenderPass_; // Use HDR render pass for scene rendering
    pci.subpass = 0;
    bool ok = (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &graphicsPipeline_) == VK_SUCCESS);
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return ok;
}

bool Renderer::createNeonPipeline() {
    std::string base = std::string(PC_ENGINE_SHADER_DIR);
    auto vertCode = readFile(base + "/neon.vert.spv");
    auto fragCode = readFile(base + "/neon.frag.spv");
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

    // Neon vertex format: pos(3) color(3) intensity(1) uv(2) texIndex(1) = 10 floats
    VkVertexInputBindingDescription binding{}; binding.binding = 0; binding.stride = sizeof(float)*10; binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = sizeof(float)*3;
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32_SFLOAT; attrs[2].offset = sizeof(float)*6;
    attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32G32_SFLOAT; attrs[3].offset = sizeof(float)*7;
    attrs[4].location = 4; attrs[4].binding = 0; attrs[4].format = VK_FORMAT_R32_SFLOAT; attrs[4].offset = sizeof(float)*9;
    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 5; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{ 0, 0, (float)swapchainExtent_.width, (float)swapchainExtent_.height, 0, 1 };
    VkRect2D scissor{ {0,0}, swapchainExtent_ };
    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1; vp.pViewports = &viewport; vp.scissorCount = 1; vp.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = VK_COMPARE_OP_LESS;

    // Premultiplied alpha blending: src*1 + dst*(1-srcAlpha)
    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = 0xF;
    cbAtt.blendEnable = VK_TRUE;
    cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.colorBlendOp = VK_BLEND_OP_ADD;
    cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cbAtt;

    VkDescriptorSetLayout setLayouts[] = { descriptorSetLayout_ };
    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1; plci.pSetLayouts = setLayouts;
    VkPipelineLayout neonLayout;
    if (vkCreatePipelineLayout(device_, &plci, nullptr, &neonLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.layout = neonLayout;
    pci.renderPass = hdrRenderPass_; // Use HDR render pass for scene rendering
    pci.subpass = 0;
    bool ok = (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &neonPipeline_) == VK_SUCCESS);
    vkDestroyPipelineLayout(device_, neonLayout, nullptr);
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return ok;
}

bool Renderer::reloadShaders() {
    // Wait for device to be idle before recreating pipeline
    vkDeviceWaitIdle(device_);
    
    // Destroy old pipeline
    if (graphicsPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        graphicsPipeline_ = VK_NULL_HANDLE;
    }
    
    // Recreate pipeline with new shaders
    return createPipeline();
}

}

