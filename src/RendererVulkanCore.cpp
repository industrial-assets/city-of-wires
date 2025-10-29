#include "Renderer.hpp"
#include <GLFW/glfw3.h>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <vulkan/vulkan.h>
#include <vector>

namespace pcengine {

bool Renderer::createInstance() {
    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "ProceduralCity";
    app.apiVersion = VK_API_VERSION_1_2;

    uint32_t extCount = 0;
    const char** ext = glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> extensions(ext, ext + extCount);
#if defined(__APPLE__)
    // Required for MoltenVK portability on macOS
    extensions.push_back("VK_KHR_portability_enumeration");
#endif

    VkInstanceCreateInfo ci{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
#if defined(__APPLE__)
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    return vkCreateInstance(&ci, nullptr, &instance_) == VK_SUCCESS;
}

bool Renderer::createSurface(GLFWwindow* window) {
    return glfwCreateWindowSurface(instance_, window, nullptr, &surface_) == VK_SUCCESS;
}

bool Renderer::pickPhysicalDevice() {
    uint32_t count = 0; vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (!count) return false;
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    physicalDevice_ = devices[0];
    return true;
}

bool Renderer::createDevice() {
    uint32_t qfCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &qfCount, qf.data());
    graphicsQueueFamily_ = 0;
    presentQueueFamily_ = 0;
    for (uint32_t i = 0; i < qfCount; ++i) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &presentSupport);
        if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
            graphicsQueueFamily_ = presentQueueFamily_ = i;
            break;
        }
    }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo q{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    q.queueFamilyIndex = graphicsQueueFamily_;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    std::vector<const char*> exts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
#if defined(__APPLE__)
    exts.push_back("VK_KHR_portability_subset");
#endif
    VkDeviceCreateInfo ci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    ci.queueCreateInfoCount = 1; ci.pQueueCreateInfos = &q;
    ci.enabledExtensionCount = (uint32_t)exts.size(); ci.ppEnabledExtensionNames = exts.data();
    if (vkCreateDevice(physicalDevice_, &ci, nullptr, &device_) != VK_SUCCESS) return false;
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    presentQueue_ = graphicsQueue_;
    return true;
}

uint32_t Renderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkFormat Renderer::findDepthFormat() {
    std::vector<VkFormat> candidates = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

}

