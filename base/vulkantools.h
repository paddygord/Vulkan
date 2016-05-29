/*
* Assorted commonly used Vulkan helper functions
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "vulkan/vulkan.h"
#include "vulkan/vk_cpp.hpp"

#include <math.h>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <fstream>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#elif defined(__ANDROID__)
#include "vulkanandroid.h"
#include <android/asset_manager.h>
#endif

// Custom define for better code readability
#define VK_FLAGS_NONE 0
// Default fence timeout in nanoseconds
#define DEFAULT_FENCE_TIMEOUT 100000000000

// Macro to check and display Vulkan return results
#define VK_CHECK_RESULT(f)																				\
{																										\
	vk::Result res = (f);																					\
	if (res != vk::Result::eSuccess)																				\
	{																									\
		std::cout << "Fatal : vk::Result is \"" << vkTools::errorString(res) << "\" in " << __FILE__ << " at line " << __LINE__ << std::endl; \
		assert(res == vk::Result::eSuccess);																		\
	}																									\
}																										\

namespace vkTools
{
	// Check if extension is globally available
	vk::Bool32 checkGlobalExtensionPresent(const char* extensionName);
	// Check if extension is present on the given device
	vk::Bool32 checkDeviceExtensionPresent(vk::PhysicalDevice physicalDevice, const char* extensionName);
	// Return string representation of a vulkan error string
	std::string errorString(vk::Result errorCode);
	// Asserts and outputs the error message if the result is not vk::Result::eSuccess
	vk::Result checkResult(vk::Result result);

	// Selected a suitable supported depth format starting with 32 bit down to 16 bit
	// Returns false if none of the depth formats in the list is supported by the device
	vk::Bool32 getSupportedDepthFormat(vk::PhysicalDevice physicalDevice, vk::Format *depthFormat);

	// Put an image memory barrier for setting an image layout on the sub resource into the given command buffer
	void setImageLayout(
		vk::CommandBuffer cmdbuffer,
		vk::Image image,
		vk::ImageAspectFlags aspectMask,
		vk::ImageLayout oldImageLayout,
		vk::ImageLayout newImageLayout,
		vk::ImageSubresourceRange subresourceRange);
	// Uses a fixed sub resource layout with first mip level and layer
	void setImageLayout(
		vk::CommandBuffer cmdbuffer, 
		vk::Image image, 
		vk::ImageAspectFlags aspectMask, 
		vk::ImageLayout oldImageLayout, 
		vk::ImageLayout newImageLayout);

	// Display error message and exit on fatal error
	void exitFatal(std::string message, std::string caption);
	// Load a text file (e.g. GLGL shader) into a std::string
	std::string readTextFile(const char *fileName);
	// Load a binary file into a buffer (e.g. SPIR-V)
	char *readBinaryFile(const char *filename, size_t *psize);

	// Load a SPIR-V shader
#if defined(__ANDROID__)
	vk::ShaderModule loadShader(AAssetManager* assetManager, const char *fileName, vk::Device device, vk::ShaderStageFlagBits stage);
#else
	vk::ShaderModule loadShader(const char *fileName, vk::Device device, vk::ShaderStageFlagBits stage);
#endif

	// Load a GLSL shader
	// Note : Only for testing purposes, support for directly feeding GLSL shaders into Vulkan
	// may be dropped at some point	
	vk::ShaderModule loadShaderGLSL(const char *fileName, vk::Device device, vk::ShaderStageFlagBits stage);

	// Returns a pre-present image memory barrier
	// Transforms the image's layout from color attachment to present khr
	vk::ImageMemoryBarrier prePresentBarrier(vk::Image presentImage);

	// Returns a post-present image memory barrier
	// Transforms the image's layout back from present khr to color attachment
	vk::ImageMemoryBarrier postPresentBarrier(vk::Image presentImage);

	// Contains all vulkan objects
	// required for a uniform data object
	struct UniformData 
	{
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		vk::DescriptorBufferInfo descriptor;
		uint32_t allocSize;
		void* mapped = nullptr;
	};

	// Destroy (and free) Vulkan resources used by a uniform data structure
	void destroyUniformData(vk::Device device, vkTools::UniformData *uniformData);

	// Contains often used vulkan object initializers
	// Save lot of VK_STRUCTURE_TYPE assignments
	// Some initializers are parameterized for convenience
	namespace initializers
	{
		vk::MemoryAllocateInfo memoryAllocateInfo();

		vk::CommandBufferAllocateInfo commandBufferAllocateInfo(
			vk::CommandPool commandPool,
			vk::CommandBufferLevel level,
			uint32_t bufferCount);

		vk::CommandPoolCreateInfo commandPoolCreateInfo();
		vk::CommandBufferBeginInfo commandBufferBeginInfo();
		vk::CommandBufferInheritanceInfo commandBufferInheritanceInfo();

		vk::RenderPassBeginInfo renderPassBeginInfo();
		vk::RenderPassCreateInfo renderPassCreateInfo();

		vk::ImageMemoryBarrier imageMemoryBarrier();
		vk::BufferMemoryBarrier bufferMemoryBarrier();
		vk::MemoryBarrier memoryBarrier();

		vk::ImageCreateInfo imageCreateInfo();
		vk::SamplerCreateInfo samplerCreateInfo();
		vk::ImageViewCreateInfo imageViewCreateInfo();

		vk::FramebufferCreateInfo framebufferCreateInfo();

		vk::SemaphoreCreateInfo semaphoreCreateInfo();
		vk::FenceCreateInfo fenceCreateInfo(vk::FenceCreateFlags flags = vk::FenceCreateFlags());
		vk::EventCreateInfo eventCreateInfo();

		vk::SubmitInfo submitInfo();

		vk::Viewport viewport(
			float width, 
			float height, 
			float minDepth, 
			float maxDepth);

		vk::Rect2D rect2D(
			int32_t width,
			int32_t height,
			int32_t offsetX,
			int32_t offsetY);

		vk::BufferCreateInfo bufferCreateInfo();

		vk::BufferCreateInfo bufferCreateInfo(
			vk::BufferUsageFlags usage, 
			vk::DeviceSize size);

		vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo(
			uint32_t poolSizeCount,
			vk::DescriptorPoolSize* pPoolSizes,
			uint32_t maxSets);

		vk::DescriptorPoolSize descriptorPoolSize(
			vk::DescriptorType type,
			uint32_t descriptorCount);

		vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding(
			vk::DescriptorType type, 
			vk::ShaderStageFlags stageFlags, 
			uint32_t binding);

		vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo(
			const vk::DescriptorSetLayoutBinding* pBindings,
			uint32_t bindingCount);

		vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo(
			const vk::DescriptorSetLayout* pSetLayouts,
			uint32_t setLayoutCount	);

		vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(
			vk::DescriptorPool descriptorPool,
			const vk::DescriptorSetLayout* pSetLayouts,
			uint32_t descriptorSetCount);

		vk::DescriptorImageInfo descriptorImageInfo(
			vk::Sampler sampler,
			vk::ImageView imageView,
			vk::ImageLayout imageLayout);

		vk::WriteDescriptorSet writeDescriptorSet(
			vk::DescriptorSet dstSet, 
			vk::DescriptorType type, 
			uint32_t binding, 
			vk::DescriptorBufferInfo* bufferInfo);

		vk::WriteDescriptorSet writeDescriptorSet(
			vk::DescriptorSet dstSet, 
			vk::DescriptorType type, 
			uint32_t binding, 
			vk::DescriptorImageInfo* imageInfo);

		vk::VertexInputBindingDescription vertexInputBindingDescription(
			uint32_t binding, 
			uint32_t stride, 
			vk::VertexInputRate inputRate);

		vk::VertexInputAttributeDescription vertexInputAttributeDescription(
			uint32_t binding,
			uint32_t location,
			vk::Format format,
			uint32_t offset);

		vk::PipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo();

		vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo(
			vk::PrimitiveTopology topology,
			vk::PipelineInputAssemblyStateCreateFlags flags= vk::PipelineInputAssemblyStateCreateFlags(),
			vk::Bool32 primitiveRestartEnable = VK_FALSE);

		vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo(
			vk::PolygonMode polygonMode,
			vk::CullModeFlags cullMode,
			vk::FrontFace frontFace,
			vk::PipelineRasterizationStateCreateFlags flags = vk::PipelineRasterizationStateCreateFlags());

		vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState(
			vk::ColorComponentFlags colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
			vk::Bool32 blendEnable = VK_FALSE);

		vk::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo(
			uint32_t attachmentCount,
			const vk::PipelineColorBlendAttachmentState* pAttachments);

		vk::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo(
			vk::Bool32 depthTestEnable,
			vk::Bool32 depthWriteEnable,
			vk::CompareOp depthCompareOp);

		vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo(
			uint32_t viewportCount,
			uint32_t scissorCount,
			vk::PipelineViewportStateCreateFlags flags = vk::PipelineViewportStateCreateFlags());

		vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo(
			vk::SampleCountFlagBits rasterizationSamples,
			vk::PipelineMultisampleStateCreateFlags flags = vk::PipelineMultisampleStateCreateFlags());

		vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo(
			const vk::DynamicState *pDynamicStates,
			uint32_t dynamicStateCount,
			vk::PipelineDynamicStateCreateFlags flags = vk::PipelineDynamicStateCreateFlags());

		vk::PipelineTessellationStateCreateInfo pipelineTessellationStateCreateInfo(
			uint32_t patchControlPoints);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo(
			vk::PipelineLayout layout,
			vk::RenderPass renderPass,
			vk::PipelineCreateFlags flags = vk::PipelineCreateFlags());

		vk::ComputePipelineCreateInfo computePipelineCreateInfo(
			vk::PipelineLayout layout,
			vk::PipelineCreateFlags flags = vk::PipelineCreateFlags());

		vk::PushConstantRange pushConstantRange(
			vk::ShaderStageFlags stageFlags,
			uint32_t size,
			uint32_t offset);
	}

}
