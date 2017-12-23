#include "vulkanContext.hpp"

using namespace vkx;

#ifdef WIN32
__declspec(thread) VkCommandPool Context::s_cmdPool;
#else
thread_local vk::CommandPool Context::s_cmdPool;
#endif

//std::list<std::string> Context::requestedLayers { { "VK_LAYER_LUNARG_standard_validation" } };

Context::DevicePickerFunction Context::DEFAULT_DEVICE_PICKER = [](const std::vector<vk::PhysicalDevice>& devices)->vk::PhysicalDevice {
    return devices[0];
};

Context::DeviceExtensionsPickerFunction Context::DEFAULT_DEVICE_EXTENSIONS_PICKER = [](const vk::PhysicalDevice& device)->std::set<std::string> {
    return {};
};

using LayerVector = std::vector<const char*>;

LayerVector filterLayers(const LayerVector& desiredLayers) {
    static std::set<std::string> validLayerNames;
    static std::once_flag once;
    std::call_once(once, [&]{
        auto layersProperties = vk::enumerateInstanceLayerProperties();
        for (const auto& layerProperties : layersProperties) {
            validLayerNames.insert({(char*)layerProperties.layerName});
        }
    });
    LayerVector result;
    for (const auto& layer : desiredLayers) {
        std::string layerString(layer);
        if (validLayerNames.count(layerString)) {
            result.push_back(layer);
        }
    }
    return result;
}

void Context::createInstance() {
    if (enableValidation) {
        requireExtension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }
    // Vulkan instance
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "VulkanExamples";
    appInfo.pEngineName = "VulkanExamples";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::set<std::string> instanceExtensions;
    instanceExtensions.insert(requiredExtensions.begin(), requiredExtensions.end());
    for (const auto& picker : instanceExtensionsPickers) {
        auto extensions = picker();
        instanceExtensions.insert(extensions.begin(), extensions.end());
    }

    std::vector<const char*> enabledExtensions;
    for (const auto& extension : instanceExtensions) {
        enabledExtensions.push_back(extension.c_str());
    }

    // Enable surface extensions depending on os
    vk::InstanceCreateInfo instanceCreateInfo;
    instanceCreateInfo.pApplicationInfo = &appInfo;
    if (enabledExtensions.size() > 0) {
        instanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
    }

    LayerVector layers;
    if (enableValidation) {
        layers = filterLayers(debug::validationLayerNames);
        instanceCreateInfo.enabledLayerCount = (uint32_t)layers.size();
        instanceCreateInfo.ppEnabledLayerNames = layers.data();
    }

    instance = vk::createInstance(instanceCreateInfo);
}
