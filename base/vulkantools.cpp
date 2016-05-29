/*
* Assorted commonly used Vulkan helper functions
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkantools.h"

namespace vkTools
{

	vk::Bool32 checkGlobalExtensionPresent(const char* extensionName)
	{
		uint32_t extensionCount = 0;
		std::vector<vk::ExtensionProperties> extensions = vk::enumerateInstanceExtensionProperties();
		for (auto& ext : extensions)
		{
			if (!strcmp(extensionName, ext.extensionName))
			{
				return true;
			}
		}
		return false;
	}

	vk::Bool32 checkDeviceExtensionPresent(vk::PhysicalDevice physicalDevice, const char* extensionName)
	{
		uint32_t extensionCount = 0;
		std::vector<vk::ExtensionProperties> extensions = physicalDevice.enumerateDeviceExtensionProperties();
		for (auto& ext : extensions)
		{
			if (!strcmp(extensionName, ext.extensionName))
			{
				return true;
			}
		}
		return false;
	}

	vk::Format getSupportedDepthFormat(vk::PhysicalDevice physicalDevice)
	{
		// Since all depth formats may be optional, we need to find a suitable depth format to use
		// Start with the highest precision packed format
		std::vector<vk::Format> depthFormats = { 
			vk::Format::eD32SfloatS8Uint, 
			vk::Format::eD32Sfloat,
			vk::Format::eD24UnormS8Uint, 
			vk::Format::eD16UnormS8Uint, 
			vk::Format::eD16Unorm 
		};

		for (auto& format : depthFormats)
		{
			vk::FormatProperties formatProps;
			formatProps = physicalDevice.getFormatProperties(format);
			// Format must support depth stencil attachment for optimal tiling
			if (formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
			{
				return format;
			}
		}

		throw std::runtime_error("No supported depth format");
	}

	// Create an image memory barrier for changing the layout of
	// an image and put it into an active command buffer
	// See chapter 11.4 "Image Layout" for details

	void setImageLayout(
		vk::CommandBuffer cmdbuffer, 
		vk::Image image, 
		vk::ImageAspectFlags aspectMask, 
		vk::ImageLayout oldImageLayout, 
		vk::ImageLayout newImageLayout,
		vk::ImageSubresourceRange subresourceRange)
	{
		// Create an image barrier object
		vk::ImageMemoryBarrier imageMemoryBarrier;
		imageMemoryBarrier.oldLayout = oldImageLayout;
		imageMemoryBarrier.newLayout = newImageLayout;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;

		// Source layouts (old)

		// Undefined layout
		// Only allowed as initial layout!
		// Make sure any writes to the image have been finished
		if (oldImageLayout == vk::ImageLayout::ePreinitialized)
		{
			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite | vk::AccessFlagBits::eTransferWrite;
		}

		// Old layout is color attachment
		// Make sure any writes to the color buffer have been finished
		if (oldImageLayout == vk::ImageLayout::eColorAttachmentOptimal) 
		{
			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		}

		// Old layout is depth/stencil attachment
		// Make sure any writes to the depth/stencil buffer have been finished
		if (oldImageLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
		{
			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		}

		// Old layout is transfer source
		// Make sure any reads from the image have been finished
		if (oldImageLayout == vk::ImageLayout::eTransferSrcOptimal)
		{
			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		}

		// Old layout is shader read (sampler, input attachment)
		// Make sure any shader reads from the image have been finished
		if (oldImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
		}

		// Target layouts (new)

		// New layout is transfer destination (copy, blit)
		// Make sure any copyies to the image have been finished
		if (newImageLayout == vk::ImageLayout::eTransferDstOptimal)
		{
			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		}

		// New layout is transfer source (copy, blit)
		// Make sure any reads from and writes to the image have been finished
		if (newImageLayout == vk::ImageLayout::eTransferSrcOptimal)
		{
			imageMemoryBarrier.srcAccessMask = imageMemoryBarrier.srcAccessMask | vk::AccessFlagBits::eTransferRead;
			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
		}

		// New layout is color attachment
		// Make sure any writes to the color buffer hav been finished
		if (newImageLayout == vk::ImageLayout::eColorAttachmentOptimal)
		{
			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		}

		// New layout is depth attachment
		// Make sure any writes to depth/stencil buffer have been finished
		if (newImageLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) 
		{
			imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		}

		// New layout is shader read (sampler, input attachment)
		// Make sure any writes to the image have been finished
		if (newImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite | vk::AccessFlagBits::eTransferWrite;
			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		}

		// Put barrier on top
		vk::PipelineStageFlags srcStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
		vk::PipelineStageFlags destStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;

		// Put barrier inside setup command buffer
		cmdbuffer.pipelineBarrier(srcStageFlags, destStageFlags, vk::DependencyFlags(), nullptr, nullptr, imageMemoryBarrier);
	}

	// Fixed sub resource on first mip level and layer
	void setImageLayout(
		vk::CommandBuffer cmdbuffer,
		vk::Image image,
		vk::ImageAspectFlags aspectMask,
		vk::ImageLayout oldImageLayout,
		vk::ImageLayout newImageLayout)
	{
		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = aspectMask;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		setImageLayout(cmdbuffer, image, aspectMask, oldImageLayout, newImageLayout, subresourceRange);
	}

	void exitFatal(std::string message, std::string caption)
	{
#ifdef _WIN32
		MessageBox(NULL, message.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
#else
		// TODO : Linux
#endif
		std::cerr << message << "\n";
		exit(1);
	}

	std::string readTextFile(const char *fileName)
	{
		std::string fileContent;
		std::ifstream fileStream(fileName, std::ios::in);
		if (!fileStream.is_open()) {
			printf("File %s not found\n", fileName);
			return "";
		}
		std::string line = "";
		while (!fileStream.eof()) {
			getline(fileStream, line);
			fileContent.append(line + "\n");
		}
		fileStream.close();
		return fileContent;
	}

#if defined(__ANDROID__)
	// Android shaders are stored as assets in the apk
	// So they need to be loaded via the asset manager
	vk::ShaderModule loadShader(AAssetManager* assetManager, const char *fileName, vk::Device device, vk::ShaderStageFlagBits stage)
	{
		// Load shader from compressed asset
		AAsset* asset = AAssetManager_open(assetManager, fileName, AASSET_MODE_STREAMING);
		assert(asset);
		size_t size = AAsset_getLength(asset);
		assert(size > 0);

		char *shaderCode = new char[size];
		AAsset_read(asset, shaderCode, size);
		AAsset_close(asset);

		vk::ShaderModule shaderModule;
		vk::ShaderModuleCreateInfo moduleCreateInfo;
		moduleCreateInfo.codeSize = size;
		moduleCreateInfo.pCode = (uint32_t*)shaderCode;
		moduleCreateInfo.flags = 0;

		shaderModule = device.createShaderModule(moduleCreateInfo, NULL);

		delete[] shaderCode;

		return shaderModule;
	}
#else
	vk::ShaderModule loadShader(const char *fileName, vk::Device device, vk::ShaderStageFlagBits stage) 
	{
		size_t size;

		FILE *fp = fopen(fileName, "rb");
		assert(fp);

		fseek(fp, 0L, SEEK_END);
		size = ftell(fp);

		fseek(fp, 0L, SEEK_SET);

		//shaderCode = malloc(size);
		char *shaderCode = new char[size];
		size_t retval = fread(shaderCode, size, 1, fp);
		assert(retval == 1);
		assert(size > 0);

		fclose(fp);

		vk::ShaderModule shaderModule;
		vk::ShaderModuleCreateInfo moduleCreateInfo;
		moduleCreateInfo.codeSize = size;
		moduleCreateInfo.pCode = (uint32_t*)shaderCode;

		shaderModule = device.createShaderModule(moduleCreateInfo, NULL);

		delete[] shaderCode;

		return shaderModule;
	}
#endif

	vk::ShaderModule loadShaderGLSL(const char *fileName, vk::Device device, vk::ShaderStageFlagBits stage)
	{
		std::string shaderSrc = readTextFile(fileName);
		const char *shaderCode = shaderSrc.c_str();
		size_t size = strlen(shaderCode);
		assert(size > 0);

		vk::ShaderModule shaderModule;
		vk::ShaderModuleCreateInfo moduleCreateInfo;
		moduleCreateInfo.codeSize = 3 * sizeof(uint32_t) + size + 1;
		moduleCreateInfo.pCode = (uint32_t*)malloc(moduleCreateInfo.codeSize);

		// Magic SPV number
		((uint32_t *)moduleCreateInfo.pCode)[0] = 0x07230203; 
		((uint32_t *)moduleCreateInfo.pCode)[1] = 0;
		((uint32_t *)moduleCreateInfo.pCode)[2] = (uint32_t)stage;
		memcpy(((uint32_t *)moduleCreateInfo.pCode + 3), shaderCode, size + 1);

		shaderModule = device.createShaderModule(moduleCreateInfo, NULL);

		return shaderModule;
	}

	vk::ImageMemoryBarrier prePresentBarrier(vk::Image presentImage)
	{
		vk::ImageMemoryBarrier imageMemoryBarrier;
		imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		imageMemoryBarrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
		imageMemoryBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
		imageMemoryBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		imageMemoryBarrier.image = presentImage;
		return imageMemoryBarrier;
	}

	vk::ImageMemoryBarrier postPresentBarrier(vk::Image presentImage)
	{
		vk::ImageMemoryBarrier imageMemoryBarrier;
		imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		imageMemoryBarrier.oldLayout = vk::ImageLayout::ePresentSrcKHR;
		imageMemoryBarrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		imageMemoryBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		imageMemoryBarrier.image = presentImage;
		return imageMemoryBarrier;
	}

	void destroyUniformData(vk::Device device, vkTools::UniformData *uniformData)
	{
		if (uniformData->mapped != nullptr)
		{
			device.unmapMemory(uniformData->memory);
		}
		device.destroyBuffer(uniformData->buffer, nullptr);
		device.freeMemory(uniformData->memory, nullptr);
	}
}

vk::CommandBufferAllocateInfo vkTools::initializers::commandBufferAllocateInfo(vk::CommandPool commandPool, vk::CommandBufferLevel level, uint32_t bufferCount)
{
	vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
	commandBufferAllocateInfo.commandPool = commandPool;
	commandBufferAllocateInfo.level = level;
	commandBufferAllocateInfo.commandBufferCount = bufferCount;
	return commandBufferAllocateInfo;
}

vk::FenceCreateInfo vkTools::initializers::fenceCreateInfo(vk::FenceCreateFlags flags)
{
	vk::FenceCreateInfo fenceCreateInfo;
	fenceCreateInfo.flags = flags;
	return fenceCreateInfo;
}

vk::Viewport vkTools::initializers::viewport(
	float width, 
	float height, 
	float minDepth, 
	float maxDepth)
{
	vk::Viewport viewport;
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;
	return viewport;
}

vk::Rect2D vkTools::initializers::rect2D(
	int32_t width, 
	int32_t height, 
	int32_t offsetX, 
	int32_t offsetY)
{
	vk::Rect2D rect2D;
	rect2D.extent.width = width;
	rect2D.extent.height = height;
	rect2D.offset.x = offsetX;
	rect2D.offset.y = offsetY;
	return rect2D;
}

vk::BufferCreateInfo vkTools::initializers::bufferCreateInfo(
	vk::BufferUsageFlags usage, 
	vk::DeviceSize size)
{
	vk::BufferCreateInfo bufCreateInfo;
	bufCreateInfo.usage = usage;
	bufCreateInfo.size = size;
	return bufCreateInfo;
}

vk::DescriptorPoolCreateInfo vkTools::initializers::descriptorPoolCreateInfo(
	uint32_t poolSizeCount, 
	vk::DescriptorPoolSize* pPoolSizes, 
	uint32_t maxSets)
{
	vk::DescriptorPoolCreateInfo descriptorPoolInfo;
	descriptorPoolInfo.poolSizeCount = poolSizeCount;
	descriptorPoolInfo.pPoolSizes = pPoolSizes;
	descriptorPoolInfo.maxSets = maxSets;
	return descriptorPoolInfo;
}

vk::DescriptorPoolSize vkTools::initializers::descriptorPoolSize(
	vk::DescriptorType type, 
	uint32_t descriptorCount)
{
	vk::DescriptorPoolSize descriptorPoolSize;
	descriptorPoolSize.type = type;
	descriptorPoolSize.descriptorCount = descriptorCount;
	return descriptorPoolSize;
}

vk::DescriptorSetLayoutBinding vkTools::initializers::descriptorSetLayoutBinding(
	vk::DescriptorType type, 
	vk::ShaderStageFlags stageFlags, 
	uint32_t binding)
{
	vk::DescriptorSetLayoutBinding setLayoutBinding;
	setLayoutBinding.descriptorType = type;
	setLayoutBinding.stageFlags = stageFlags;
	setLayoutBinding.binding = binding;
	// Default value in all examples
	setLayoutBinding.descriptorCount = 1; 
	return setLayoutBinding;
}

vk::DescriptorSetLayoutCreateInfo vkTools::initializers::descriptorSetLayoutCreateInfo(
	const vk::DescriptorSetLayoutBinding* pBindings,
	uint32_t bindingCount)
{
	vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;
	descriptorSetLayoutCreateInfo.pBindings = pBindings;
	descriptorSetLayoutCreateInfo.bindingCount = bindingCount;
	return descriptorSetLayoutCreateInfo;
}

vk::PipelineLayoutCreateInfo vkTools::initializers::pipelineLayoutCreateInfo(
	const vk::DescriptorSetLayout* pSetLayouts,
	uint32_t setLayoutCount)
{
	vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	pipelineLayoutCreateInfo.setLayoutCount = setLayoutCount;
	pipelineLayoutCreateInfo.pSetLayouts = pSetLayouts;
	return pipelineLayoutCreateInfo;
}

vk::DescriptorSetAllocateInfo vkTools::initializers::descriptorSetAllocateInfo(
	vk::DescriptorPool descriptorPool,
	const vk::DescriptorSetLayout* pSetLayouts,
	uint32_t descriptorSetCount)
{
	vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo;
	descriptorSetAllocateInfo.descriptorPool = descriptorPool;
	descriptorSetAllocateInfo.pSetLayouts = pSetLayouts;
	descriptorSetAllocateInfo.descriptorSetCount = descriptorSetCount;
	return descriptorSetAllocateInfo;
}

vk::DescriptorImageInfo vkTools::initializers::descriptorImageInfo(vk::Sampler sampler, vk::ImageView imageView, vk::ImageLayout imageLayout)
{
	vk::DescriptorImageInfo descriptorImageInfo;
	descriptorImageInfo.sampler = sampler;
	descriptorImageInfo.imageView = imageView;
	descriptorImageInfo.imageLayout = imageLayout;
	return descriptorImageInfo;
}

vk::WriteDescriptorSet vkTools::initializers::writeDescriptorSet(
	vk::DescriptorSet dstSet, 
	vk::DescriptorType type, 
	uint32_t binding, 
	vk::DescriptorBufferInfo* bufferInfo)
{
	vk::WriteDescriptorSet writeDescriptorSet;
	writeDescriptorSet.dstSet = dstSet;
	writeDescriptorSet.descriptorType = type;
	writeDescriptorSet.dstBinding = binding;
	writeDescriptorSet.pBufferInfo = bufferInfo;
	// Default value in all examples
	writeDescriptorSet.descriptorCount = 1;
	return writeDescriptorSet;
}

vk::WriteDescriptorSet vkTools::initializers::writeDescriptorSet(
	vk::DescriptorSet dstSet, 
	vk::DescriptorType type, 
	uint32_t binding, 
	vk::DescriptorImageInfo * imageInfo)
{
	vk::WriteDescriptorSet writeDescriptorSet;
	writeDescriptorSet.dstSet = dstSet;
	writeDescriptorSet.descriptorType = type;
	writeDescriptorSet.dstBinding = binding;
	writeDescriptorSet.pImageInfo = imageInfo;
	// Default value in all examples
	writeDescriptorSet.descriptorCount = 1;
	return writeDescriptorSet;
}

vk::VertexInputBindingDescription vkTools::initializers::vertexInputBindingDescription(
	uint32_t binding, 
	uint32_t stride, 
	vk::VertexInputRate inputRate)
{
	vk::VertexInputBindingDescription vInputBindDescription;
	vInputBindDescription.binding = binding;
	vInputBindDescription.stride = stride;
	vInputBindDescription.inputRate = inputRate;
	return vInputBindDescription;
}

vk::VertexInputAttributeDescription vkTools::initializers::vertexInputAttributeDescription(
	uint32_t binding, 
	uint32_t location, 
	vk::Format format, 
	uint32_t offset)
{
	vk::VertexInputAttributeDescription vInputAttribDescription;
	vInputAttribDescription.location = location;
	vInputAttribDescription.binding = binding;
	vInputAttribDescription.format = format;
	vInputAttribDescription.offset = offset;
	return vInputAttribDescription;
}

vk::PipelineInputAssemblyStateCreateInfo vkTools::initializers::pipelineInputAssemblyStateCreateInfo(
	vk::PrimitiveTopology topology, 
	vk::PipelineInputAssemblyStateCreateFlags flags, 
	vk::Bool32 primitiveRestartEnable)
{
	vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo;
	pipelineInputAssemblyStateCreateInfo.topology = topology;
	pipelineInputAssemblyStateCreateInfo.flags = flags;
	pipelineInputAssemblyStateCreateInfo.primitiveRestartEnable = primitiveRestartEnable;
	return pipelineInputAssemblyStateCreateInfo;
}

vk::PipelineRasterizationStateCreateInfo vkTools::initializers::pipelineRasterizationStateCreateInfo(
	vk::PolygonMode polygonMode, 
	vk::CullModeFlags cullMode, 
	vk::FrontFace frontFace, 
	vk::PipelineRasterizationStateCreateFlags flags)
{
	vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo;
	pipelineRasterizationStateCreateInfo.polygonMode = polygonMode;
	pipelineRasterizationStateCreateInfo.cullMode = cullMode;
	pipelineRasterizationStateCreateInfo.frontFace = frontFace;
	pipelineRasterizationStateCreateInfo.flags = flags;
	pipelineRasterizationStateCreateInfo.depthClampEnable = VK_TRUE;
	pipelineRasterizationStateCreateInfo.lineWidth = 1.0f;
	return pipelineRasterizationStateCreateInfo;
}

vk::ColorComponentFlags vkTools::initializers::fullColorWriteMask() {
        return vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
}

vk::PipelineColorBlendAttachmentState vkTools::initializers::pipelineColorBlendAttachmentState(
	vk::ColorComponentFlags colorWriteMask,
	vk::Bool32 blendEnable)
{
	vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState;
	pipelineColorBlendAttachmentState.colorWriteMask = colorWriteMask;
	pipelineColorBlendAttachmentState.blendEnable = blendEnable;
	return pipelineColorBlendAttachmentState;
}

vk::PipelineColorBlendStateCreateInfo vkTools::initializers::pipelineColorBlendStateCreateInfo(
	uint32_t attachmentCount, 
	const vk::PipelineColorBlendAttachmentState * pAttachments)
{
	vk::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo;
	pipelineColorBlendStateCreateInfo.attachmentCount = attachmentCount;
	pipelineColorBlendStateCreateInfo.pAttachments = pAttachments;
	return pipelineColorBlendStateCreateInfo;
}

vk::PipelineDepthStencilStateCreateInfo vkTools::initializers::pipelineDepthStencilStateCreateInfo(
	vk::Bool32 depthTestEnable, 
	vk::Bool32 depthWriteEnable, 
	vk::CompareOp depthCompareOp)
{
	vk::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo;
	pipelineDepthStencilStateCreateInfo.depthTestEnable = depthTestEnable;
	pipelineDepthStencilStateCreateInfo.depthWriteEnable = depthWriteEnable;
	pipelineDepthStencilStateCreateInfo.depthCompareOp = depthCompareOp;
	pipelineDepthStencilStateCreateInfo.front = pipelineDepthStencilStateCreateInfo.back;
	pipelineDepthStencilStateCreateInfo.back.compareOp = vk::CompareOp::eAlways;
	return pipelineDepthStencilStateCreateInfo;
}

vk::PipelineViewportStateCreateInfo vkTools::initializers::pipelineViewportStateCreateInfo(
	uint32_t viewportCount, 
	uint32_t scissorCount, 
	vk::PipelineViewportStateCreateFlags flags)
{
	vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo;
	pipelineViewportStateCreateInfo.viewportCount = viewportCount;
	pipelineViewportStateCreateInfo.scissorCount = scissorCount;
	pipelineViewportStateCreateInfo.flags = flags;
	return pipelineViewportStateCreateInfo;
}

vk::PipelineMultisampleStateCreateInfo vkTools::initializers::pipelineMultisampleStateCreateInfo(
	vk::SampleCountFlagBits rasterizationSamples, 
	vk::PipelineMultisampleStateCreateFlags flags)
{
	vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo;
	pipelineMultisampleStateCreateInfo.rasterizationSamples = rasterizationSamples;
	return pipelineMultisampleStateCreateInfo;
}

vk::PipelineDynamicStateCreateInfo vkTools::initializers::pipelineDynamicStateCreateInfo(
	const vk::DynamicState * pDynamicStates, 
	uint32_t dynamicStateCount, 
	vk::PipelineDynamicStateCreateFlags flags)
{
	vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo;
	pipelineDynamicStateCreateInfo.pDynamicStates = pDynamicStates;
	pipelineDynamicStateCreateInfo.dynamicStateCount = dynamicStateCount;
	return pipelineDynamicStateCreateInfo;
}

vk::PipelineTessellationStateCreateInfo vkTools::initializers::pipelineTessellationStateCreateInfo(uint32_t patchControlPoints)
{
	vk::PipelineTessellationStateCreateInfo pipelineTessellationStateCreateInfo;
	pipelineTessellationStateCreateInfo.patchControlPoints = patchControlPoints;
	return pipelineTessellationStateCreateInfo;
}

vk::GraphicsPipelineCreateInfo vkTools::initializers::pipelineCreateInfo(
	vk::PipelineLayout layout, 
	vk::RenderPass renderPass, 
	vk::PipelineCreateFlags flags)
{
	vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
	pipelineCreateInfo.layout = layout;
	pipelineCreateInfo.renderPass = renderPass;
	pipelineCreateInfo.flags = flags;
	return pipelineCreateInfo;
}

vk::ComputePipelineCreateInfo vkTools::initializers::computePipelineCreateInfo(vk::PipelineLayout layout, vk::PipelineCreateFlags flags)
{
	vk::ComputePipelineCreateInfo computePipelineCreateInfo;
	computePipelineCreateInfo.layout = layout;
	computePipelineCreateInfo.flags = flags;
	return computePipelineCreateInfo;
}

vk::PushConstantRange vkTools::initializers::pushConstantRange(
	vk::ShaderStageFlags stageFlags,
	uint32_t size,
	uint32_t offset)
{
	vk::PushConstantRange pushConstantRange;
	pushConstantRange.stageFlags = stageFlags;
	pushConstantRange.offset = offset;
	pushConstantRange.size = size;
	return pushConstantRange;
}
