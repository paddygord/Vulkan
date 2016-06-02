#pragma once

#include <iostream>

#include <vulkan/vk_cpp.hpp>
#include <gli/gli.hpp>

#include "vulkandebug.h"
#include "vulkantools.h"

namespace vkTools {

	class VulkanContext {
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
					instanceCreateInfo.enabledLayerCount = vkDebug::validationLayerCount;
					instanceCreateInfo.ppEnabledLayerNames = vkDebug::validationLayerNames;
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
				if (vkTools::checkDeviceExtensionPresent(physicalDevice, VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
					enabledExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
					enableDebugMarkers = true;
				}
				if (enabledExtensions.size() > 0) {
					deviceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
					deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
				}
				if (enableValidation) {
					deviceCreateInfo.enabledLayerCount = vkDebug::validationLayerCount;
					deviceCreateInfo.ppEnabledLayerNames = vkDebug::validationLayerNames;
				}
				device = physicalDevice.createDevice(deviceCreateInfo);
			}

			if (enableValidation) {
				vkDebug::setupDebugging(instance, (VkDebugReportFlagsEXT)(vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning), VK_NULL_HANDLE);
#if defined(_WIN32)
				// Enable console if validation is active
				// Debug message callback will output to it
				setupConsole("VulkanExample");
#endif
			}

			if (enableDebugMarkers) {
				vkDebug::DebugMarker::setup(device);
			}
			pipelineCache = device.createPipelineCache(vk::PipelineCacheCreateInfo());
		}

		void destroy() {
			device.destroyPipelineCache(pipelineCache);
			device.destroy();
			if (enableValidation) {
				vkDebug::freeDebugCallback(instance);
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
		// Pipeline cache object
		vk::PipelineCache pipelineCache;

		struct BufferResult {
			vk::Buffer buffer;
			vk::DeviceMemory memory;
		};

		BufferResult createBuffer(vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags memoryPropertyFlags, vk::DeviceSize size, const void * data = nullptr) {
			BufferResult result;

			vk::BufferCreateInfo bufferCreateInfo;
			bufferCreateInfo.usage = usageFlags;
			bufferCreateInfo.size = size;
			result.buffer = device.createBuffer(bufferCreateInfo);

			vk::MemoryRequirements memReqs = device.getBufferMemoryRequirements(result.buffer);
			vk::MemoryAllocateInfo memAlloc;
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
			result.memory = device.allocateMemory(memAlloc);
			if (data != nullptr) {
				copyToMemory(result.memory, data, size);
			}
			device.bindBufferMemory(result.buffer, result.memory, 0);
			return result;
		}

		BufferResult createBuffer(vk::BufferUsageFlags usage, vk::DeviceSize size, const void * data = nullptr) {
			return createBuffer(usage, vk::MemoryPropertyFlagBits::eHostVisible, size, data);
		}

		template <typename T>
		BufferResult createBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryPropertyFlags, const T& data) {
			return createBuffer(usage, memoryPropertyFlags, sizeof(T), &data);
		}

		template <typename T>
		BufferResult createBuffer(vk::BufferUsageFlags usage, const T& data) {
			return createBuffer(usage, vk::MemoryPropertyFlagBits::eHostVisible, data);
		}


		template <typename T>
		BufferResult createBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryPropertyFlags, const std::vector<T>& data) {
			return createBuffer(usage, memoryPropertyFlags, data.size() * sizeof(T), (void*)data.data());
		}

		template <typename T>
		BufferResult createBuffer(vk::BufferUsageFlags usage, const std::vector<T>& data) {
			return createBuffer(usage, vk::MemoryPropertyFlagBits::eHostVisible, data);
		}

		void copyToMemory(const vk::DeviceMemory & memory, const void* data, vk::DeviceSize size, vk::DeviceSize offset) {
			void *mapped = device.mapMemory(memory, offset, size, vk::MemoryMapFlags());
			memcpy(mapped, data, size);
			device.unmapMemory(memory);
		}

		template<typename T>
		void copyToMemory(const vk::DeviceMemory &memory, const T& data, size_t offset = 0) {
			copyToMemory(memory, &data, sizeof(T), offset);
		}


		template<typename T>
		void copyToMemory(const vk::DeviceMemory &memory, const std::vector<T>& data, size_t offset = 0) {
			copyToMemory(memory, data.data(), data.size() * sizeof(T), offset);
		}

		vk::Bool32 getMemoryType(uint32_t typeBits, const vk::MemoryPropertyFlags& properties, uint32_t * typeIndex) {
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

		uint32_t getMemoryType(uint32_t typeBits, const vk::MemoryPropertyFlags& properties) {
			uint32_t result = 0;
			if (!getMemoryType(typeBits, properties, &result)) {
				// todo : throw error
			}
			return result;
		}

	private:
#if defined(_WIN32)
		// Win32 : Sets up a console window and redirects standard output to it
		void setupConsole(const std::string& title) {
			AllocConsole();
			AttachConsole(GetCurrentProcessId());
			FILE *stream;
			freopen_s(&stream, "CONOUT$", "w+", stdout);
			SetConsoleTitle(TEXT(title.c_str()));
			if (enableValidation) {
				std::cout << "Validation enabled:\n";
			}
		}
#endif

	};

	// Template specialization for texture objects
	template <>
	inline VulkanContext::BufferResult VulkanContext::createBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryPropertyFlags, const gli::texture& texture) {
		return createBuffer(usage, memoryPropertyFlags, (vk::DeviceSize)texture.size(), texture.data());
	}

	template <>
	inline void VulkanContext::copyToMemory(const vk::DeviceMemory &memory, const gli::texture& texture, size_t offset) {
		copyToMemory(memory, texture.data(), vk::DeviceSize(texture.size()), offset);
	}

}