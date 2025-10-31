#include "Renderer.hpp"
#include "CityGenerator.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <cstring>

namespace pcengine {

// Helper to read shader files
static std::vector<char> readShaderFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> data((size_t)len);
    fread(data.data(), 1, data.size(), f);
    fclose(f);
    return data;
}

bool Renderer::createShadowVolumePipeline() {
    std::string base = std::string(PC_ENGINE_SHADER_DIR);
    auto vertCode = readShaderFile(base + "/shadow_volume.vert.spv");
    auto fragCode = readShaderFile(base + "/shadow_volume.frag.spv");
    
    if (vertCode.empty() || fragCode.empty()) {
        printf("Failed to load shadow volume shaders\n");
        return false;
    }
    
    auto createShader = [&](const std::vector<char>& code) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule m;
        vkCreateShaderModule(device_, &ci, nullptr, &m);
        return m;
    };
    
    VkShaderModule vertShader = createShader(vertCode);
    VkShaderModule fragShader = createShader(fragCode);
    
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";
    
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";
    
    // Vertex input: just position (vec3)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 3 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = 0;
    
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkViewport viewport{};
    viewport.width = (float)swapchainExtent_.width;
    viewport.height = (float)swapchainExtent_.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.extent = swapchainExtent_;
    
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // We'll render both front and back faces in separate passes
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth-fail algorithm (Carmack's Reverse) stencil operations
    // IMPORTANT: We enable depth writes so shadow volumes occlude volumetric lighting
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;  // Write depth to block volumetric light shafts!
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.stencilTestEnable = VK_TRUE;
    
    // Default stencil ops (will use dynamic state or separate pipelines for front/back)
    depthStencil.back.failOp = VK_STENCIL_OP_KEEP;
    depthStencil.back.depthFailOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;  // Depth-fail: increment
    depthStencil.back.passOp = VK_STENCIL_OP_KEEP;
    depthStencil.back.compareMask = 0xFF;
    depthStencil.back.writeMask = 0xFF;
    depthStencil.back.reference = 0;
    depthStencil.back.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.front = depthStencil.back;
    
    // No color writes - shadow volumes only affect stencil
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0;  // Disable all color writes
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout_;  // Reuse main pipeline layout for UBO
    pipelineInfo.renderPass = hdrRenderPass_;  // Render in HDR pass
    pipelineInfo.subpass = 0;
    
    bool success = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                            nullptr, &shadowVolumePipeline_) == VK_SUCCESS;
    
    vkDestroyShaderModule(device_, vertShader, nullptr);
    vkDestroyShaderModule(device_, fragShader, nullptr);
    
    if (success) {
        printf("Created shadow volume pipeline\n");
    }
    
    return success;
}

void Renderer::renderShadowVolumes(VkCommandBuffer cmd) {
    if (!shadowVolumePipeline_ || shadowVolumeIndexCount_ == 0) return;
    
    // STEP 1: Write shadow volumes to stencil buffer
    // ================================================
    
    // Bind shadow volume pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowVolumePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                           0, 1, &descriptorSets_[0], 0, nullptr);
    
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &shadowVolumeVertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, shadowVolumeIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    
    // Two-pass rendering using depth-fail algorithm (Carmack's Reverse)
    // Result: Stencil > 0 where pixel is in shadow
    
    // Configure stencil operations for both faces
    vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
    vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFF);
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    
    // Render shadow volume geometry
    // Pipeline has: front faces decrement on depth-fail, back faces increment on depth-fail
    vkCmdDrawIndexed(cmd, shadowVolumeIndexCount_, 1, 0, 0, 0);
    
    // STEP 2: Darken pixels where stencil != 0 (in shadow)
    // =====================================================
    // TODO: Add a full-screen darkening pass here
    // For now, stencil is written but not visualized
    // This requires a separate pipeline with stencil test enabled
    // and multiplicative blending to darken shadowed areas
}

}

