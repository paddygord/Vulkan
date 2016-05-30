/*
* Class wrapping access to the swap chain
* 
* A swap chain is a collection of framebuffers used for rendering
* The swap chain images can then presented to the windowing system
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <assert.h>
#include <stdio.h>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#endif

#include <vulkan/vulkan.h>
#include "vulkantools.h"

#ifdef __ANDROID__
#include "vulkanandroid.h"
#endif

typedef struct _SwapChainBuffers {
	vk::Image image;
	vk::ImageView view;
} SwapChainBuffer;

class VulkanSwapChain
{
private: 
	vk::Instance instance;
	vk::Device device;
	vk::PhysicalDevice physicalDevice;
	vk::SurfaceKHR surface;
public:
	vk::Format colorFormat;
	vk::ColorSpaceKHR colorSpace;

	vk::SwapchainKHR swapChain;

	uint32_t imageCount;
	std::vector<vk::Image> images;
	std::vector<SwapChainBuffer> buffers;

	// Index of the deteced graphics and presenting device queue
	uint32_t queueNodeIndex = UINT32_MAX;

	// Creates an os specific surface
	// Tries to find a graphics and a present queue
	void initSurface(
#ifdef _WIN32
		void* platformHandle, void* platformWindow
#else
#ifdef __ANDROID__
		ANativeWindow* window
#else
		xcb_connection_t* connection, xcb_window_t window
#endif
#endif
	)
	{
		// Create surface depending on OS
#ifdef _WIN32
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo;
		surfaceCreateInfo.hinstance = (HINSTANCE)platformHandle;
		surfaceCreateInfo.hwnd = (HWND)platformWindow;
		surface = instance.createWin32SurfaceKHR(surfaceCreateInfo);
#else
#ifdef __ANDROID__
		vk::AndroidSurfaceCreateInfoKHR surfaceCreateInfo;
		surfaceCreateInfo.window = window;
		surface = instance.createAndroidSurfaceKHR(surfaceCreateInfo);
#else
		vk::XcbSurfaceCreateInfoKHR surfaceCreateInfo;
		surfaceCreateInfo.connection = connection;
		surfaceCreateInfo.window = window;
		surface = instance.createXcbSurfaceKHR(surfaceCreateInfo);
#endif
#endif

		// Get available queue family properties
		std::vector<vk::QueueFamilyProperties> queueProps = physicalDevice.getQueueFamilyProperties();
		auto queueCount = queueProps.size();

		// Iterate over each queue to learn whether it supports presenting:
		// Find a queue with present support
		// Will be used to present the swap chain images to the windowing system
		std::vector<vk::Bool32> supportsPresent(queueCount);
		for (uint32_t i = 0; i < queueCount; i++) 
		{
			supportsPresent[i] = physicalDevice.getSurfaceSupportKHR(i, surface);
		}

		// Search for a graphics and a present queue in the array of queue
		// families, try to find one that supports both
		uint32_t graphicsQueueNodeIndex = UINT32_MAX;
		uint32_t presentQueueNodeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < queueCount; i++) 
		{
			if (queueProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
			{
				if (graphicsQueueNodeIndex == UINT32_MAX) 
				{
					graphicsQueueNodeIndex = i;
				}

				if (supportsPresent[i] == VK_TRUE) 
				{
					graphicsQueueNodeIndex = i;
					presentQueueNodeIndex = i;
					break;
				}
			}
		}
		if (presentQueueNodeIndex == UINT32_MAX) 
		{	
			// If there's no queue that supports both present and graphics
			// try to find a separate present queue
			for (uint32_t i = 0; i < queueCount; ++i) 
			{
				if (supportsPresent[i] == VK_TRUE) 
				{
					presentQueueNodeIndex = i;
					break;
				}
			}
		}

		// Exit if either a graphics or a presenting queue hasn't been found
		if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) 
		{
			vkTools::exitFatal("Could not find a graphics and/or presenting queue!", "Fatal error");
		}

		// todo : Add support for separate graphics and presenting queue
		if (graphicsQueueNodeIndex != presentQueueNodeIndex) 
		{
			vkTools::exitFatal("Separate graphics and presenting queues are not supported yet!", "Fatal error");
		}

		queueNodeIndex = graphicsQueueNodeIndex;

		// Get list of supported surface formats
		std::vector<vk::SurfaceFormatKHR> surfaceFormats = physicalDevice.getSurfaceFormatsKHR(surface);
		auto formatCount = surfaceFormats.size();


		// If the surface format list only includes one entry with vk::Format::eUndefined,
		// there is no preferered format, so we assume vk::Format::eB8G8R8A8Unorm
		if ((formatCount == 1) && (surfaceFormats[0].format == vk::Format::eUndefined))
		{
			colorFormat = vk::Format::eB8G8R8A8Unorm;
		}
		else
		{
			// Always select the first available color format
			// If you need a specific format (e.g. SRGB) you'd need to
			// iterate over the list of available surface format and
			// check for it's presence
			colorFormat = surfaceFormats[0].format;
		}
		colorSpace = surfaceFormats[0].colorSpace;
	}

	// Connect to the instance und device and get all required function pointers
	void connect(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device)
	{
		this->instance = instance;
		this->physicalDevice = physicalDevice;
		this->device = device;
	}

	// Create the swap chain and get images with given width and height
	void create(vk::CommandBuffer cmdBuffer, uint32_t *width, uint32_t *height)
	{
		vk::SwapchainKHR oldSwapchain = swapChain;

		// Get physical device surface properties and formats
		vk::SurfaceCapabilitiesKHR surfCaps = physicalDevice.getSurfaceCapabilitiesKHR(surface).value;
		// Get available present modes
		std::vector<vk::PresentModeKHR> presentModes = physicalDevice.getSurfacePresentModesKHR(surface);
		auto presentModeCount = presentModes.size();

		vk::Extent2D swapchainExtent;
		// width and height are either both -1, or both not -1.
		if (surfCaps.currentExtent.width == -1)
		{
			// If the surface size is undefined, the size is set to
			// the size of the images requested.
			swapchainExtent.width = *width;
			swapchainExtent.height = *height;
		}
		else
		{
			// If the surface size is defined, the swap chain size must match
			swapchainExtent = surfCaps.currentExtent;
			*width = surfCaps.currentExtent.width;
			*height = surfCaps.currentExtent.height;
		}

		// Prefer mailbox mode if present, it's the lowest latency non-tearing present  mode
		vk::PresentModeKHR swapchainPresentMode = vk::PresentModeKHR::eFifo;
		for (size_t i = 0; i < presentModeCount; i++) 
		{
			if (presentModes[i] == vk::PresentModeKHR::eMailbox) 
			{
				swapchainPresentMode = vk::PresentModeKHR::eMailbox;
				break;
			}
			if ((swapchainPresentMode != vk::PresentModeKHR::eMailbox) && (presentModes[i] == vk::PresentModeKHR::eImmediate)) 
			{
				swapchainPresentMode = vk::PresentModeKHR::eImmediate;
			}
		}

		// Determine the number of images
		uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
		if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount))
		{
			desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
		}

		vk::SurfaceTransformFlagBitsKHR preTransform;
		if (surfCaps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
		{
			preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
		}
		else 
		{
			preTransform = surfCaps.currentTransform;
		}

		vk::SwapchainCreateInfoKHR swapchainCI;
		swapchainCI.surface = surface;
		swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
		swapchainCI.imageFormat = colorFormat;
		swapchainCI.imageColorSpace = colorSpace;
		swapchainCI.imageExtent = vk::Extent2D { swapchainExtent.width, swapchainExtent.height };
		swapchainCI.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
		swapchainCI.preTransform = preTransform;
		swapchainCI.imageArrayLayers = 1;
		swapchainCI.imageSharingMode = vk::SharingMode::eExclusive;
		swapchainCI.queueFamilyIndexCount = 0;
		swapchainCI.pQueueFamilyIndices = NULL;
		swapchainCI.presentMode = swapchainPresentMode;
		swapchainCI.oldSwapchain = oldSwapchain;
		swapchainCI.clipped = true;
		swapchainCI.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;

		swapChain = device.createSwapchainKHR(swapchainCI, nullptr);
		

		// If an existing sawp chain is re-created, destroy the old swap chain
		// This also cleans up all the presentable images
		if (oldSwapchain) 
		{ 
			for (uint32_t i = 0; i < imageCount; i++)
			{
				device.destroyImageView(buffers[i].view, nullptr);
			}
			device.destroySwapchainKHR(oldSwapchain, nullptr);
		}

		
		// Get the swap chain images
		images = device.getSwapchainImagesKHR(swapChain);
		imageCount = (uint32_t)images.size();

		// Get the swap chain buffers containing the image and imageview
		buffers.resize(imageCount);
		for (uint32_t i = 0; i < imageCount; i++)
		{
			vk::ImageViewCreateInfo colorAttachmentView;
			colorAttachmentView.format = colorFormat;
			colorAttachmentView.components = {
				vk::ComponentSwizzle::eR,
				vk::ComponentSwizzle::eG,
				vk::ComponentSwizzle::eB,
				vk::ComponentSwizzle::eA
			};
			colorAttachmentView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			colorAttachmentView.subresourceRange.baseMipLevel = 0;
			colorAttachmentView.subresourceRange.levelCount = 1;
			colorAttachmentView.subresourceRange.baseArrayLayer = 0;
			colorAttachmentView.subresourceRange.layerCount = 1;
			colorAttachmentView.viewType = vk::ImageViewType::e2D;

			buffers[i].image = images[i];

			// Transform images from initial (undefined) to present layout
			vkTools::setImageLayout(
				cmdBuffer, 
				buffers[i].image, 
				vk::ImageAspectFlagBits::eColor, 
				vk::ImageLayout::eUndefined, 
				vk::ImageLayout::ePresentSrcKHR);

			colorAttachmentView.image = buffers[i].image;

			buffers[i].view = device.createImageView(colorAttachmentView, nullptr);
			
		}
	}

	// Acquires the next image in the swap chain
	uint32_t acquireNextImage(vk::Semaphore presentCompleteSemaphore)
	{
		auto resultValue = device.acquireNextImageKHR(swapChain, UINT64_MAX, presentCompleteSemaphore, vk::Fence());
		vk::Result result = resultValue.result;
		if (result != vk::Result::eSuccess) {
			// TODO handle eSuboptimalKHR
			std::cerr << "Invalid acquire result: " << vk::to_string(result);
			throw std::error_code(result);
		}
		return resultValue.value;
	}

	// Present the current image to the queue
	vk::Result queuePresent(vk::Queue queue, uint32_t currentBuffer)
	{
		vk::PresentInfoKHR presentInfo;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain;
		presentInfo.pImageIndices = &currentBuffer;
		return queue.presentKHR(presentInfo);
	}

	// Present the current image to the queue
	vk::Result queuePresent(vk::Queue queue, uint32_t currentBuffer, vk::Semaphore waitSemaphore)
	{
		vk::PresentInfoKHR presentInfo;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain;
		presentInfo.pImageIndices = &currentBuffer;
		if (waitSemaphore)
		{
			presentInfo.pWaitSemaphores = &waitSemaphore;
			presentInfo.waitSemaphoreCount = 1;
		}
		return queue.presentKHR(presentInfo);
	}


	// Free all Vulkan resources used by the swap chain
	void cleanup()
	{
		for (uint32_t i = 0; i < imageCount; i++)
		{
			device.destroyImageView(buffers[i].view, nullptr);
		}
		device.destroySwapchainKHR(swapChain, nullptr);
		instance.destroySurfaceKHR(surface, nullptr);
	}

};
