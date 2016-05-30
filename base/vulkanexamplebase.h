/*
* Vulkan Example base class
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows")
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#elif defined(__ANDROID__)
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include "vulkanandroid.h"
#elif defined(__linux__)
#include <xcb/xcb.h>
#endif

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <sstream>
#include <streambuf>
#include <thread>
#include <vector>


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <vulkan/vulkan.h>

#include "vulkantools.h"
#include "vulkandebug.h"
#include "vulkanShaders.h"

#include "vulkanswapchain.hpp"
#include "vulkanTextureLoader.hpp"
#include "vulkanMeshLoader.hpp"
#include "vulkantextoverlay.hpp"

#define GAMEPAD_BUTTON_A 0x1000
#define GAMEPAD_BUTTON_B 0x1001
#define GAMEPAD_BUTTON_X 0x1002
#define GAMEPAD_BUTTON_Y 0x1003
#define GAMEPAD_BUTTON_L1 0x1004
#define GAMEPAD_BUTTON_R1 0x1005
#define GAMEPAD_BUTTON_START 0x1006

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1
#define ENABLE_VALIDATION true

class VulkanExampleBase
{
private:	
	// Set to true when example is created with enabled validation layers
	bool enableValidation = false;
	// Set to true when the debug marker extension is detected
	bool enableDebugMarkers = false;
	// fps timer (one second interval)
	float fpsTimer = 0.0f;
	// Create application wide Vulkan instance
	void createInstance(bool enableValidation);
	// Create logical Vulkan device based on physical device
	void createDevice(vk::DeviceQueueCreateInfo requestedQueues, bool enableValidation);
	// Get window title with example name, device, et.
	std::string getWindowTitle();
	// Destination dimensions for resizing the window
	uint32_t destWidth;
	uint32_t destHeight;
	// Called if the window is resized and some resources have to be recreatesd
	void windowResize();
protected:
	// Last frame time, measured using a high performance timer (if available)
	float frameTimer = 1.0f;
	// Frame counter to display fps
	uint32_t frameCounter = 0;
	uint32_t lastFPS = 0;
	// Vulkan instance, stores all per-application states
	vk::Instance instance;
	// Physical device (GPU) that Vulkan will ise
	vk::PhysicalDevice physicalDevice;
	// Stores physical device properties (for e.g. checking device limits)
	vk::PhysicalDeviceProperties deviceProperties;
	// Stores phyiscal device features (for e.g. checking if a feature is available)
	vk::PhysicalDeviceFeatures deviceFeatures;

	// Requested features
	vk::PhysicalDeviceFeatures requestedFeatures;
	// Stores all available memory (type) properties for the physical device
	vk::PhysicalDeviceMemoryProperties deviceMemoryProperties;
	// Logical device, application's view of the physical device (GPU)
	vk::Device device;
	// Handle to the device graphics queue that command buffers are submitted to
	vk::Queue queue;
	// Color buffer format
	vk::Format colorformat = vk::Format::eB8G8R8A8Unorm;
	// Depth buffer format
	// Depth format is selected during Vulkan initialization
	vk::Format depthFormat;
	// Command buffer pool
	vk::CommandPool cmdPool;
	// Command buffer used for setup
	vk::CommandBuffer setupCmdBuffer;
	// Command buffer for submitting a post present image barrier
	vk::CommandBuffer postPresentCmdBuffer;
	// Command buffer for submitting a pre present image barrier
	vk::CommandBuffer prePresentCmdBuffer;
	// Pipeline stage flags for the submit info structure
	vk::PipelineStageFlags submitPipelineStages = vk::PipelineStageFlagBits::eBottomOfPipe;
	// Contains command buffers and semaphores to be presented to the queue
	vk::SubmitInfo submitInfo;
	// Command buffers used for rendering
	std::vector<vk::CommandBuffer> drawCmdBuffers;
	// Global render pass for frame buffer writes
	vk::RenderPass renderPass;
	// List of available frame buffers (same as number of swap chain images)
	std::vector<vk::Framebuffer>frameBuffers;
	// Active frame buffer index
	uint32_t currentBuffer = 0;
	// Descriptor set pool
	vk::DescriptorPool descriptorPool;
	// List of shader modules created (stored for cleanup)
	std::vector<vk::ShaderModule> shaderModules;
	// Pipeline cache object
	vk::PipelineCache pipelineCache;
	// Wraps the swap chain to present images (framebuffers) to the windowing system
	VulkanSwapChain swapChain;
	// Synchronization semaphores
	struct {
		// Swap chain image presentation
		vk::Semaphore presentComplete;
		// Command buffer submission and execution
		vk::Semaphore renderComplete;
		// Text overlay submission and execution
		vk::Semaphore textOverlayComplete;
	} semaphores;
	// Simple texture loader
	vkTools::VulkanTextureLoader *textureLoader = nullptr;
	// Returns the base asset path (for shaders, models, textures) depending on the os
	const std::string getAssetPath();
public: 
	bool prepared = false;
	uint32_t width = 1280;
	uint32_t height = 720;

	VK_CLEAR_COLOR_TYPE defaultClearColor = vkTools::initializers::clearColor(glm::vec4({ 0.025f, 0.025f, 0.025f, 1.0f }));

	float zoom = 0;

	// Defines a frame rate independent timer value clamped from -1.0...1.0
	// For use in animations, rotations, etc.
	float timer = 0.0f;
	// Multiplier for speeding up (or slowing down) the global timer
	float timerSpeed = 0.25f;
	
	bool paused = false;

	bool enableTextOverlay = false;
	VulkanTextOverlay *textOverlay;

	// Use to adjust mouse rotation speed
	float rotationSpeed = 1.0f;
	// Use to adjust mouse zoom speed
	float zoomSpeed = 1.0f;

	glm::vec3 rotation = glm::vec3();
	glm::vec3 cameraPos = glm::vec3();
	glm::vec2 mousePos;

	std::string title = "Vulkan Example";
	std::string name = "vulkanExample";

	struct 
	{
		vk::Image image;
		vk::DeviceMemory mem;
		vk::ImageView view;
	} depthStencil;

	// Gamepad state (only one pad supported)

	struct GamePadState
	{
		struct Axes
		{
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
			float rz = 0.0f;
		} axes;
	} gamePadState;

	// OS specific 
#if defined(_WIN32)
	HWND window;
	HINSTANCE windowInstance;
#elif defined(__ANDROID__)
	android_app* androidApp;
	// true if application has focused, false if moved to background
	bool focused = false;
#elif defined(__linux__)
	struct {
		bool left = false;
		bool right = false;
		bool middle = false;
	} mouseButtons;
	bool quit;
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_intern_atom_reply_t *atom_wm_delete_window;
#endif

	VulkanExampleBase(bool enableValidation, const vk::PhysicalDeviceFeatures& requestedFeatures = vk::PhysicalDeviceFeatures());
	VulkanExampleBase() : VulkanExampleBase(false) {};
	~VulkanExampleBase();

	// Setup the vulkan instance, enable required extensions and connect to the physical device (GPU)
	void initVulkan(bool enableValidation);

#if defined(_WIN32)
	void setupConsole(std::string title);
	HWND setupWindow(HINSTANCE hinstance, WNDPROC wndproc);
	void handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#elif defined(__ANDROID__)
	static int32_t handleAppInput(struct android_app* app, AInputEvent* event);
	static void handleAppCommand(android_app* app, int32_t cmd);
#elif defined(__linux__)
	xcb_window_t setupWindow();
	void initxcbConnection();
	void handleEvent(const xcb_generic_event_t *event);
#endif

	// Pure virtual render function (override in derived class)
	virtual void render() = 0;
	// Called when view change occurs
	// Can be overriden in derived class to e.g. update uniform buffers 
	// Containing view dependant matrices
	virtual void viewChanged();
	// Called if a key is pressed
	// Can be overriden in derived class to do custom key handling
	virtual void keyPressed(uint32_t keyCode);
	// Called when the window has been resized
	// Can be overriden in derived class to recreate or rebuild resources attached to the frame buffer / swapchain
	virtual void windowResized();
	// Pure virtual function to be overriden by the dervice class
	// Called in case of an event where e.g. the framebuffer has to be rebuild and thus
	// all command buffers that may reference this
	virtual void buildCommandBuffers();

	// Get memory type for a given memory allocation (flags and bits)
	vk::Bool32 getMemoryType(uint32_t typeBits, const vk::MemoryPropertyFlags& properties, uint32_t *typeIndex);
	uint32_t getMemoryType(uint32_t typeBits, const vk::MemoryPropertyFlags& properties = vk::MemoryPropertyFlags());

	// Creates a new (graphics) command pool object storing command buffers
	void createCommandPool();
	// Setup default depth and stencil views
	void setupDepthStencil();
	// Create framebuffers for all requested swap chain images
	// Can be overriden in derived class to setup a custom framebuffer (e.g. for MSAA)
	virtual void setupFrameBuffer();
	// Setup a default render pass
	// Can be overriden in derived class to setup a custom render pass (e.g. for MSAA)
	virtual void setupRenderPass();

	// Connect and prepare the swap chain
	void initSwapchain();
	// Create swap chain images
	void setupSwapChain();

	// Check if command buffers are valid (!= VK_NULL_HANDLE)
	bool checkCommandBuffers();
	// Create command buffers for drawing commands
	void createCommandBuffers();
	// Destroy all command buffers and set their handles to VK_NULL_HANDLE
	// May be necessary during runtime if options are toggled 
	void destroyCommandBuffers();
	// Create command buffer for setup commands
	void createSetupCommandBuffer();
	// Finalize setup command bufferm submit it to the queue and remove it
	void flushSetupCommandBuffer();

	// Command buffer creation
	// Creates and returns a new command buffer
	vk::CommandBuffer createCommandBuffer(vk::CommandBufferLevel level, bool begin);
	// End the command buffer, submit it to the queue and free (if requested)
	// Note : Waits for the queue to become idle
	void flushCommandBuffer(vk::CommandBuffer& commandBuffer, const vk::Queue& queue, bool free);

	// Create a cache pool for rendering pipelines
	void createPipelineCache();

	// Prepare commonly used Vulkan functions
	virtual void prepare();

	// Load a SPIR-V shader
	vk::PipelineShaderStageCreateInfo loadShader(const std::string& fileName, vk::ShaderStageFlagBits stage);
	
	// Create a buffer, fill it with data (if != NULL) and bind buffer memory
	vk::Bool32 createBuffer(
		vk::BufferUsageFlags usageFlags,
		vk::MemoryPropertyFlags memoryPropertyFlags,
		vk::DeviceSize size,
		void *data,
		vk::Buffer &buffer,
		vk::DeviceMemory &memory);
	// This version always uses HOST_VISIBLE memory
	vk::Bool32 createBuffer(
		vk::BufferUsageFlags usage,
		vk::DeviceSize size,
		void *data,
		vk::Buffer &buffer,
		vk::DeviceMemory &memory);
	// Overload that assigns buffer info to descriptor
	vk::Bool32 createBuffer(
		vk::BufferUsageFlags usage,
		vk::DeviceSize size,
		void *data,
		vk::Buffer &buffer,
		vk::DeviceMemory &memory,
		vk::DescriptorBufferInfo &descriptor);
	// Overload to pass memory property flags
	vk::Bool32 createBuffer(
		vk::BufferUsageFlags usage,
		vk::MemoryPropertyFlags memoryPropertyFlags,
		vk::DeviceSize size,
		void *data,
		vk::Buffer &buffer,
		vk::DeviceMemory &memory,
		vk::DescriptorBufferInfo &descriptor);

	struct CreateBufferResult {
		vk::Buffer buf;
		vk::DeviceMemory mem;
	};

	// Overload to pass memory property flags
	CreateBufferResult createBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryPropertyFlags, vk::DeviceSize size) {
		CreateBufferResult result;
		if (!createBuffer(usage, memoryPropertyFlags, size, nullptr, result.buf, result.mem)) {
			throw std::runtime_error("Unable to allocate buffer");
		}
		return result;
	}

	template <typename T>
	CreateBufferResult createBuffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memoryPropertyFlags, const std::vector<T>& data) {
		CreateBufferResult result;
		createBuffer(usage, memoryPropertyFlags, data.size() * sizeof(T), (void*)data.data(), result.buf, result.mem);
		return result;
	}

	template <typename T>
	CreateBufferResult createBuffer(vk::BufferUsageFlags usage, const std::vector<T>& data) {
		return createBuffer(usage, vk::MemoryPropertyFlagBits::eHostVisible, data);
	}

	// Create a short lived command buffer which is immediately executed and released
	template <typename F>
	void withPrimaryCommandBuffer(F f) {
		vk::CommandBuffer commandBuffer = createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
		f(commandBuffer);
		flushCommandBuffer(commandBuffer, queue, true);
	}

	template <typename T> 
	CreateBufferResult stageToBuffer(vk::BufferUsageFlags usage, const std::vector<T>& data) {
		size_t size = sizeof(T) * data.size();
		CreateBufferResult staging = createBuffer(vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible, data);
		CreateBufferResult result = createBuffer(usage | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, size);
		withPrimaryCommandBuffer([&](vk::CommandBuffer copyCmd) {
			copyCmd.copyBuffer(staging.buf, result.buf, vk::BufferCopy(0, 0, size));
		});
		device.freeMemory(staging.mem);
		device.destroyBuffer(staging.buf);
		return result;
	}

	void copyToMemory(const vk::DeviceMemory &memory, void* data, vk::DeviceSize size, vk::DeviceSize offset = 0);

	template<typename T>
	void copyToMemory(const vk::DeviceMemory &memory, const T& data, size_t offset = 0) {
		copyToMemory(memory, data.data(), sizeof(T), offset);
	}

	template<typename T>
	void copyToMemory(const vk::DeviceMemory &memory, const std::vector<T>& data, size_t offset = 0) {
		copyToMemory(memory, data.data(), data.size() * sizeof(T), offset);
	}
	// Load a mesh (using ASSIMP) and create vulkan vertex and index buffers with given vertex layout
	void loadMesh(
		const std::string& fiename,
		vkMeshLoader::MeshBuffer *meshBuffer,
		const std::vector<vkMeshLoader::VertexLayout>& vertexLayout,
		float scale);

	// Start the main render loop
	void renderLoop();

	// Submit a pre present image barrier to the queue
	// Transforms the (framebuffer) image layout from color attachment to present(khr) for presenting to the swap chain
	void submitPrePresentBarrier(const vk::Image& image);

	// Submit a post present image barrier to the queue
	// Transforms the (framebuffer) image layout back from present(khr) to color attachment layout
	void submitPostPresentBarrier(const vk::Image& image);

	// Prepare a submit info structure containing
	// semaphores and submit buffer info for vkQueueSubmit
	vk::SubmitInfo prepareSubmitInfo(
		const std::vector<vk::CommandBuffer>& commandBuffers,
		vk::PipelineStageFlags *pipelineStages);

	void updateTextOverlay();

	// Called when the text overlay is updating
	// Can be overriden in derived class to add custom text to the overlay
	virtual void getOverlayText(VulkanTextOverlay * textOverlay);

	// Prepare the frame for workload submission
	// - Acquires the next image from the swap chain 
	// - Submits a post present barrier
	// - Sets the default wait and signal semaphores
	void prepareFrame();

	// Submit the frames' workload 
	// - Submits the text overlay (if enabled)
	// - 
	void submitFrame();

};

