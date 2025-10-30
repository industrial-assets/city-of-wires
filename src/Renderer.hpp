#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <glm/glm.hpp>

struct GLFWwindow;

namespace pcengine {

class CityGenerator;

struct UniformBufferObject {
    float model[16];
    float view[16];
    float proj[16];
    float lightSpaceMatrix[16]; // For shadow mapping
    float cameraPos[3];
    float time;
    float fogColor[3];
    float fogDensity;
    float skyLightDir[3];
    float skyLightIntensity;
    float texTiling;
    float textureCount; // as float for std140 alignment
};

struct PostProcessingUBO {
    float exposure;
    float bloomThreshold;
    float bloomIntensity;
    float fogHeightFalloff;
    float fogHeightOffset;
    float vignetteStrength;
    float vignetteRadius;
    float grainStrength;
    float contrast;
    float saturation;
    float colorTemperature;
    float lightShaftIntensity;
    float lightShaftDensity;
    float view[16];  // View matrix for sun projection
    float proj[16];  // Projection matrix for sun projection
    float sunWorldDir[3];  // World-space sun direction (normalized, points towards sun)
    float _pad;  // Padding for std140 alignment
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    bool initialize(GLFWwindow* window);
    void shutdown();
    void waitIdle();

    void update(float deltaSeconds);
    void drawFrame();
    void checkShaderReload();
    void toggleShaderReload();
    
    // Input handling
    void processKeyboard(int key, int action);
    void processMouseMovement(float xpos, float ypos);
    void processMouseButton(int button, int action);

private:
    bool createInstance();
    bool createSurface(GLFWwindow* window);
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    bool createImageViews();
    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createNeonPipeline();
    bool createDepthResources();
    bool createFramebuffers();
    bool createCommandPoolAndBuffers();
    bool createSyncObjects();
    bool createVertexIndexBuffers();
    bool createUniformBuffers();
    bool createDescriptorPoolAndSets();
    bool reloadShaders();
    bool createCityGeometry();
    bool createNeonGeometry();
    void updateChunks();
    void rebuildGeometryIfNeeded();
    bool loadTextures();
    bool createTextureImage(const std::string& filename, VkImage& image, VkDeviceMemory& memory);
    bool createTextureImageView(VkImage image, VkImageView& imageView);
    bool createTextureSampler();
    bool loadNeonTextures();
    
    // Shadow map methods
    bool createShadowMapResources();
    void renderShadowMap(VkCommandBuffer cmd);
    
    // Post-processing methods
    bool createHDRRenderTarget();
    bool createPostProcessingPipeline();
    bool createPostProcessingDescriptorSet();
    void renderPostProcessing(VkCommandBuffer cmd, uint32_t imageIndex);
    bool createBloomTextures();
    void renderBloom(VkCommandBuffer cmd);

    void recreateSwapchain();
    void cleanupSwapchain();
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkFormat findDepthFormat();
    
    // Camera control helpers
    void updateCameraVectors();
    void processMovement(float deltaSeconds);

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    uint32_t presentQueueFamily_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent_ {0, 0};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline neonPipeline_ = VK_NULL_HANDLE;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    // Shadow map resources
    VkImage shadowMapImage_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowMapMemory_ = VK_NULL_HANDLE;
    VkImageView shadowMapView_ = VK_NULL_HANDLE;
    VkFramebuffer shadowMapFramebuffer_ = VK_NULL_HANDLE;
    VkRenderPass shadowMapRenderPass_ = VK_NULL_HANDLE;
    VkSampler shadowMapSampler_ = VK_NULL_HANDLE;
    static constexpr uint32_t shadowMapSize_ = 2048;

    std::vector<VkFramebuffer> framebuffers_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    VkSemaphore imageAvailableSemaphore_ = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore_ = VK_NULL_HANDLE;
    VkFence inFlightFence_ = VK_NULL_HANDLE;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
    
    // Fullscreen quad for post-processing
    VkBuffer fullscreenQuadBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory fullscreenQuadBufferMemory_ = VK_NULL_HANDLE;
    
    // City geometry
    VkBuffer cityVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory cityVertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer cityIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory cityIndexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t cityIndexCount_ = 0;
    
    // Neon geometry
    VkBuffer neonVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory neonVertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer neonIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory neonIndexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t neonIndexCount_ = 0;

    struct BufferWithMemory {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    std::vector<BufferWithMemory> uniformBuffers_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;

    // Texture resources (array)
    static constexpr int kMaxBuildingTextures = 8;
    VkImage buildingTextures_[kMaxBuildingTextures] = {};
    VkDeviceMemory buildingTextureMemories_[kMaxBuildingTextures] = {};
    VkImageView buildingTextureViews_[kMaxBuildingTextures] = {};
    VkSampler textureSampler_ = VK_NULL_HANDLE;
    int numBuildingTextures_ = 0;
    

    // Neon texture resources
    VkImage neonArrayImage_ = VK_NULL_HANDLE;
    VkDeviceMemory neonArrayMemory_ = VK_NULL_HANDLE;
    VkImageView neonArrayView_ = VK_NULL_HANDLE;
    int numNeonTextures_ = 0; // number of layers
    
    // Post-processing resources
    VkImage hdrColorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory hdrColorMemory_ = VK_NULL_HANDLE;
    VkImageView hdrColorView_ = VK_NULL_HANDLE;
    VkFramebuffer hdrFramebuffer_ = VK_NULL_HANDLE;
    VkRenderPass hdrRenderPass_ = VK_NULL_HANDLE;
    
    // Bloom textures (downsampled)
    std::vector<VkImage> bloomImages_;
    std::vector<VkDeviceMemory> bloomMemories_;
    std::vector<VkImageView> bloomViews_;
    std::vector<VkFramebuffer> bloomFramebuffers_;
    VkRenderPass bloomRenderPass_ = VK_NULL_HANDLE;
    
    // Post-processing pipeline
    VkPipeline postProcessingPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout postProcessingLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout postProcessingDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool postProcessingDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet postProcessingDescriptorSet_ = VK_NULL_HANDLE;
    
    // Post-processing uniform buffer
    BufferWithMemory postProcessingUBO_;

    float rotation_ = 0.0f;
    float time_ = 0.0f;
    bool hdrImageInitialized_ = false; // Track if HDR image has been transitioned from UNDEFINED
    
    // City generation
    void* cityGenerator_; // Opaque pointer to avoid forward declaration issues
    std::set<std::pair<int, int>> activeChunks_;  // Track which chunks are currently loaded
    bool geometryNeedsRebuild_ = false;  // Flag to indicate geometry needs updating
    float chunkLoadDistance_ = 150.0f;  // Distance to load chunks (in world units)
    float chunkUnloadDistance_ = 200.0f;  // Distance to unload chunks (in world units)
    
    // Atmospheric parameters
    glm::vec3 cameraPos_ = glm::vec3(0, 100, -150);  // Start behind the city, looking towards it
    glm::vec3 fogColor_ = glm::vec3(0.08f, 0.12f, 0.15f);  // Slightly more green-tinted fog
    float fogDensity_ = 0.02f;  // Slightly denser fog
    glm::vec3 skyLightDir_ = glm::vec3(0.2f, -0.8f, 0.3f);  // Original light direction
    float skyLightIntensity_ = 0.6f;  // Slightly stronger light
    
    // Post-processing parameters
    float exposure_ = 1.8f;  // Balanced exposure for HDR tone mapping
    float bloomThreshold_ = 0.7f;  // Lower threshold for more bloom
    float bloomIntensity_ = 0.0f;  // Bloom disabled until properly implemented
    float fogHeightFalloff_ = 0.15f;  // More height-based fog variation
    float fogHeightOffset_ = 0.0f;
    float vignetteStrength_ = 0.4f;  // Stronger vignette
    float vignetteRadius_ = 0.7f;  // Smaller radius for more dramatic effect
    float grainStrength_ = 0.15f;  // More film grain
    float contrast_ = 1.2f;  // Moderate contrast
    float saturation_ = 0.9f;  // Less desaturated for more vibrant colors
    float colorTemperature_ = 0.6f;  // Cooler temperature for dystopian look
    float lightShaftIntensity_ = 0.6f;  // Stronger light shafts
    float lightShaftDensity_ = 0.03f;  // Denser light shafts
    
    // Flight controls
    glm::vec3 cameraFront_ = glm::vec3(0.0f, -0.2f, 1.0f);  // Look towards the city (positive Z)
    glm::vec3 cameraUp_ = glm::vec3(0.0f, 1.0f, 0.0f);       // Up vector
    float yaw_ = 90.0f;     // Horizontal rotation (looking towards positive Z)
    float pitch_ = 0.0f;    // Vertical rotation (level, looking forward)
    float lastMouseX_ = 0.0f;
    float lastMouseY_ = 0.0f;
    bool firstMouse_ = true;
    bool mouseCaptured_ = false;
    
    // Movement state
    bool keys_[1024] = {false};  // Track key states
    float moveSpeed_ = 10.0f;    // Movement speed
    float mouseSensitivity_ = 0.1f;  // Mouse sensitivity
    
    // Hot reloading
    std::chrono::steady_clock::time_point lastShaderCheck_;
    std::filesystem::file_time_type lastVertTime_;
    std::filesystem::file_time_type lastFragTime_;
    bool shaderReloadEnabled_ = true;
};

}


