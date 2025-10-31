#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <glm/glm.hpp>
#include "FrustumCuller.hpp"

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
    float volumetricScatteringMultiplier;
    float chromaticAberrationStrength;
    // std140 requires mat4 to start at a 16-byte boundary. We have 15 floats
    // above (60 bytes), so add 1 float of padding to reach the next 16-byte boundary.
    float _pad_before_mats;
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
    struct BufferWithMemory;

    bool createInstance();
    bool createSurface(GLFWwindow* window);
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    bool createImageViews();
    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createPipelineWireframe();  // Wireframe version of city pipeline
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
    bool createGroundGeometry();
    void updateChunks();
    void rebuildGeometryIfNeeded();
    bool loadTextures();
    bool createTextureImage(const std::string& filename, VkImage& image, VkDeviceMemory& memory);
    bool createTextureImageView(VkImage image, VkImageView& imageView);
    bool createTextureSampler();
    bool loadNeonTextures();
    bool createVolumetricResources();
    void destroyVolumetricResources();
    bool createVolumetricDescriptorSets();
    bool createVolumetricPipelines();
    void recordVolumetricPasses(VkCommandBuffer cmd);
    void updateVolumetricConstants(const glm::mat4& view, const glm::mat4& proj, float nearPlane, float farPlane);
    bool createBuffer(BufferWithMemory& buffer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, bool map = false);
    void destroyBuffer(BufferWithMemory& buffer);
    
    // Shadow map methods
    bool createShadowMapResources();
    void renderShadowMap(VkCommandBuffer cmd);
    
    // Shadow volume methods
    bool createShadowVolumeGeometry();
    bool createShadowVolumePipeline();
    void renderShadowVolumes(VkCommandBuffer cmd);
    
    // Post-processing methods
    bool createHDRRenderTarget();
    bool createPostProcessingPipeline();
    bool createPostProcessingDescriptorSet();
    void renderPostProcessing(VkCommandBuffer cmd, uint32_t imageIndex);
    void updatePostProcessingDescriptors();
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
    VkPipeline graphicsPipelineWireframe_ = VK_NULL_HANDLE;  // Wireframe version for debug mode
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
    
    // Ground plane geometry
    VkBuffer groundVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory groundVertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer groundIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory groundIndexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t groundIndexCount_ = 0;
    
    // Shadow volume geometry
    VkBuffer shadowVolumeVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowVolumeVertexBufferMemory_ = VK_NULL_HANDLE;
    VkBuffer shadowVolumeIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory shadowVolumeIndexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t shadowVolumeIndexCount_ = 0;
    VkPipeline shadowVolumePipeline_ = VK_NULL_HANDLE;
    bool shadowVolumesEnabled_ = false;  // Toggle for shadow volume rendering
    
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

    struct VolumetricLightRecord {
        glm::vec4 colorIntensity{}; // rgb = color, w = intensity multiplier
        glm::vec4 positionRadius{}; // xyz = world position, w = radius
    };

    std::vector<VolumetricLightRecord> volumetricLights_;
    uint32_t volumetricLightCount_ = 0;

    struct VolumetricDensityRecord {
        glm::vec4 minBoundsSigma{};  // xyz = froxel min, w = sigma boost
        glm::vec4 maxBounds{};       // xyz = froxel max, w unused
        glm::vec4 albedo{};          // xyz = albedo, w unused
    };

    std::vector<VolumetricDensityRecord> volumetricDensities_;
    uint32_t volumetricDensityCount_ = 0;

    struct TemporalNeighborhood {
        glm::vec4 minVals{0.0f};
        glm::vec4 maxVals{0.0f};
    };

    std::vector<TemporalNeighborhood> temporalNeighborhoods_;

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
    float chunkUnloadDistance_ = 250.0f;  // Distance to unload chunks (in world units) - moderate hysteresis
    
    // Atmospheric parameters
    glm::vec3 cameraPos_ = glm::vec3(0, 100, -150);  // Start behind the city, looking towards it
    glm::vec3 fogColor_ = glm::vec3(0.08f, 0.12f, 0.15f);  // Slightly more green-tinted fog
    float fogDensity_ = 0.02f;  // Slightly denser fog
    glm::vec3 skyLightDir_ = glm::vec3(0.3f, 0.7f, 0.6f);  // Light travels: right, UP, forward (convention: toward sun for lighting calcs)
    float skyLightIntensity_ = 0.6f;  // Slightly stronger light
    
    // Frustum culling
    Frustum viewFrustum_;
    
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
    float lightShaftIntensity_ = 0.3f;  // Very subtle atmospheric light
    float lightShaftDensity_ = 0.5f;  // More ray samples for quality

    // Volumetric lighting parameters (adjustable at runtime)
    float volumetricScatteringMultiplier_ = 8.0f;  // How bright the volumetrics appear
    float volumetricLightIntensityScale_ = 1.0f;   // Scale for all light intensities
    float volumetricFogDensityScale_ = 2.0f;       // Scale for fog density
    float volumetricLightRadiusScale_ = 1.0f;      // Scale for light radii
    
    // Debug Overlay state and methods
    bool debugOverlayVisible_ = false;  // § key - show on-screen metrics
    bool debugVisualizationMode_ = false;  // ] key - wireframe/chunk boxes/bypass effects
    float frameTimes_[128] = {0}; // For FPS smoothing
    int frameTimeIndex_ = 0;
    float debug_fpsSmoothed_ = 0.0f;
    void renderDebugOverlay();
    void gatherDebugOverlayStats(float deltaSeconds);
    
    // Debug Overlay rendering resources
    VkPipeline debugTextPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout debugTextPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout debugTextDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool debugTextDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet debugTextDescriptorSet_ = VK_NULL_HANDLE;
    
    VkBuffer debugTextVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory debugTextVertexMemory_ = VK_NULL_HANDLE;
    VkBuffer debugTextIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory debugTextIndexMemory_ = VK_NULL_HANDLE;
    uint32_t debugTextIndexCount_ = 0;
    
    VkImage debugFontImage_ = VK_NULL_HANDLE;
    VkDeviceMemory debugFontMemory_ = VK_NULL_HANDLE;
    VkImageView debugFontView_ = VK_NULL_HANDLE;
    VkSampler debugFontSampler_ = VK_NULL_HANDLE;
    
    bool createDebugOverlayResources();
    bool createDebugTextPipeline();
    bool createDebugFontTexture();
    void updateDebugTextGeometry(const char* text, float x, float y, float scale);
    void renderDebugOverlayGraphical(VkCommandBuffer cmd);
    
    // Chunk visualization
    VkPipeline debugChunkPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout debugChunkPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout debugChunkDescriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool debugChunkDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet debugChunkDescriptorSet_ = VK_NULL_HANDLE;
    
    VkBuffer debugChunkVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory debugChunkVertexMemory_ = VK_NULL_HANDLE;
    uint32_t debugChunkVertexCount_ = 0;
    
    bool createDebugChunkVisualization();
    void updateDebugChunkGeometry();
    void renderDebugChunks(VkCommandBuffer cmd);
    
    // Light volume debug markers
    VkBuffer debugLightMarkerVertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory debugLightMarkerVertexMemory_ = VK_NULL_HANDLE;
    VkBuffer debugLightMarkerIndexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory debugLightMarkerIndexMemory_ = VK_NULL_HANDLE;
    uint32_t debugLightMarkerIndexCount_ = 0;
    bool debugShowLightMarkers_ = false;
    
    void updateDebugLightMarkers();
    void renderDebugLightMarkers(VkCommandBuffer cmd);
    
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

    glm::mat4 prevView_ = glm::mat4(1.0f);
    glm::mat4 prevProj_ = glm::mat4(1.0f);
    glm::mat4 prevViewProj_ = glm::mat4(1.0f);
    glm::vec3 prevCameraPos_ = glm::vec3(0.0f);
    bool hasPrevViewProj_ = false;
    uint32_t frameCounter_ = 0;

    struct VolumetricResources {
        VkImage densityImage = VK_NULL_HANDLE;
        VkDeviceMemory densityMemory = VK_NULL_HANDLE;
        VkImageView densityView = VK_NULL_HANDLE;

        VkImage lightImage = VK_NULL_HANDLE;
        VkDeviceMemory lightMemory = VK_NULL_HANDLE;
        VkImageView lightView = VK_NULL_HANDLE;

        VkImage scatteringImage = VK_NULL_HANDLE;
        VkDeviceMemory scatteringMemory = VK_NULL_HANDLE;
        VkImageView scatteringView = VK_NULL_HANDLE;

        VkImage transmittanceImage = VK_NULL_HANDLE;
        VkDeviceMemory transmittanceMemory = VK_NULL_HANDLE;
        VkImageView transmittanceView = VK_NULL_HANDLE;

        VkImage historyImage = VK_NULL_HANDLE;
        VkDeviceMemory historyMemory = VK_NULL_HANDLE;
        VkImageView historyView = VK_NULL_HANDLE;

        // Anamorphic bloom resources
        VkImage anamorphicBloomImage = VK_NULL_HANDLE;
        VkDeviceMemory anamorphicBloomMemory = VK_NULL_HANDLE;
        VkImageView anamorphicBloomView = VK_NULL_HANDLE;
        
        VkImage anamorphicTempImage = VK_NULL_HANDLE;
        VkDeviceMemory anamorphicTempMemory = VK_NULL_HANDLE;
        VkImageView anamorphicTempView = VK_NULL_HANDLE;

        BufferWithMemory constantsBuffer;
        BufferWithMemory lightRecordsBuffer;
        BufferWithMemory clusterIndicesBuffer;
        BufferWithMemory clusterOffsetsBuffer;
        BufferWithMemory densityVolumesBuffer;

        VkDescriptorSetLayout descriptorSetLayouts[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkDescriptorSetLayout anamorphicBloomDescriptorLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSets[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkDescriptorPool anamorphicBloomDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet anamorphicBloomDescriptorSets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE}; // horiz and vert

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayout anamorphicBloomPipelineLayout = VK_NULL_HANDLE;
        VkPipeline clusterPipeline = VK_NULL_HANDLE;
        VkPipeline densityPipeline = VK_NULL_HANDLE;
        VkPipeline lightPipeline = VK_NULL_HANDLE;
        VkPipeline raymarchPipeline = VK_NULL_HANDLE;
        VkPipeline temporalPipeline = VK_NULL_HANDLE;
        VkPipeline anamorphicBloomPipeline = VK_NULL_HANDLE;

        VkExtent3D froxelGrid = {160, 96, 160};
        VkExtent2D raymarchExtent = {0, 0};
        bool imagesInitialized = false;
        bool historyInitialized = false;
    };

    VolumetricResources volumetrics_{};
    bool volumetricsEnabled_ = true;
    bool volumetricsReady_ = false;
    uint32_t volumetricFrameIndex_ = 0;

    void updateVolumetricLights();
    void updateVolumetricDensities();
};

}


