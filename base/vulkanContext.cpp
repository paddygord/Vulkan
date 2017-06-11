#include "vulkanContext.hpp"

using namespace vkx;

#ifdef WIN32
__declspec(thread) VkCommandPool Context::s_cmdPool;
#else
thread_local vk::CommandPool Context::s_cmdPool;
#endif

std::list<std::string> Context::requestedLayers { { "VK_LAYER_LUNARG_standard_validation" } };

Context::DevicePickerFunction Context::DEFAULT_DEVICE_PICKER = [](const std::vector<vk::PhysicalDevice>& devices)->vk::PhysicalDevice {
    return devices[0];
};

void Context::createInstance() {
    if (enableValidation) {
        requireExtension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }
    // Vulkan instance
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "VulkanExamples";
    appInfo.pEngineName = "VulkanExamples";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> enabledExtensions;
    for (const auto& extension : requiredExtensions) {
        enabledExtensions.push_back(extension.c_str());
    }
    // Enable surface extensions depending on os
    vk::InstanceCreateInfo instanceCreateInfo;
    instanceCreateInfo.pApplicationInfo = &appInfo;
    if (enabledExtensions.size() > 0) {
        instanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
        instanceCreateInfo.enabledLayerCount = (uint32_t)debug::validationLayerNames.size();
        instanceCreateInfo.ppEnabledLayerNames = debug::validationLayerNames.data();
    }
    instance = vk::createInstance(instanceCreateInfo);
}
