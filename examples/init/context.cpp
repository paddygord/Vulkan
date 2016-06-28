/*
* Vulkan Example - Multi pass offscreen rendering (bloom)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "common.hpp"
#include "vulkanTools.h"

namespace vkx {
    // A trimmed down version of our context class.
    // The full version creates additional objects required for rendering:
    //
    //  vk::Device 
    //  vk::Queue 
    //  vk::PipelineCache 
    //  vk::CommandPool
    // 
    // The full version also sets up validation layers for debugging if requested
    // See vulkanContext.hpp
    class Context {
    public:
        // Vulkan instance, stores all per-application states
        vk::Instance instance;
        std::vector<vk::PhysicalDevice> physicalDevices;
        // Physical device (GPU) that Vulkan will ise
        vk::PhysicalDevice physicalDevice;
        // Stores physical device properties (for e.g. checking device limits)
        vk::PhysicalDeviceProperties deviceProperties;
        // Stores phyiscal device features (for e.g. checking if a feature is available)
        vk::PhysicalDeviceFeatures deviceFeatures;
        // Stores all available memory (type) properties for the physical device
        vk::PhysicalDeviceMemoryProperties deviceMemoryProperties;

        Version version;
        Version driverVersion;

        void createContext() {
            {
                // Vulkan instance
                vk::ApplicationInfo appInfo;
                appInfo.pApplicationName = "VulkanExamples";
                appInfo.pEngineName = "VulkanExamples";
                appInfo.apiVersion = VK_API_VERSION_1_0;

                std::vector<const char*> enabledExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };
                // Enable surface extensions depending on os
#if defined(_WIN32)
                enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__ANDROID__)
                enabledExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
                enabledExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif

                vk::InstanceCreateInfo instanceCreateInfo;
                instanceCreateInfo.pApplicationInfo = &appInfo;
                if (enabledExtensions.size() > 0) {
                    instanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
                    instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
                }
                instance = vk::createInstance(instanceCreateInfo);
            }

#if defined(__ANDROID__)
            loadVulkanFunctions(instance);
#endif

            // Physical device
            physicalDevices = instance.enumeratePhysicalDevices();
            // Note :
            // This example will always use the first physical device reported,
            // change the vector index if you have multiple Vulkan devices installed
            // and want to use another one
            physicalDevice = physicalDevices[0];

            // Version information for Vulkan is stored in a single 32 bit integer
            // with individual bits representing the major, minor and patch versions.
            // The maximum possible major and minor version is 512 (look out nVidia)
            // while the maximum possible patch version is 2048

            // Store properties (including limits) and features of the phyiscal device
            // So examples can check against them and see if a feature is actually supported
            deviceProperties = physicalDevice.getProperties();
            version = deviceProperties.apiVersion;
            driverVersion = deviceProperties.driverVersion;
            deviceFeatures = physicalDevice.getFeatures();
            // Gather physical device memory properties
            deviceMemoryProperties = physicalDevice.getMemoryProperties();
        }

        void destroyContext() {
            instance.destroy();
        }
    };
}

std::string toHumanSize(size_t size) {
    static const std::vector<std::string> SUFFIXES{ { "B", "KB", "MB", "GB", "TB", "PB"} };
    size_t suffixIndex = 0;
    while (suffixIndex < SUFFIXES.size() - 1 && size > 1024) {
        size >>= 10;
        ++suffixIndex;
    }

    std::stringstream buffer;
    buffer << size << " " << SUFFIXES[suffixIndex];
    return buffer.str();
}

class InitContextExample {
    vkx::Context context;

public:
    InitContextExample() {
        context.createContext();
    }

    ~InitContextExample() {
        context.destroyContext();
    }

    void run() {
        std::cout << "Vulkan Context Created" << std::endl;
        std::cout << "API Version:    " << context.version.toString() << std::endl;
        std::cout << "Driver Version: " << context.driverVersion.toString() << std::endl;
        std::cout << "Device Name:    " << context.deviceProperties.deviceName << std::endl;
        std::cout << "Device Type:    " << vk::to_string(context.deviceProperties.deviceType) << std::endl;
        std::cout << "Memory Heaps:  " << context.deviceMemoryProperties.memoryHeapCount << std::endl;
        for (size_t i = 0; i < context.deviceMemoryProperties.memoryHeapCount; ++i) {
            const auto& heap = context.deviceMemoryProperties.memoryHeaps[i];
            std::cout << "\tHeap " << i << " flags " << vk::to_string(heap.flags) << " size " << toHumanSize(heap.size) << std::endl;
        }
        std::cout << std::endl;
        std::cout << "Memory Types:  " << context.deviceMemoryProperties.memoryTypeCount << std::endl;
        for (size_t i = 0; i < context.deviceMemoryProperties.memoryTypeCount; ++i) {
            const auto type = context.deviceMemoryProperties.memoryTypes[i];
            std::cout << "\tType " << i << " flags " << vk::to_string(type.propertyFlags) << " heap " << type.heapIndex << std::endl;
        }
        std::cout << std::endl;
        std::cout << "Queues:" << std::endl;
        std::vector<vk::QueueFamilyProperties> queueProps = context.physicalDevice.getQueueFamilyProperties();

        for (size_t i = 0; i < queueProps.size(); ++i) {
            const auto& queueFamilyProperties = queueProps[i];
            std::cout << std::endl;
            std::cout << "Queue Family: " << i << std::endl;
            std::cout << "\tQueue Family Flags: " << vk::to_string(queueFamilyProperties.queueFlags) << std::endl;
            std::cout << "\tQueue Count: " << queueFamilyProperties.queueCount << std::endl;
        }
        std::cout << "Press enter to exit";
        std::cin.get();
    }
};

RUN_EXAMPLE(InitContextExample)


