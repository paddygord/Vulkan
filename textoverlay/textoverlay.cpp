/*
* Vulkan Example - Text overlay rendering on-top of an existing scene using a separate render pass
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "../external/stb/stb_font_consolas_24_latin1.inl"


// Vertex layout for this example
std::vector<vkMeshLoader::VertexLayout> vertexLayout =
{
	vkMeshLoader::VERTEX_LAYOUT_POSITION,
	vkMeshLoader::VERTEX_LAYOUT_NORMAL,
	vkMeshLoader::VERTEX_LAYOUT_UV,
	vkMeshLoader::VERTEX_LAYOUT_COLOR,
};


// Defines for the STB font used
// STB font files can be found at http://nothings.org/stb/font/
#define STB_FONT_NAME stb_font_consolas_24_latin1
#define STB_FONT_WIDTH STB_FONT_consolas_24_latin1_BITMAP_WIDTH
#define STB_FONT_HEIGHT STB_FONT_consolas_24_latin1_BITMAP_HEIGHT 
#define STB_FIRST_CHAR STB_FONT_consolas_24_latin1_FIRST_CHAR
#define STB_NUM_CHARS STB_FONT_consolas_24_latin1_NUM_CHARS

// Max. number of chars the text overlay buffer can hold
#define MAX_CHAR_COUNT 2048

// Mostly self-contained text overlay class
class TextOverlay
{
private:
	vk::PhysicalDevice physicalDevice;
	vk::Device device;
	vk::PhysicalDeviceMemoryProperties deviceMemoryProperties;
	vk::Queue queue;
	vk::Format colorFormat;
	vk::Format depthFormat;

	uint32_t *frameBufferWidth;
	uint32_t *frameBufferHeight;

	vk::Sampler sampler;
	vk::Image image;
	vk::ImageView view;
	vk::Buffer buffer;
	vk::DeviceMemory memory;
	vk::DeviceMemory imageMemory;
	vk::DescriptorPool descriptorPool;
	vk::DescriptorSetLayout descriptorSetLayout;
	vk::DescriptorSet descriptorSet;
	vk::PipelineLayout pipelineLayout;
	vk::PipelineCache pipelineCache;
	vk::Pipeline pipeline;
	vk::RenderPass renderPass;
	vk::CommandPool commandPool;
	std::vector<vk::CommandBuffer> cmdBuffers;
	std::vector<vk::Framebuffer*> frameBuffers;
	std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

	// Pointer to mapped vertex buffer
	glm::vec4 *mapped = nullptr;

	stb_fontchar stbFontData[STB_NUM_CHARS];
	uint32_t numLetters;

	// Try to find appropriate memory type for a memory allocation
	vk::Bool32 getMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties, uint32_t *typeIndex)
	{
		for (int i = 0; i < 32; i++) {
			if ((typeBits & 1) == 1) {
				if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				{
					*typeIndex = i;
					return true;
				}
			}
			typeBits >>= 1;
		}
		return false;
	}
public:

	enum TextAlign { alignLeft, alignCenter, alignRight };

	bool visible = true;

	TextOverlay(
		vk::PhysicalDevice physicalDevice,
		vk::Device device,
		vk::Queue queue,
		std::vector<vk::Framebuffer> &framebuffers,
		vk::Format colorformat,
		vk::Format depthformat,
		uint32_t *framebufferwidth,
		uint32_t *framebufferheight,
		std::vector<vk::PipelineShaderStageCreateInfo> shaderstages)
	{
		this->physicalDevice = physicalDevice;
		this->device = device;
		this->queue = queue;
		this->colorFormat = colorformat;
		this->depthFormat = depthformat;

		this->frameBuffers.resize(framebuffers.size());
		for (uint32_t i = 0; i < framebuffers.size(); i++)
		{
			this->frameBuffers[i] = &framebuffers[i];
		}

		this->shaderStages = shaderstages;

		this->frameBufferWidth = framebufferwidth;
		this->frameBufferHeight = framebufferheight;

		deviceMemoryProperties = physicalDevice.getMemoryProperties();
		cmdBuffers.resize(framebuffers.size());
		prepareResources();
		prepareRenderPass();
		preparePipeline();
	}

	~TextOverlay()
	{
		// Free up all Vulkan resources requested by the text overlay
		device.destroySampler(sampler, nullptr);
		device.destroyImage(image, nullptr);
		device.destroyImageView(view, nullptr);
		device.destroyBuffer(buffer, nullptr);
		device.freeMemory(memory, nullptr);
		device.freeMemory(imageMemory, nullptr);
		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);
		device.destroyDescriptorPool(descriptorPool, nullptr);
		device.destroyPipelineLayout(pipelineLayout, nullptr);
		device.destroyPipelineCache(pipelineCache, nullptr);
		device.destroyPipeline(pipeline, nullptr);
		device.destroyRenderPass(renderPass, nullptr);
		device.freeCommandBuffers(commandPool, cmdBuffers.size(), cmdBuffers.data());
		device.destroyCommandPool(commandPool, nullptr);
	}

	// Prepare all vulkan resources required to render the font
	// The text overlay uses separate resources for descriptors (pool, sets, layouts), pipelines and command buffers
	void prepareResources()
	{
		static unsigned char font24pixels[STB_FONT_HEIGHT][STB_FONT_WIDTH];
		STB_FONT_NAME(stbFontData, font24pixels, STB_FONT_HEIGHT);

		// Command buffer

		// Pool
		vk::CommandPoolCreateInfo cmdPoolInfo;
		cmdPoolInfo.queueFamilyIndex = 0; // todo : pass from example base / swap chain
		cmdPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
		commandPool = device.createCommandPool(cmdPoolInfo, nullptr);

		vk::CommandBufferAllocateInfo cmdBufAllocateInfo =
			vkTools::initializers::commandBufferAllocateInfo(commandPool, vk::CommandBufferLevel::ePrimary, (uint32_t)cmdBuffers.size());

		cmdBuffers = device.allocateCommandBuffers(cmdBufAllocateInfo);

		// Vertex buffer
		vk::DeviceSize bufferSize = MAX_CHAR_COUNT * sizeof(glm::vec4);

		vk::BufferCreateInfo bufferInfo = vkTools::initializers::bufferCreateInfo(vk::BufferUsageFlagBits::eVertexBuffer, bufferSize);
		buffer = device.createBuffer(bufferInfo, nullptr);

		vk::MemoryRequirements memReqs;
		vk::MemoryAllocateInfo allocInfo;

		memReqs = device.getBufferMemoryRequirements(buffer);
		allocInfo.allocationSize = memReqs.size;
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible, &allocInfo.memoryTypeIndex);

		memory = device.allocateMemory(allocInfo, nullptr);
		device.bindBufferMemory(buffer, memory, 0);

		// Font texture
		vk::ImageCreateInfo imageInfo;
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = vk::Format::eR8Unorm;
		imageInfo.extent.width = STB_FONT_WIDTH;
		imageInfo.extent.height = STB_FONT_HEIGHT;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;
		imageInfo.initialLayout = vk::ImageLayout::ePreinitialized;

		image = device.createImage(imageInfo, nullptr);

		memReqs = device.getImageMemoryRequirements(image);
		allocInfo.allocationSize = memReqs.size;
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &allocInfo.memoryTypeIndex);

		imageMemory = device.allocateMemory(allocInfo, nullptr);
		device.bindImageMemory(image, imageMemory, 0);

		// Staging

		struct {
			vk::DeviceMemory memory;
			vk::Buffer buffer;
		} stagingBuffer;

		vk::BufferCreateInfo bufferCreateInfo;
		bufferCreateInfo.size = allocInfo.allocationSize;
		bufferCreateInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

		stagingBuffer.buffer = device.createBuffer(bufferCreateInfo, nullptr);

		// Get memory requirements for the staging buffer (alignment, memory type bits)
		memReqs = device.getBufferMemoryRequirements(stagingBuffer.buffer);

		allocInfo.allocationSize = memReqs.size;
		// Get memory type index for a host visible buffer
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible, &allocInfo.memoryTypeIndex);

		stagingBuffer.memory = device.allocateMemory(allocInfo, nullptr);
		device.bindBufferMemory(stagingBuffer.buffer, stagingBuffer.memory, 0);

		void *data = device.mapMemory(stagingBuffer.memory, 0, allocInfo.allocationSize, vk::MemoryMapFlags());
		memcpy(data, &font24pixels[0][0], STB_FONT_WIDTH * STB_FONT_HEIGHT);
		device.unmapMemory(stagingBuffer.memory);

		// Copy to image

		vk::CommandBuffer copyCmd;
		cmdBufAllocateInfo.commandBufferCount = 1;
		copyCmd = device.allocateCommandBuffers(cmdBufAllocateInfo)[0];
	
		vk::CommandBufferBeginInfo cmdBufInfo;
		copyCmd.begin(cmdBufInfo);

		// Prepare for transfer
		vkTools::setImageLayout(
			copyCmd,
			image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::ePreinitialized,
			vk::ImageLayout::eTransferDstOptimal);

		vk::BufferImageCopy bufferCopyRegion;
		bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = STB_FONT_WIDTH;
		bufferCopyRegion.imageExtent.height = STB_FONT_HEIGHT;
		bufferCopyRegion.imageExtent.depth = 1;

		copyCmd.copyBufferToImage(stagingBuffer.buffer, image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegion);

		// Prepare for shader read
		vkTools::setImageLayout(
			copyCmd,
			image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal);

		copyCmd.end();

		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &copyCmd;

		queue.submit(submitInfo, VK_NULL_HANDLE);
		queue.waitIdle();

		device.freeCommandBuffers(commandPool, copyCmd);
		device.freeMemory(stagingBuffer.memory, nullptr);
		device.destroyBuffer(stagingBuffer.buffer, nullptr);


		vk::ImageViewCreateInfo imageViewInfo;
		imageViewInfo.image = image;
		imageViewInfo.viewType = vk::ImageViewType::e2D;
		imageViewInfo.format = imageInfo.format;
		imageViewInfo.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB,	vk::ComponentSwizzle::eA };
		imageViewInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

		view = device.createImageView(imageViewInfo, nullptr);

		// Sampler
		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.compareOp = vk::CompareOp::eNever;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 1.0f;
		samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		sampler = device.createSampler(samplerInfo, nullptr);

		// Descriptor
		// Font uses a separate descriptor pool
		std::array<vk::DescriptorPoolSize, 1> poolSizes;
		poolSizes[0] = vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1);

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 1);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);

		// Descriptor set layout
		std::array<vk::DescriptorSetLayoutBinding, 1> setLayoutBindings;
		setLayoutBindings[0] = vkTools::initializers::descriptorSetLayoutBinding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0);

		vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorSetLayoutInfo, nullptr);

		// Pipeline layout
		vk::PipelineLayoutCreateInfo pipelineLayoutInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayout = device.createPipelineLayout(pipelineLayoutInfo, nullptr);

		// Descriptor set
		vk::DescriptorSetAllocateInfo descriptorSetAllocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSet = device.allocateDescriptorSets(descriptorSetAllocInfo)[0];

		vk::DescriptorImageInfo texDescriptor =
			vkTools::initializers::descriptorImageInfo(sampler, view, vk::ImageLayout::eGeneral);

		std::array<vk::WriteDescriptorSet, 1> writeDescriptorSets;
		writeDescriptorSets[0] = vkTools::initializers::writeDescriptorSet(descriptorSet, vk::DescriptorType::eCombinedImageSampler, 0, &texDescriptor);
		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Pipeline cache
		vk::PipelineCacheCreateInfo pipelineCacheCreateInfo;
		pipelineCache = device.createPipelineCache(pipelineCacheCreateInfo, nullptr);
	}

	// Prepare a separate pipeline for the font rendering decoupled from the main application
	void preparePipeline()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleStrip, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise);

		// Enable blending
		vk::PipelineColorBlendAttachmentState blendAttachmentState;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

		vk::PipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		vk::PipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);

		vk::PipelineViewportStateCreateInfo viewportState =
			vkTools::initializers::pipelineViewportStateCreateInfo(1, 1);

		vk::PipelineMultisampleStateCreateInfo multisampleState =
			vkTools::initializers::pipelineMultisampleStateCreateInfo(vk::SampleCountFlagBits::e1);

		std::vector<vk::DynamicState> dynamicStateEnables = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		vk::PipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

		std::array<vk::VertexInputBindingDescription, 2> vertexBindings = {};
		vertexBindings[0] = vkTools::initializers::vertexInputBindingDescription(0, sizeof(glm::vec4), vk::VertexInputRate::eVertex);
		vertexBindings[1] = vkTools::initializers::vertexInputBindingDescription(1, sizeof(glm::vec4), vk::VertexInputRate::eVertex);

		std::array<vk::VertexInputAttributeDescription, 2> vertexAttribs = {};
		// Position
		vertexAttribs[0] = vkTools::initializers::vertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, 0);
		// UV
		vertexAttribs[1] = vkTools::initializers::vertexInputAttributeDescription(1, 1, vk::Format::eR32G32Sfloat, sizeof(glm::vec2));

		vk::PipelineVertexInputStateCreateInfo inputState = vk::PipelineVertexInputStateCreateInfo();
		inputState.vertexBindingDescriptionCount = vertexBindings.size();
		inputState.pVertexBindingDescriptions = vertexBindings.data();
		inputState.vertexAttributeDescriptionCount = vertexAttribs.size();
		inputState.pVertexAttributeDescriptions = vertexAttribs.data();

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(pipelineLayout, renderPass);

		pipelineCreateInfo.pVertexInputState = &inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		pipeline = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	// Prepare a separate render pass for rendering the text as an overlay
	void prepareRenderPass()
	{
		vk::AttachmentDescription attachments[2] = {};

		// Color attachment
		attachments[0].format = colorFormat;
		attachments[0].samples = vk::SampleCountFlagBits::e1;
		// Don't clear the framebuffer (like the renderpass from the example does)
		attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
		attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
		attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attachments[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		// Depth attachment
		attachments[1].format = depthFormat;
		attachments[1].samples = vk::SampleCountFlagBits::e1;
		attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
		attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
		attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attachments[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentReference colorReference;
		colorReference.attachment = 0;
		colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference depthReference;
		depthReference.attachment = 1;
		depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		subpass.pDepthStencilAttachment = &depthReference;

		vk::RenderPassCreateInfo renderPassInfo;
		renderPassInfo.attachmentCount = 2;
		renderPassInfo.pAttachments = attachments;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		renderPass = device.createRenderPass(renderPassInfo, nullptr);
	}

	// Map buffer 
	void beginTextUpdate()
	{
		mapped = (glm::vec4*)device.mapMemory(memory, 0, VK_WHOLE_SIZE, vk::MemoryMapFlags());
		numLetters = 0;
	}

	// Add text to the current buffer
	// todo : drop shadow? color attribute?
	void addText(std::string text, float x, float y, TextAlign align)
	{
		assert(mapped != nullptr);

		const float charW = 1.5f / *frameBufferWidth;
		const float charH = 1.5f / *frameBufferHeight;

		float fbW = (float)*frameBufferWidth;
		float fbH = (float)*frameBufferHeight;
		x = (x / fbW * 2.0f) - 1.0f;
		y = (y / fbH * 2.0f) - 1.0f;

		// Calculate text width
		float textWidth = 0;
		for (auto letter : text)
		{
			stb_fontchar *charData = &stbFontData[(uint32_t)letter - STB_FIRST_CHAR];
			textWidth += charData->advance * charW;
		}

		switch (align)
		{
			case alignRight:
				x -= textWidth;
				break;
			case alignCenter:
				x -= textWidth / 2.0f;
				break;
		}

		// Generate a uv mapped quad per char in the new text
		for (auto letter : text)
		{
			stb_fontchar *charData = &stbFontData[(uint32_t)letter - STB_FIRST_CHAR];

			mapped->x = (x + (float)charData->x0 * charW);
			mapped->y = (y + (float)charData->y0 * charH);
			mapped->z = charData->s0;
			mapped->w = charData->t0;
			mapped++;

			mapped->x = (x + (float)charData->x1 * charW);
			mapped->y = (y + (float)charData->y0 * charH);
			mapped->z = charData->s1;
			mapped->w = charData->t0;
			mapped++;

			mapped->x = (x + (float)charData->x0 * charW);
			mapped->y = (y + (float)charData->y1 * charH);
			mapped->z = charData->s0;
			mapped->w = charData->t1;
			mapped++;

			mapped->x = (x + (float)charData->x1 * charW);
			mapped->y = (y + (float)charData->y1 * charH);
			mapped->z = charData->s1;
			mapped->w = charData->t1;
			mapped++;

			x += charData->advance * charW;

			numLetters++;
		}
	}

	// Unmap buffer and update command buffers
	void endTextUpdate()
	{
		device.unmapMemory(memory);
		mapped = nullptr;
		updateCommandBuffers();
	}

	// Needs to be called by the application
	void updateCommandBuffers()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = *frameBufferWidth;
		renderPassBeginInfo.renderArea.extent.height = *frameBufferHeight;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < cmdBuffers.size(); ++i)
		{
			renderPassBeginInfo.framebuffer = *frameBuffers[i];

			cmdBuffers[i].begin(cmdBufInfo);

			cmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkTools::initializers::viewport((float)*frameBufferWidth, (float)*frameBufferHeight, 0.0f, 1.0f);
			cmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(*frameBufferWidth, *frameBufferHeight, 0, 0);
			cmdBuffers[i].setScissor(0, scissor);

			cmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
			cmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);

			vk::DeviceSize offsets = 0;
			cmdBuffers[i].bindVertexBuffers(0, buffer, offsets);
			cmdBuffers[i].bindVertexBuffers(1, buffer, offsets);
			for (uint32_t j = 0; j < numLetters; j++)
			{
				cmdBuffers[i].draw(4, 1, j * 4, 0);
			}


			cmdBuffers[i].endRenderPass();

			cmdBuffers[i].end();
		}
	}

	// Submit the text command buffers to a queue
	// Does a queue wait idle
	void submit(vk::Queue queue, uint32_t bufferindex)
	{
		if (!visible)
		{
			return;
		}

		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdBuffers[bufferindex];

		queue.submit(submitInfo, VK_NULL_HANDLE);
		queue.waitIdle();
	}

};

class VulkanExample : public VulkanExampleBase
{
public:
	TextOverlay *textOverlay = nullptr;

	struct {
		vkTools::VulkanTexture background;
		vkTools::VulkanTexture cube;
	} textures;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer cube;
	} meshes;

	struct {
		vkTools::UniformData vsScene;
	} uniformData;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	} uboVS;

	struct {
		vk::Pipeline solid;
		vk::Pipeline background;
	} pipelines;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSetLayout descriptorSetLayout;

	struct {
		vk::DescriptorSet background;
		vk::DescriptorSet cube;
	} descriptorSets;


	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -4.5f;
		zoomSpeed = 2.5f;
		rotation = { -25.0f, 0.0f, 0.0f };
		title = "Vulkan Example - Text overlay";
		// Disable text overlay of the example base class
		enableTextOverlay = false;
	}

	~VulkanExample()
	{
		device.destroyPipeline(pipelines.solid, nullptr);
		device.destroyPipeline(pipelines.background, nullptr);
		device.destroyPipelineLayout(pipelineLayout, nullptr);
		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.cube);
		textureLoader->destroyTexture(textures.background);
		textureLoader->destroyTexture(textures.cube);
		vkTools::destroyUniformData(device, &uniformData.vsScene);
		delete(textOverlay);
	}

	void buildCommandBuffers()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[3];

		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 3;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			drawCmdBuffers[i].begin(cmdBufInfo);

			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.background, nullptr);

			vk::DeviceSize offsets = 0;
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.cube.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.cube.indices.buf, 0, vk::IndexType::eUint32);

			// Background
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.background);
			// Vertices are generated by the vertex shader
			drawCmdBuffers[i].draw(4, 1, 0, 0);

			// Cube
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.cube, nullptr);
			drawCmdBuffers[i].drawIndexed(meshes.cube.indexCount, 1, 0, 0, 0);

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
		}

		queue.waitIdle();
	}

	// Update the text buffer displayed by the text overlay
	void updateTextOverlay(void)
	{
		textOverlay->beginTextUpdate();

		textOverlay->addText(title, 5.0f, 5.0f, TextOverlay::alignLeft);

		std::stringstream ss;
		ss << std::fixed << std::setprecision(2) << (frameTimer * 1000.0f) << "ms (" << lastFPS << " fps)";
		textOverlay->addText(ss.str(), 5.0f, 25.0f, TextOverlay::alignLeft);

		textOverlay->addText(deviceProperties.deviceName, 5.0f, 45.0f, TextOverlay::alignLeft);
		
		textOverlay->addText("Press \"space\" to toggle text overlay", 5.0f, height - 20.0f, TextOverlay::alignLeft);

		// Display projected cube vertices
		for (int32_t x = -1; x <= 1; x += 2)
		{
			for (int32_t y = -1; y <= 1; y += 2)
			{
				for (int32_t z = -1; z <= 1; z += 2)
				{
					std::stringstream vpos;
					vpos << std::showpos << x << "/" << y << "/" << z;
					glm::vec3 projected = glm::project(glm::vec3((float)x, (float)y, (float)z), uboVS.model, uboVS.projection, glm::vec4(0, 0, (float)width, (float)height));
					textOverlay->addText(vpos.str(), projected.x, projected.y + (y > -1 ? 5.0f : -20.0f), TextOverlay::alignCenter);
				}
			}
		}

		// Display current model view matrix
		textOverlay->addText("model view matrix", width, 5.0f, TextOverlay::alignRight);

		for (uint32_t i = 0; i < 4; i++)
		{
			ss.str("");
			ss << std::fixed << std::setprecision(2) << std::showpos;
			ss << uboVS.model[0][i] << " " << uboVS.model[1][i] << " " << uboVS.model[2][i] << " " << uboVS.model[3][i];
			textOverlay->addText(ss.str(), width, 25.0f + (float)i * 20.0f, TextOverlay::alignRight);
		}

		glm::vec3 projected = glm::project(glm::vec3(0.0f), uboVS.model, uboVS.projection, glm::vec4(0, 0, (float)width, (float)height));
		textOverlay->addText("Uniform cube", projected.x, projected.y, TextOverlay::alignCenter);

#if defined(__ANDROID__)
		// toto
#else
		textOverlay->addText("Hold middle mouse button and drag to move", 5.0f, height - 40.0f, TextOverlay::alignLeft);
#endif
		textOverlay->endTextUpdate();
	}

	void draw()
	{
		// Get next image in the swap chain (back/front buffer)
		prepareFrame();
		// Command buffer to be sumitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		queue.submit(submitInfo, VK_NULL_HANDLE);

		// Submit text overlay to queue
		textOverlay->submit(queue, currentBuffer);

		submitFrame();
	}

	void loadTextures()
	{
		textureLoader->loadTexture(getAssetPath() + "textures/skysphere_bc3.ktx", vk::Format::eBc3UnormBlock, &textures.background);
		textureLoader->loadTexture(getAssetPath() + "textures/round_window_bc3.ktx", vk::Format::eBc3UnormBlock, &textures.cube);
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/cube.dae", &meshes.cube, vertexLayout, 1.0f);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vkMeshLoader::vertexSize(vertexLayout), vk::VertexInputRate::eVertex);

		// Attribute descriptions
		vertices.attributeDescriptions.resize(4);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);
		// Location 1 : Normal
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3);
		// Location 2 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 6);
		// Location 3 : Color
		vertices.attributeDescriptions[3] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32B32Sfloat, sizeof(float) * 8);

		vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 2),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2),
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(
			vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eVertex,
				0),
			// Binding 1 : Fragment shader combined sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				1),
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);

		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
	}

	void setupDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		// Background
		descriptorSets.background = device.allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo texDescriptor =
			vkTools::initializers::descriptorImageInfo(textures.background.sampler, textures.background.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets;

		// Binding 0 : Vertex shader uniform buffer
		writeDescriptorSets.push_back(
			vkTools::initializers::writeDescriptorSet(descriptorSets.background, vk::DescriptorType::eUniformBuffer, 0, &uniformData.vsScene.descriptor));

		// Binding 1 : Color map 
		writeDescriptorSets.push_back(
			vkTools::initializers::writeDescriptorSet(descriptorSets.background, vk::DescriptorType::eCombinedImageSampler, 1, &texDescriptor));

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Cube
		descriptorSets.cube = device.allocateDescriptorSets(allocInfo)[0];
		texDescriptor.sampler = textures.cube.sampler;
		texDescriptor.imageView = textures.cube.view;
		writeDescriptorSets[0].dstSet = descriptorSets.cube;
		writeDescriptorSets[1].dstSet = descriptorSets.cube;
		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise);

		vk::PipelineColorBlendAttachmentState blendAttachmentState =
			vkTools::initializers::pipelineColorBlendAttachmentState();

		vk::PipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		vk::PipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);

		vk::PipelineViewportStateCreateInfo viewportState =
			vkTools::initializers::pipelineViewportStateCreateInfo(1, 1);

		vk::PipelineMultisampleStateCreateInfo multisampleState =
			vkTools::initializers::pipelineMultisampleStateCreateInfo(vk::SampleCountFlagBits::e1);

		std::vector<vk::DynamicState> dynamicStateEnables = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};
		vk::PipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

		// Wire frame rendering pipeline
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/textoverlay/mesh.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/textoverlay/mesh.frag.spv", vk::ShaderStageFlagBits::eFragment);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(pipelineLayout, renderPass);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		pipelines.solid = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Background rendering pipeline
		depthStencilState.depthTestEnable = VK_FALSE;
		depthStencilState.depthWriteEnable = VK_FALSE;

		rasterizationState.polygonMode = vk::PolygonMode::eFill;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/textoverlay/background.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/textoverlay/background.frag.spv", vk::ShaderStageFlagBits::eFragment);

		pipelines.background = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVS),
			&uboVS,
			uniformData.vsScene.buffer,
			uniformData.vsScene.memory,
			uniformData.vsScene.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// Vertex shader
		uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);

		glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));

		uboVS.model = viewMatrix * glm::translate(glm::mat4(), cameraPos);
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		void *pData = device.mapMemory(uniformData.vsScene.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());
		memcpy(pData, &uboVS, sizeof(uboVS));
		device.unmapMemory(uniformData.vsScene.memory);
	}

	void prepareTextOverlay()
	{
		// Load the text rendering shaders
		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
		shaderStages.push_back(loadShader(getAssetPath() + "shaders/textoverlay/text.vert.spv", vk::ShaderStageFlagBits::eVertex));
		shaderStages.push_back(loadShader(getAssetPath() + "shaders/textoverlay/text.frag.spv", vk::ShaderStageFlagBits::eFragment));

		textOverlay = new TextOverlay(
			physicalDevice,
			device,
			queue,
			frameBuffers,
			colorformat,
			depthFormat,
			&width,
			&height,
			shaderStages
			);
		updateTextOverlay();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadTextures();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		prepareTextOverlay();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		if (frameCounter == 0)
		{
			vkDeviceWaitIdle(device);
			updateTextOverlay();
		}
	}

	virtual void viewChanged()
	{
		vkDeviceWaitIdle(device);
		updateUniformBuffers();
		updateTextOverlay();
	}

	virtual void windowResized()
	{
		updateTextOverlay();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case 0x6B:
		case 0x20:
			textOverlay->visible = !textOverlay->visible;
		}
	}
};

VulkanExample *vulkanExample;

#if defined(_WIN32)
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleMessages(hWnd, uMsg, wParam, lParam);
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
#elif defined(__linux__) && !defined(__ANDROID__)
static void handleEvent(const xcb_generic_event_t *event)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleEvent(event);
	}
}
#endif

// Main entry point
#if defined(_WIN32)
// Windows entry point
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
#elif defined(__ANDROID__)
// Android entry point
void android_main(android_app* state)
#elif defined(__linux__)
// Linux entry point
int main(const int argc, const char *argv[])
#endif
{
#if defined(__ANDROID__)
	// Removing this may cause the compiler to omit the main entry point 
	// which would make the application crash at start
	app_dummy();
#endif
	vulkanExample = new VulkanExample();
#if defined(_WIN32)
	vulkanExample->setupWindow(hInstance, WndProc);
#elif defined(__ANDROID__)
	// Attach vulkan example to global android application state
	state->userData = vulkanExample;
	state->onAppCmd = VulkanExample::handleAppCommand;
	state->onInputEvent = VulkanExample::handleAppInput;
	vulkanExample->androidApp = state;
#elif defined(__linux__)
	vulkanExample->setupWindow();
#endif
#if !defined(__ANDROID__)
	vulkanExample->initSwapchain();
	vulkanExample->prepare();
#endif
	vulkanExample->renderLoop();
	delete(vulkanExample);
#if !defined(__ANDROID__)
	return 0;
#endif
}
