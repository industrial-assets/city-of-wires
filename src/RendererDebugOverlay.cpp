#include "Renderer.hpp"
#include "CityGenerator.hpp"
#include <vector>
#include <cstring>

namespace pcengine {

// Helper function to read file contents
static std::vector<char> readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<char> data((size_t)len);
    fread(data.data(), 1, data.size(), f); fclose(f);
    return data;
}

bool Renderer::createDebugFontTexture() {
    // Create a simple monospace bitmap font texture
    // 16x16 grid of 8x8 pixel characters = 128x128 texture
    const int charWidth = 8;
    const int charHeight = 8;
    const int charsPerRow = 16;
    const int textureSize = charWidth * charsPerRow;
    
    // Allocate texture data (single channel, 8-bit)
    std::vector<uint8_t> fontData(textureSize * textureSize, 0);
    
    // Generate an ASCII bitmap font (5x7 glyphs packed into an 8x8 cell)
    auto setPixel = [&](int x, int y, uint8_t val) {
        if (x >= 0 && x < textureSize && y >= 0 && y < textureSize) {
            fontData[y * textureSize + x] = val;
        }
    };
    
    // 5x7 font covering ASCII 0x20..0x7F (space to tilde). Each row is one byte; least-significant 5 bits are used.
    // Source: compact public-domain 5x7 font commonly used in embedded examples (manually inlined here).
    static const uint8_t font5x7[96][7] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
        {0x04,0x04,0x04,0x04,0x00,0x00,0x04}, // 0x21 '!'
        {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // 0x22 '"'
        {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // 0x23 '#'
        {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // 0x24 '$'
        {0x19,0x19,0x02,0x04,0x08,0x13,0x13}, // 0x25 '%'
        {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // 0x26 '&'
        {0x06,0x04,0x08,0x00,0x00,0x00,0x00}, // 0x27 '\''
        {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // 0x28 '('
        {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // 0x29 ')'
        {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00}, // 0x2A '*'
        {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // 0x2B '+'
        {0x00,0x00,0x00,0x00,0x06,0x04,0x08}, // 0x2C ','
        {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 0x2D '-'
        {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // 0x2E '.'
        {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // 0x2F '/'
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0x30 '0'
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 0x31 '1'
        {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 0x32 '2'
        {0x1F,0x01,0x02,0x06,0x01,0x11,0x0E}, // 0x33 '3'
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 0x34 '4'
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 0x35 '5'
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 0x36 '6'
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 0x37 '7'
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 0x38 '8'
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 0x39 '9'
        {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // 0x3A ':'
        {0x00,0x0C,0x0C,0x00,0x06,0x04,0x08}, // 0x3B ';'
        {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // 0x3C '<'
        {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // 0x3D '='
        {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // 0x3E '>'
        {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // 0x3F '?'
        {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // 0x40 '@'
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 0x41 'A'
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 0x42 'B'
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 0x43 'C'
        {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // 0x44 'D'
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 0x45 'E'
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 0x46 'F'
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 0x47 'G'
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 0x48 'H'
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 0x49 'I'
        {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}, // 0x4A 'J'
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 0x4B 'K'
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 0x4C 'L'
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 0x4D 'M'
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 0x4E 'N'
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 0x4F 'O'
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 0x50 'P'
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 0x51 'Q'
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 0x52 'R'
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 0x53 'S'
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 0x54 'T'
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 0x55 'U'
        {0x11,0x11,0x11,0x0A,0x0A,0x04,0x04}, // 0x56 'V'
        {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 0x57 'W'
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 0x58 'X'
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 0x59 'Y'
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 0x5A 'Z'
        {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // 0x5B '['
        {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, // 0x5C '\\'
        {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // 0x5D ']'
        {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // 0x5E '^'
        {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // 0x5F '_'
        {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, // 0x60 '`'
        {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 0x61 'a'
        {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, // 0x62 'b'
        {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E}, // 0x63 'c'
        {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, // 0x64 'd'
        {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 0x65 'e'
        {0x06,0x08,0x1C,0x08,0x08,0x08,0x08}, // 0x66 'f'
        {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}, // 0x67 'g'
        {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // 0x68 'h'
        {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 0x69 'i'
        {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 0x6A 'j'
        {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 0x6B 'k'
        {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 0x6C 'l'
        {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 0x6D 'm'
        {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // 0x6E 'n'
        {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 0x6F 'o'
        {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10}, // 0x70 'p'
        {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01}, // 0x71 'q'
        {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 0x72 'r'
        {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, // 0x73 's'
        {0x08,0x08,0x1C,0x08,0x08,0x08,0x06}, // 0x74 't'
        {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, // 0x75 'u'
        {0x00,0x00,0x11,0x11,0x0A,0x0A,0x04}, // 0x76 'v'
        {0x00,0x00,0x11,0x15,0x15,0x1B,0x11}, // 0x77 'w'
        {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 0x78 'x'
        {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 0x79 'y'
        {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 0x7A 'z'
        {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, // 0x7B '{'
        {0x04,0x04,0x04,0x00,0x04,0x04,0x04}, // 0x7C '|'
        {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, // 0x7D '}'
        {0x08,0x15,0x02,0x00,0x00,0x00,0x00}  // 0x7E '~'
    };

    for (int charCode = 32; charCode < 128; ++charCode) {
        int charX = (charCode % charsPerRow) * charWidth;
        int charY = (charCode / charsPerRow) * charHeight;

        const uint8_t* glyph = font5x7[charCode - 32];
        for (int gy = 0; gy < 7; ++gy) {
            uint8_t rowBits = glyph[gy];
            for (int gx = 0; gx < 5; ++gx) {
                if (rowBits & (1 << gx)) {
                    // Center the 5x7 glyph inside the 8x8 cell with a 1px border
                    setPixel(charX + 1 + gx, charY + 1 + gy, 255);
                }
            }
        }
    }
    
    // Create Vulkan image
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = textureSize;
    imageInfo.extent.height = textureSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    if (vkCreateImage(device_, &imageInfo, nullptr, &debugFontImage_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, debugFontImage_, &memReq);
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &debugFontMemory_) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, debugFontImage_, debugFontMemory_, 0);
    
    // Create staging buffer and upload font data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize bufferSize = fontData.size();
    
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(device_, &bufferInfo, nullptr, &stagingBuffer);
    
    vkGetBufferMemoryRequirements(device_, stagingBuffer, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &allocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device_, stagingBuffer, stagingMemory, 0);
    
    void* data;
    vkMapMemory(device_, stagingMemory, 0, bufferSize, 0, &data);
    memcpy(data, fontData.data(), bufferSize);
    vkUnmapMemory(device_, stagingMemory);
    
    // Transition image and copy buffer to image
    VkCommandBuffer cmd = commandBuffers_[0];
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = debugFontImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)textureSize, (uint32_t)textureSize, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, debugFontImage_, 
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    
    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingMemory, nullptr);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = debugFontImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device_, &viewInfo, nullptr, &debugFontView_) != VK_SUCCESS) return false;
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    
    return vkCreateSampler(device_, &samplerInfo, nullptr, &debugFontSampler_) == VK_SUCCESS;
}

bool Renderer::createDebugTextPipeline() {
    // Load shaders
    std::string shaderDir = std::string(PC_ENGINE_SHADER_DIR);
    auto vertCode = readFile(shaderDir + "/debug_text.vert.spv");
    auto fragCode = readFile(shaderDir + "/debug_text.frag.spv");
    
    if (vertCode.empty() || fragCode.empty()) {
        printf("Failed to load debug text shaders\n");
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
    
    // Vertex input: pos(2) + texCoord(2) + color(3) = 7 floats
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 7 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attributes[3] = {};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = 0;
    
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = 2 * sizeof(float);
    
    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = 4 * sizeof(float);
    
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;
    
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    
    // Enable alpha blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Create descriptor set layout if it doesn't exist (for swapchain recreation)
    if (debugTextDescriptorLayout_ == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = 0;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerBinding;
        
        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &debugTextDescriptorLayout_) != VK_SUCCESS) {
            vkDestroyShaderModule(device_, vertShader, nullptr);
            vkDestroyShaderModule(device_, fragShader, nullptr);
            return false;
        }
    }
    
    // Create pipeline layout if it doesn't exist (for swapchain recreation)
    if (debugTextPipelineLayout_ == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &debugTextDescriptorLayout_;
        
        if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &debugTextPipelineLayout_) != VK_SUCCESS) {
            vkDestroyShaderModule(device_, vertShader, nullptr);
            vkDestroyShaderModule(device_, fragShader, nullptr);
            return false;
        }
    }
    
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
    pipelineInfo.layout = debugTextPipelineLayout_;
    pipelineInfo.renderPass = renderPass_; // Use main render pass
    pipelineInfo.subpass = 0;
    
    bool success = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, 
                                            nullptr, &debugTextPipeline_) == VK_SUCCESS;
    
    vkDestroyShaderModule(device_, vertShader, nullptr);
    vkDestroyShaderModule(device_, fragShader, nullptr);
    
    return success;
}

bool Renderer::createDebugOverlayResources() {
    // Create font texture
    if (!createDebugFontTexture()) {
        printf("Failed to create debug font texture\n");
        return false;
    }
    
    // Create pipeline
    if (!createDebugTextPipeline()) {
        printf("Failed to create debug text pipeline\n");
        return false;
    }
    
    // Create vertex buffer (large enough for ~2000 characters)
    const size_t maxChars = 2000;
    const size_t maxVertices = maxChars * 4;
    const size_t vertexBufferSize = maxVertices * 7 * sizeof(float);
    
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = vertexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &debugTextVertexBuffer_) != VK_SUCCESS) return false;
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, debugTextVertexBuffer_, &memReq);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &debugTextVertexMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, debugTextVertexBuffer_, debugTextVertexMemory_, 0);
    
    // Create index buffer
    const size_t maxIndices = maxChars * 6;
    const size_t indexBufferSize = maxIndices * sizeof(uint16_t);
    
    bufferInfo.size = indexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    
    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &debugTextIndexBuffer_) != VK_SUCCESS) return false;
    
    vkGetBufferMemoryRequirements(device_, debugTextIndexBuffer_, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &debugTextIndexMemory_) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, debugTextIndexBuffer_, debugTextIndexMemory_, 0);
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &debugTextDescriptorPool_) != VK_SUCCESS) return false;
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = debugTextDescriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &debugTextDescriptorLayout_;
    
    if (vkAllocateDescriptorSets(device_, &allocateInfo, &debugTextDescriptorSet_) != VK_SUCCESS) return false;
    
    // Update descriptor set
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = debugFontView_;
    imageInfo.sampler = debugFontSampler_;
    
    VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptorWrite.dstSet = debugTextDescriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    
    printf("Debug overlay resources created successfully\n");
    return true;
}

bool Renderer::createDebugChunkVisualization() {
    // Load shaders
    std::string shaderDir = std::string(PC_ENGINE_SHADER_DIR);
    auto vertCode = readFile(shaderDir + "/debug_chunk.vert.spv");
    auto fragCode = readFile(shaderDir + "/debug_chunk.frag.spv");
    
    if (vertCode.empty() || fragCode.empty()) {
        printf("Failed to load debug chunk shaders\n");
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
    
    // Vertex input: pos(3) + color(3) = 6 floats
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 6 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attributes[2] = {};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;
    
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = 3 * sizeof(float);
    
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 2.0f;  // Thicker lines for visibility
    
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth buffer
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    
    // Enable alpha blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Create pipeline layout if it doesn't exist (for swapchain recreation)
    if (debugChunkPipelineLayout_ == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;  // Reuse main descriptor layout
        
        if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &debugChunkPipelineLayout_) != VK_SUCCESS) {
            vkDestroyShaderModule(device_, vertShader, nullptr);
            vkDestroyShaderModule(device_, fragShader, nullptr);
            return false;
        }
    }
    
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
    pipelineInfo.layout = debugChunkPipelineLayout_;
    pipelineInfo.renderPass = hdrRenderPass_;  // Render in HDR pass (before post-processing)
    pipelineInfo.subpass = 0;
    
    bool success = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, 
                                            nullptr, &debugChunkPipeline_) == VK_SUCCESS;
    
    vkDestroyShaderModule(device_, vertShader, nullptr);
    vkDestroyShaderModule(device_, fragShader, nullptr);
    
    if (!success) return false;
    
    // Create vertex buffer if it doesn't exist (for swapchain recreation)
    if (debugChunkVertexBuffer_ == VK_NULL_HANDLE) {
        const size_t maxChunks = 100;
        const size_t verticesPerChunk = 24;  // 12 edges * 2 vertices per edge
        const size_t maxVertices = maxChunks * verticesPerChunk;
        const size_t vertexBufferSize = maxVertices * 6 * sizeof(float);
        
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = vertexBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        
        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &debugChunkVertexBuffer_) != VK_SUCCESS) return false;
        
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device_, debugChunkVertexBuffer_, &memReq);
        
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device_, &allocInfo, nullptr, &debugChunkVertexMemory_) != VK_SUCCESS) return false;
        vkBindBufferMemory(device_, debugChunkVertexBuffer_, debugChunkVertexMemory_, 0);
    }
    
    printf("Debug chunk visualization created successfully\n");
    return true;
}

void Renderer::updateDebugChunkGeometry() {
    std::vector<float> vertices;
    
    CityGenerator* gen = static_cast<CityGenerator*>(cityGenerator_);
    float chunkSize = gen->getChunkSize();
    
    // Limit to prevent buffer overflow (100 chunks = 2400 vertices max)
    const size_t maxChunksToRender = 100;
    size_t chunksRendered = 0;
    
    // Generate wireframe box for each active chunk
    for (const auto& chunk : activeChunks_) {
        if (chunksRendered >= maxChunksToRender) break;
        int chunkX = chunk.first;
        int chunkZ = chunk.second;
        
        // Calculate chunk boundaries in world space
        float minX = chunkX * chunkSize;
        float maxX = (chunkX + 1) * chunkSize;
        float minZ = chunkZ * chunkSize;
        float maxZ = (chunkZ + 1) * chunkSize;
        float minY = 0.0f;
        float maxY = 5.0f;  // Height of the boundary box
        
        // Color based on distance from camera chunk
        int camChunkX = static_cast<int>(std::floor(cameraPos_.x / chunkSize));
        int camChunkZ = static_cast<int>(std::floor(cameraPos_.z / chunkSize));
        int distX = std::abs(chunkX - camChunkX);
        int distZ = std::abs(chunkZ - camChunkZ);
        int maxDist = std::max(distX, distZ);
        
        glm::vec3 color;
        if (maxDist == 0) {
            color = glm::vec3(0.0f, 1.0f, 0.0f);  // Green for current chunk
        } else if (maxDist <= 1) {
            color = glm::vec3(1.0f, 1.0f, 0.0f);  // Yellow for adjacent chunks
        } else {
            color = glm::vec3(0.0f, 0.5f, 1.0f);  // Blue for far chunks
        }
        
        // 12 edges of the box, each edge is 2 vertices (line)
        // Bottom square
        vertices.insert(vertices.end(), {minX, minY, minZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {maxX, minY, minZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {maxX, minY, minZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {maxX, minY, maxZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {maxX, minY, maxZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {minX, minY, maxZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {minX, minY, maxZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {minX, minY, minZ, color.r, color.g, color.b});
        
        // Top square
        vertices.insert(vertices.end(), {minX, maxY, minZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {maxX, maxY, minZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {maxX, maxY, minZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {maxX, maxY, maxZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {maxX, maxY, maxZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {minX, maxY, maxZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {minX, maxY, maxZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {minX, maxY, minZ, color.r, color.g, color.b});
        
        // Vertical edges
        vertices.insert(vertices.end(), {minX, minY, minZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {minX, maxY, minZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {maxX, minY, minZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {maxX, maxY, minZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {maxX, minY, maxZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {maxX, maxY, maxZ, color.r, color.g, color.b});
        
        vertices.insert(vertices.end(), {minX, minY, maxZ, color.r, color.g, color.b});
        vertices.insert(vertices.end(), {minX, maxY, maxZ, color.r, color.g, color.b});
        
        chunksRendered++;
    }
    
    debugChunkVertexCount_ = static_cast<uint32_t>(vertices.size() / 6);
    
    if (debugChunkVertexCount_ > 0 && debugChunkVertexBuffer_) {
        void* data;
        vkMapMemory(device_, debugChunkVertexMemory_, 0, vertices.size() * sizeof(float), 0, &data);
        memcpy(data, vertices.data(), vertices.size() * sizeof(float));
        vkUnmapMemory(device_, debugChunkVertexMemory_);
    }
}

void Renderer::renderDebugChunks(VkCommandBuffer cmd) {
    if (!debugChunkPipeline_ || debugChunkVertexCount_ == 0) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugChunkPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugChunkPipelineLayout_, 
                           0, 1, &descriptorSets_[0], 0, nullptr);
    
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &debugChunkVertexBuffer_, &offset);
    vkCmdDraw(cmd, debugChunkVertexCount_, 1, 0, 0);
}

void Renderer::updateDebugLightMarkers() {
    if (!cityGenerator_ || !device_) return;
    
    auto* gen = static_cast<CityGenerator*>(cityGenerator_);
    const auto& neonLights = gen->getNeonLights();
    const auto& lightVolumes = gen->getLightVolumes();
    
    printf("=== Debug Light Markers ===\n");
    printf("Total neons: %zu, Total volumes: %zu\n", neonLights.size(), lightVolumes.size());
    if (!neonLights.empty()) {
        printf("First 3 neon positions:\n");
        for (size_t i = 0; i < std::min(size_t(3), neonLights.size()); ++i) {
            printf("  [%zu] pos=(%.1f, %.1f, %.1f)\n", i, 
                   neonLights[i].position.x, neonLights[i].position.y, neonLights[i].position.z);
        }
    }
    if (!lightVolumes.empty()) {
        printf("First 3 volume positions:\n");
        for (size_t i = 0; i < std::min(size_t(3), lightVolumes.size()); ++i) {
            printf("  [%zu] pos=(%.1f, %.1f, %.1f) %s\n", i,
                   lightVolumes[i].basePosition.x, lightVolumes[i].basePosition.y, lightVolumes[i].basePosition.z,
                   lightVolumes[i].isCone ? "CONE" : "CUBE");
        }
    }
    
    // Create simple point markers for each light volume (much more memory efficient)
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    
    // Add markers for neon lights (cyan markers) - only show nearby ones
    size_t maxNeons = std::min(neonLights.size(), size_t(300));
    for (size_t idx = 0; idx < maxNeons; ++idx) {
        const auto& neon = neonLights[idx];
        glm::vec3 pos = neon.position;
        float size = 1.5f; // Larger markers for visibility
        glm::vec3 color = glm::vec3(0.0f, 1.0f, 1.0f); // Bright cyan for neons
        
        uint32_t baseIndex = static_cast<uint32_t>(vertices.size() / 6);
        
        // Simple cross marker
        glm::vec3 points[4] = {
            pos + glm::vec3(-size, 0, 0),
            pos + glm::vec3( size, 0, 0),
            pos + glm::vec3(0, -size, 0),
            pos + glm::vec3(0,  size, 0)
        };
        
        for (int i = 0; i < 4; ++i) {
            vertices.push_back(points[i].x);
            vertices.push_back(points[i].y);
            vertices.push_back(points[i].z);
            vertices.push_back(color.r);
            vertices.push_back(color.g);
            vertices.push_back(color.b);
        }
        
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 3);
    }
    
    // Add markers for light volumes (red for beams, green for cubes)
    size_t maxVolumes = std::min(lightVolumes.size(), size_t(200));
    for (size_t idx = 0; idx < maxVolumes; ++idx) {
        const auto& volume = lightVolumes[idx];
        glm::vec3 pos = volume.basePosition;
        float size = 3.0f; // Larger for volume markers
        glm::vec3 color = volume.isCone ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f); // Bright red/green
        
        uint32_t baseIndex = static_cast<uint32_t>(vertices.size() / 6);
        
        // Just 2 crossing lines instead of full cube (4 points, 4 lines = 8 indices vs 24)
        glm::vec3 points[4] = {
            pos + glm::vec3(-size, 0, 0),
            pos + glm::vec3( size, 0, 0),
            pos + glm::vec3(0, -size, 0),
            pos + glm::vec3(0,  size, 0)
        };
        
        for (int i = 0; i < 4; ++i) {
            vertices.push_back(points[i].x);
            vertices.push_back(points[i].y);
            vertices.push_back(points[i].z);
            vertices.push_back(color.r);
            vertices.push_back(color.g);
            vertices.push_back(color.b);
        }
        
        // 2 crossing lines (X shape)
        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 3);
    }
    
    if (maxNeons < neonLights.size() || maxVolumes < lightVolumes.size()) {
        printf("  Debug markers: %zu/%zu neons (cyan), %zu/%zu volumes (red/green)\n", 
               maxNeons, neonLights.size(), maxVolumes, lightVolumes.size());
    }
    
    debugLightMarkerIndexCount_ = static_cast<uint32_t>(indices.size());
    
    if (debugLightMarkerIndexCount_ == 0) return;
    
    // Create or recreate buffers
    if (debugLightMarkerVertexBuffer_) {
        vkDestroyBuffer(device_, debugLightMarkerVertexBuffer_, nullptr);
        vkFreeMemory(device_, debugLightMarkerVertexMemory_, nullptr);
        vkDestroyBuffer(device_, debugLightMarkerIndexBuffer_, nullptr);
        vkFreeMemory(device_, debugLightMarkerIndexMemory_, nullptr);
    }
    
    // Vertex buffer
    VkDeviceSize vertexBufferSize = vertices.size() * sizeof(float);
    VkBufferCreateInfo vertexBufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vertexBufferInfo.size = vertexBufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    
    if (vkCreateBuffer(device_, &vertexBufferInfo, nullptr, &debugLightMarkerVertexBuffer_) != VK_SUCCESS) {
        debugLightMarkerIndexCount_ = 0;
        return;
    }
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, debugLightMarkerVertexBuffer_, &memReq);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &debugLightMarkerVertexMemory_) != VK_SUCCESS) {
        vkDestroyBuffer(device_, debugLightMarkerVertexBuffer_, nullptr);
        debugLightMarkerVertexBuffer_ = VK_NULL_HANDLE;
        debugLightMarkerIndexCount_ = 0;
        return;
    }
    vkBindBufferMemory(device_, debugLightMarkerVertexBuffer_, debugLightMarkerVertexMemory_, 0);
    
    void* data;
    vkMapMemory(device_, debugLightMarkerVertexMemory_, 0, vertexBufferSize, 0, &data);
    std::memcpy(data, vertices.data(), vertexBufferSize);
    vkUnmapMemory(device_, debugLightMarkerVertexMemory_);
    
    // Index buffer
    VkDeviceSize indexBufferSize = indices.size() * sizeof(uint32_t);
    VkBufferCreateInfo indexBufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    indexBufferInfo.size = indexBufferSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    
    if (vkCreateBuffer(device_, &indexBufferInfo, nullptr, &debugLightMarkerIndexBuffer_) != VK_SUCCESS) {
        debugLightMarkerIndexCount_ = 0;
        return;
    }
    
    vkGetBufferMemoryRequirements(device_, debugLightMarkerIndexBuffer_, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    if (vkAllocateMemory(device_, &allocInfo, nullptr, &debugLightMarkerIndexMemory_) != VK_SUCCESS) {
        vkDestroyBuffer(device_, debugLightMarkerIndexBuffer_, nullptr);
        debugLightMarkerIndexBuffer_ = VK_NULL_HANDLE;
        debugLightMarkerIndexCount_ = 0;
        return;
    }
    vkBindBufferMemory(device_, debugLightMarkerIndexBuffer_, debugLightMarkerIndexMemory_, 0);
    
    vkMapMemory(device_, debugLightMarkerIndexMemory_, 0, indexBufferSize, 0, &data);
    std::memcpy(data, indices.data(), indexBufferSize);
    vkUnmapMemory(device_, debugLightMarkerIndexMemory_);
}

void Renderer::renderDebugLightMarkers(VkCommandBuffer cmd) {
    if (!debugShowLightMarkers_ || !debugChunkPipeline_ || !debugChunkPipelineLayout_ || 
        !descriptorSets_[0] || debugLightMarkerIndexCount_ == 0 || 
        !debugLightMarkerVertexBuffer_ || !debugLightMarkerIndexBuffer_) {
        return;
    }
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugChunkPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugChunkPipelineLayout_, 
                           0, 1, &descriptorSets_[0], 0, nullptr);
    
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &debugLightMarkerVertexBuffer_, &offset);
    vkCmdBindIndexBuffer(cmd, debugLightMarkerIndexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, debugLightMarkerIndexCount_, 1, 0, 0, 0);
}

}

