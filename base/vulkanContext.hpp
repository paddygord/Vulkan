#pragma once

#include <iostream>
#include <algorithm>

#include <vulkan/vk_cpp.hpp>
#include <gli/gli.hpp>

#include "vulkanDebug.h"
#include "vulkanTools.h"

namespace vkx {
    class Context {
    public:
        // Set to true when example is created with enabled validation layers
        bool enableValidation = false;
        // Set to true when the debug marker extension is detected
        bool enableDebugMarkers = false;
        // fps timer (one second interval)
        float fpsTimer = 0.0f;
        // Create application wide Vulkan instance

        void createContext(bool enableValidation) {

            this->enableValidation = enableValidation;
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
                    if (enableValidation) {
                        enabledExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
                    }
                    instanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
                    instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
                }
                if (enableValidation) {
                    instanceCreateInfo.enabledLayerCount = debug::validationLayerCount;
                    instanceCreateInfo.ppEnabledLayerNames = debug::validationLayerNames;
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
            // Store properties (including limits) and features of the phyiscal device
            // So examples can check against them and see if a feature is actually supported
            deviceProperties = physicalDevice.getProperties();
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
                // enable the debug marker extension if it is present (likely meaning a debugging tool is present)
                if (vkx::checkDeviceExtensionPresent(physicalDevice, VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
                    enabledExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
                    enableDebugMarkers = true;
                }
                if (enabledExtensions.size() > 0) {
                    deviceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
                    deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
                }
                if (enableValidation) {
                    deviceCreateInfo.enabledLayerCount = debug::validationLayerCount;
                    deviceCreateInfo.ppEnabledLayerNames = debug::validationLayerNames;
                }
                device = physicalDevice.createDevice(deviceCreateInfo);
            }

            if (enableValidation) {
                debug::setupDebugging(instance, vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning);
            }

            if (enableDebugMarkers) {
                debug::marker::setup(device);
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
            if (enableValidation) {
                debug::freeDebugCallback(instance);
            }

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
        uint32_t graphicsQueueIndex;


#ifdef WIN32
        static thread_local vk::CommandPool s_cmdPool;
#else
        static thread_local vk::CommandPool s_cmdPool;
#endif

        const vk::CommandPool& getCommandPool() const {
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

        void flushCommandBuffer(vk::CommandBuffer& commandBuffer, bool free = false) const {
            if (!commandBuffer) {
                return;
            }

            commandBuffer.end();

            vk::SubmitInfo submitInfo;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            queue.submit(submitInfo, vk::Fence());
            queue.waitIdle();

            if (free) {
                device.freeCommandBuffers(getCommandPool(), commandBuffer);
                commandBuffer = vk::CommandBuffer();
            }
        }

        // Create a short lived command buffer which is immediately executed and released
        template <typename F>
        void withPrimaryCommandBuffer(F f) const {
            vk::CommandBuffer commandBuffer = createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
            f(commandBuffer);
            flushCommandBuffer(commandBuffer, true);
        }

        CreateImageResult createImage(const vk::ImageCreateInfo& imageCreateInfo, const vk::MemoryPropertyFlags& memoryPropertyFlags) {
            CreateImageResult result;
            result.device = device;
            result.image = device.createImage(imageCreateInfo);
            result.format = imageCreateInfo.format;
            vk::MemoryRequirements memReqs = device.getImageMemoryRequirements(result.image);
            vk::MemoryAllocateInfo memAllocInfo;
            memAllocInfo.allocationSize = result.allocSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            result.memory = device.allocateMemory(memAllocInfo);
            device.bindImageMemory(result.image, result.memory, 0);
            return result;
        }

        CreateBufferResult createBuffer(const vk::BufferUsageFlags& usageFlags, const vk::MemoryPropertyFlags& memoryPropertyFlags, vk::DeviceSize size, const void * data = nullptr) const {
            CreateBufferResult result;
            result.device = device;
            result.size = size;
            result.descriptor.range = size;
            result.descriptor.offset = 0;

            vk::BufferCreateInfo bufferCreateInfo;
            bufferCreateInfo.usage = usageFlags;
            bufferCreateInfo.size = size;

            result.descriptor.buffer = result.buffer = device.createBuffer(bufferCreateInfo);

            vk::MemoryRequirements memReqs = device.getBufferMemoryRequirements(result.buffer);
            vk::MemoryAllocateInfo memAlloc;
            result.allocSize = memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
            result.memory = device.allocateMemory(memAlloc);
            if (data != nullptr) {
                copyToMemory(result.memory, data, size);
            }
            device.bindBufferMemory(result.buffer, result.memory, 0);
            return result;
        }

        CreateBufferResult createBuffer(const vk::BufferUsageFlags& usage, vk::DeviceSize size, const void * data = nullptr) const {
            return createBuffer(usage, vk::MemoryPropertyFlagBits::eHostVisible, size, data);
        }

        template <typename T>
        CreateBufferResult createBuffer(const vk::BufferUsageFlags& usage, const vk::MemoryPropertyFlags& memoryPropertyFlags, const T& data) const {
            return createBuffer(usage, memoryPropertyFlags, sizeof(T), &data);
        }

        template <typename T>
        CreateBufferResult createBuffer(const vk::BufferUsageFlags& usage, const T& data) const {
            return createBuffer(usage, vk::MemoryPropertyFlagBits::eHostVisible, data);
        }

        template <typename T>
        CreateBufferResult createBuffer(const vk::BufferUsageFlags& usage, const vk::MemoryPropertyFlags& memoryPropertyFlags, const std::vector<T>& data) const {
            return createBuffer(usage, memoryPropertyFlags, data.size() * sizeof(T), (void*)data.data());
        }

        template <typename T>
        CreateBufferResult createBuffer(const vk::BufferUsageFlags& usage, const std::vector<T>& data) const {
            return createBuffer(usage, vk::MemoryPropertyFlagBits::eHostVisible, data);
        }

        template <typename T>
        CreateBufferResult createUniformBuffer(const T& data) const {
            return createBuffer(vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, data);
        }

        void copyToMemory(const vk::DeviceMemory & memory, const void* data, vk::DeviceSize size, vk::DeviceSize offset = 0) const {
            void *mapped = device.mapMemory(memory, offset, size, vk::MemoryMapFlags());
            memcpy(mapped, data, size);
            device.unmapMemory(memory);
        }

        template<typename T>
        void copyToMemory(const vk::DeviceMemory &memory, const T& data, size_t offset = 0) const {
            copyToMemory(memory, &data, sizeof(T), offset);
        }

        template<typename T>
        void copyToMemory(const vk::DeviceMemory &memory, const std::vector<T>& data, size_t offset = 0) const {
            copyToMemory(memory, data.data(), data.size() * sizeof(T), offset);
        }

        CreateBufferResult stageToDeviceBuffer(const vk::BufferUsageFlags& usage, size_t size, const void* data) const {
            CreateBufferResult staging = createBuffer(vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible, size, data);
            CreateBufferResult result = createBuffer(usage | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, size);
            withPrimaryCommandBuffer([&](vk::CommandBuffer copyCmd) {
                copyCmd.copyBuffer(staging.buffer, result.buffer, vk::BufferCopy(0, 0, size));
            });
            device.freeMemory(staging.memory);
            device.destroyBuffer(staging.buffer);
            return result;
        }

        template <typename T>
        CreateBufferResult stageToDeviceBuffer(const vk::BufferUsageFlags& usage, const std::vector<T>& data) const {
            return stageToDeviceBuffer(usage, sizeof(T)* data.size(), data.data());
        }

        template <typename T>
        CreateBufferResult stageToDeviceBuffer(const vk::BufferUsageFlags& usage, const T& data) const {
            return stageToDeviceBuffer(usage, sizeof(T), (void*)&data);
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

    // Template specialization for texture objects
    template <>
    inline CreateBufferResult Context::createBuffer(const vk::BufferUsageFlags& usage, const gli::textureCube& texture) const {
        return createBuffer(usage, (vk::DeviceSize)texture.size(), texture.data());
    }

    template <>
    inline CreateBufferResult Context::createBuffer(const vk::BufferUsageFlags& usage, const gli::texture2D& texture) const {
        return createBuffer(usage, (vk::DeviceSize)texture.size(), texture.data());
    }

    template <>
    inline CreateBufferResult Context::createBuffer(const vk::BufferUsageFlags& usage, const gli::texture& texture) const {
        return createBuffer(usage, (vk::DeviceSize)texture.size(), texture.data());
    }

    template <>
    inline CreateBufferResult Context::createBuffer(const vk::BufferUsageFlags& usage, const vk::MemoryPropertyFlags& memoryPropertyFlags, const gli::texture& texture) const {
        return createBuffer(usage, memoryPropertyFlags, (vk::DeviceSize)texture.size(), texture.data());
    }

    template <>
    inline void Context::copyToMemory(const vk::DeviceMemory &memory, const gli::texture& texture, size_t offset) const {
        copyToMemory(memory, texture.data(), vk::DeviceSize(texture.size()), offset);
    }

}
