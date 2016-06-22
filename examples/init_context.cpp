    /*
* Vulkan Example - Multi pass offscreen rendering (bloom)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "common.hpp"

namespace vkx {
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
        // Logical device, application's view of the physical device (GPU)
        vk::Device device;
        // vk::Pipeline cache object
        vk::PipelineCache pipelineCache;

        vk::Queue queue;
        // Find a queue that supports graphics operations
        uint32_t graphicsQueueIndex{ UINT32_MAX };

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
            struct Version {
                uint32_t patch : 12;
                uint32_t minor : 10;
                uint32_t major : 10;
            } _version;
            // Store properties (including limits) and features of the phyiscal device
            // So examples can check against them and see if a feature is actually supported
            deviceProperties = physicalDevice.getProperties();
            memcpy(&_version, &deviceProperties.apiVersion, sizeof(uint32_t));
            deviceFeatures = physicalDevice.getFeatures();
            // Gather physical device memory properties
            deviceMemoryProperties = physicalDevice.getMemoryProperties();

            // Vulkan device
            {
                // Find a queue that supports graphics operations
                uint32_t graphicsQueueIndex = findQueue(vk::QueueFlagBits::eGraphics);
                std::array<float, 1> queuePriorities = { 0.0f };
                vk::DeviceQueueCreateInfo queueCreateInfo;
                queueCreateInfo.queueFamilyIndex = graphicsQueueIndex;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = queuePriorities.data();
                std::vector<const char*> enabledExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
                vk::DeviceCreateInfo deviceCreateInfo;
                deviceCreateInfo.queueCreateInfoCount = 1;
                deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
                deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
                if (enabledExtensions.size() > 0) {
                    deviceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
                    deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
                }
                device = physicalDevice.createDevice(deviceCreateInfo);
            }

            pipelineCache = device.createPipelineCache(vk::PipelineCacheCreateInfo());
            // Find a queue that supports graphics operations
            graphicsQueueIndex = findQueue(vk::QueueFlagBits::eGraphics);
            // Get the graphics queue
            queue = device.getQueue(graphicsQueueIndex, 0);
        }

        void destroyContext() {
            destroyCommandPool();
            device.destroyPipelineCache(pipelineCache);
            device.destroy();
            instance.destroy();
        }

        uint32_t findQueue(const vk::QueueFlags& flags, const vk::SurfaceKHR& presentSurface = vk::SurfaceKHR()) {
            std::vector<vk::QueueFamilyProperties> queueProps = physicalDevice.getQueueFamilyProperties();
            size_t queueCount = queueProps.size();
            for (uint32_t i = 0; i < queueCount; i++) {
                if (queueProps[i].queueFlags & flags) {
                    if (presentSurface && !physicalDevice.getSurfaceSupportKHR(i, presentSurface)) {
                        continue;
                    }
                    return i;
                }
            }
            throw std::runtime_error("No queue matches the flags " + vk::to_string(flags));
        }

#ifdef WIN32
        static __declspec(thread) VkCommandPool s_cmdPool;
#else
        static thread_local vk::CommandPool s_cmdPool;
#endif

        const vk::CommandPool getCommandPool() const {
            if (!s_cmdPool) {
                vk::CommandPoolCreateInfo cmdPoolInfo;
                cmdPoolInfo.queueFamilyIndex = graphicsQueueIndex;
                cmdPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
                s_cmdPool = device.createCommandPool(cmdPoolInfo);
            }
            return s_cmdPool;
        }

        void destroyCommandPool() {
            if (s_cmdPool) {
                device.destroyCommandPool(s_cmdPool);
                s_cmdPool = vk::CommandPool();
            }
        }

        vk::CommandBuffer createCommandBuffer(vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary, bool begin = false) const {
            vk::CommandBuffer cmdBuffer;
            vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
            cmdBufAllocateInfo.commandPool = getCommandPool();
            cmdBufAllocateInfo.level = level;
            cmdBufAllocateInfo.commandBufferCount = 1;

            cmdBuffer = device.allocateCommandBuffers(cmdBufAllocateInfo)[0];

            // If requested, also start the new command buffer
            if (begin) {
                cmdBuffer.begin(vk::CommandBufferBeginInfo());
            }

            return cmdBuffer;
        }

        vk::Bool32 getMemoryType(uint32_t typeBits, const vk::MemoryPropertyFlags& properties, uint32_t * typeIndex) const {
            for (uint32_t i = 0; i < 32; i++) {
                if ((typeBits & 1) == 1) {
                    if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                        *typeIndex = i;
                        return true;
                    }
                }
                typeBits >>= 1;
            }
            return false;
        }

        uint32_t getMemoryType(uint32_t typeBits, const vk::MemoryPropertyFlags& properties) const {
            uint32_t result = 0;
            if (!getMemoryType(typeBits, properties, &result)) {
                // todo : throw error
            }
            return result;
        }
    };

}

struct Version {
    Version(uint32_t version) {
        *((uint32_t*)this) = version;
    }
    uint32_t patch : 12;
    uint32_t minor : 10;
    uint32_t major : 10;

    std::string toString() const {
        std::stringstream buffer;
        buffer << major << "." << minor << "." << patch;
        return buffer.str();
    }
};

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
        Version apiVersion(context.deviceProperties.apiVersion);
        std::cout << "API Version:    " << apiVersion.toString() << std::endl;
        Version driverVersion(context.deviceProperties.driverVersion);
        std::cout << "Driver Version: " << driverVersion.toString() << std::endl;
        std::cout << "Device Name:    " << context.deviceProperties.deviceName << std::endl;
        std::cout << "Device Type:    " << vk::to_string(context.deviceProperties.deviceType) << std::endl;
        std::cout << "Memory Heaps:  " << context.deviceMemoryProperties.memoryHeapCount << std::endl;
        for (size_t i = 0; i < context.deviceMemoryProperties.memoryHeapCount; ++i) {
            const auto& heap = context.deviceMemoryProperties.memoryHeaps[i];
            std::cout << "\tHeap " << i << " flags " << vk::to_string(heap.flags) << " size " << toHumanSize(heap.size)  << std::endl;
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
        std::cout << "Press any key to exit";
        std::cin.get();
    }
};

RUN_EXAMPLE(InitContextExample)


