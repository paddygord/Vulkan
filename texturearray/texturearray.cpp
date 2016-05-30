/*
* Vulkan Example - Texture arrays and instanced rendering
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"


// Vertex layout for this example
struct Vertex {
	float pos[3];
	float uv[2];
};

class VulkanExample : public VulkanExampleBase
{
public:
	// Number of array layers in texture array
	// Also used as instance count
	uint32_t layerCount;
	vkTools::VulkanTexture textureArray;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer quad;
	} meshes;

	struct {
		vkTools::UniformData vertexShader;
	} uniformData;

	struct UboInstanceData {
		// Model matrix
		glm::mat4 model;
		// Texture array index
		// Vec4 due to padding
		glm::vec4 arrayIndex;
	};

	struct {
		// Global matrices
		struct {
			glm::mat4 projection;
			glm::mat4 view;
		} matrices;
		// Seperate data for each instance
		UboInstanceData *instance;		
	} uboVS;

	struct {
		vk::Pipeline solid;
	} pipelines;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSet descriptorSet;
	vk::DescriptorSetLayout descriptorSetLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -15.0f;
		rotationSpeed = 0.25f;
		rotation = { -15.0f, 35.0f, 0.0f };
		title = "Vulkan Example - Texture arrays";
		srand(time(NULL));
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		// Clean up texture resources
		device.destroyImageView(textureArray.view);
		device.destroyImage(textureArray.image);
		device.destroySampler(textureArray.sampler);
		device.freeMemory(textureArray.deviceMemory);

		device.destroyPipeline(pipelines.solid);

		device.destroyPipelineLayout(pipelineLayout);
		device.destroyDescriptorSetLayout(descriptorSetLayout);

		vkMeshLoader::freeMeshBufferResources(device, &meshes.quad);

		vkTools::destroyUniformData(device, &uniformData.vertexShader);

		delete[] uboVS.instance;
	}

	void loadTextureArray(std::string filename, vk::Format format)
	{
#if defined(__ANDROID__)
		// Textures are stored inside the apk on Android (compressed)
		// So they need to be loaded via the asset manager
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		size_t size = AAsset_getLength(asset);
		assert(size > 0);

		void *textureData = malloc(size);
		AAsset_read(asset, textureData, size);
		AAsset_close(asset);

		gli::texture2DArray tex2DArray(gli::load((const char*)textureData, size));
#else
		gli::texture2DArray tex2DArray(gli::load(filename));
#endif

		assert(!tex2DArray.empty());

		textureArray.width = tex2DArray.dimensions().x;
		textureArray.height = tex2DArray.dimensions().y;
		layerCount = tex2DArray.layers();

		vk::MemoryAllocateInfo memAllocInfo;
		vk::MemoryRequirements memReqs;

		// Create a host-visible staging buffer that contains the raw image data
		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;

		vk::BufferCreateInfo bufferCreateInfo;
		bufferCreateInfo.size = tex2DArray.size();
		// This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

		stagingBuffer = device.createBuffer(bufferCreateInfo);

		// Get memory requirements for the staging buffer (alignment, memory type bits)
		memReqs = device.getBufferMemoryRequirements(stagingBuffer);

		memAllocInfo.allocationSize = memReqs.size;
		// Get memory type index for a host visible buffer
		memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);

		stagingMemory = device.allocateMemory(memAllocInfo);
		device.bindBufferMemory(stagingBuffer, stagingMemory, 0);

		// Copy texture data into staging buffer
		void *data = device.mapMemory(stagingMemory, 0, memReqs.size, vk::MemoryMapFlags());
		memcpy(data, tex2DArray.data(), tex2DArray.size());
		device.unmapMemory(stagingMemory);

		// Setup buffer copy regions for array layers
		std::vector<vk::BufferImageCopy> bufferCopyRegions;
		uint32_t offset = 0;

		// Check if all array layers have the same dimesions
		bool sameDims = true;
		for (uint32_t layer = 0; layer < layerCount; layer++)
		{
			if (tex2DArray[layer].dimensions().x != textureArray.width || tex2DArray[layer].dimensions().y != textureArray.height)
			{
				sameDims = false;
				break;
			}
		}

		// If all layers of the texture array have the same dimensions, we only need to do one copy
		if (sameDims)
		{
			vk::BufferImageCopy bufferCopyRegion;
			bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = layerCount;
			bufferCopyRegion.imageExtent.width = tex2DArray[0].dimensions().x;
			bufferCopyRegion.imageExtent.height = tex2DArray[0].dimensions().y;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = offset;

			bufferCopyRegions.push_back(bufferCopyRegion);
		}
		else
		{
			// If dimensions differ, copy layer by layer and pass offsets
			for (uint32_t layer = 0; layer < layerCount; layer++)
			{
				vk::BufferImageCopy bufferCopyRegion;
				bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				bufferCopyRegion.imageSubresource.mipLevel = 0;
				bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
				bufferCopyRegion.imageSubresource.layerCount = 1;
				bufferCopyRegion.imageExtent.width = tex2DArray[layer].dimensions().x;
				bufferCopyRegion.imageExtent.height = tex2DArray[layer].dimensions().y;
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = offset;

				bufferCopyRegions.push_back(bufferCopyRegion);

				offset += tex2DArray[layer].size();
			}
		}

		// Create optimal tiled target image
		vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
		imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled;
		imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
		imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
		imageCreateInfo.extent = vk::Extent3D { textureArray.width, textureArray.height, 1 };
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
		imageCreateInfo.arrayLayers = layerCount;

		textureArray.image = device.createImage(imageCreateInfo);

		memReqs = device.getImageMemoryRequirements(textureArray.image);

		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

		textureArray.deviceMemory = device.allocateMemory(memAllocInfo);
		device.bindImageMemory(textureArray.image, textureArray.deviceMemory, 0);

		vk::CommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

		// Image barrier for optimal image (target)
		// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = layerCount;

		vkTools::setImageLayout(
			copyCmd,
			textureArray.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::ePreinitialized,
			vk::ImageLayout::eTransferDstOptimal,
			subresourceRange);

		// Copy the cube map faces from the staging buffer to the optimal tiled image
		copyCmd.copyBufferToImage(stagingBuffer, textureArray.image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegions.size(), bufferCopyRegions.data());

		// Change texture image layout to shader read after all faces have been copied
		textureArray.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		vkTools::setImageLayout(
			copyCmd,
			textureArray.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			textureArray.imageLayout,
			subresourceRange);

		VulkanExampleBase::flushCommandBuffer(copyCmd, queue, true);

		// Create sampler
		vk::SamplerCreateInfo sampler;
		sampler.magFilter = vk::Filter::eLinear;
		sampler.minFilter = vk::Filter::eLinear;
		sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
		sampler.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 8;
		sampler.compareOp = vk::CompareOp::eNever;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		textureArray.sampler = device.createSampler(sampler);

		// Create image view
		vk::ImageViewCreateInfo view;
		view.viewType = vk::ImageViewType::e2DArray;
		view.format = format;
		view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		view.subresourceRange.layerCount = layerCount;
		view.image = textureArray.image;
		textureArray.view = device.createImageView(view);

		// Clean up staging resources
		device.freeMemory(stagingMemory);
		device.destroyBuffer(stagingBuffer);
	}

	void loadTextures()
	{
		loadTextureArray(
			getAssetPath() + "textures/texturearray_bc3.ktx", 
			vk::Format::eBc3UnormBlock);
	}

	void buildCommandBuffers()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
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

			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);

			vk::DeviceSize offsets = 0;
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.quad.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);

			drawCmdBuffers[i].drawIndexed(meshes.quad.indexCount, layerCount, 0, 0, 0);

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
		}
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

		submitFrame();
	}

	// Setup vertices for a single uv-mapped quad
	void generateQuad()
	{
#define dim 2.5f
		std::vector<Vertex> vertexBuffer =
		{
			{ {  dim,  dim, 0.0f }, { 1.0f, 1.0f } },
			{ { -dim,  dim, 0.0f }, { 0.0f, 1.0f } },
			{ { -dim, -dim, 0.0f }, { 0.0f, 0.0f } },
			{ {  dim, -dim, 0.0f }, { 1.0f, 0.0f } }
		};
#undef dim
		createBuffer(vk::BufferUsageFlagBits::eVertexBuffer,
			vertexBuffer.size() * sizeof(Vertex),
			vertexBuffer.data(),
			meshes.quad.vertices.buf,
			meshes.quad.vertices.mem);

		// Setup indices
		std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
		meshes.quad.indexCount = indexBuffer.size();
		createBuffer(vk::BufferUsageFlagBits::eIndexBuffer,
			indexBuffer.size() * sizeof(uint32_t),
			indexBuffer.data(),
			meshes.quad.indices.buf,
			meshes.quad.indices.mem);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(2);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);
		// Location 1 : Texture coordinates
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32Sfloat, sizeof(float) * 3);

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
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1)
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
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
			// Binding 1 : Fragment shader image sampler (texture array)
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				1)
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);
	}

	void setupDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the texture array
		vk::DescriptorImageInfo texArrayDescriptor =
			vkTools::initializers::descriptorImageInfo(textureArray.sampler, textureArray.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vertexShader.descriptor),
			// Binding 1 : Fragment shader cubemap sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texArrayDescriptor)
		};

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

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

		// Instacing pipeline
		// Load shaders
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/texturearray/instancing.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/texturearray/instancing.frag.spv", vk::ShaderStageFlagBits::eFragment);

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
	}

	void prepareUniformBuffers()
	{
		uboVS.instance = new UboInstanceData[layerCount];

		uint32_t uboSize = sizeof(uboVS.matrices) + (layerCount * sizeof(UboInstanceData));

		// Vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			uboSize,
			&uboVS,
			uniformData.vertexShader.buffer,
			uniformData.vertexShader.memory,
			uniformData.vertexShader.descriptor);

		// Array indices and model matrices are fixed
		float offset = -1.5f;
		uint32_t index = 0;
		float center = (layerCount*offset) / 2;
		for (uint32_t i = 0; i < layerCount; i++)
		{
			// Instance model matrix
			uboVS.instance[i].model = glm::translate(glm::mat4(), glm::vec3(0.0f, i * offset - center, 0.0f));
			uboVS.instance[i].model = glm::rotate(uboVS.instance[i].model, glm::radians(60.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			// Instance texture array index
			uboVS.instance[i].arrayIndex.x = i;
		}

		// Update instanced part of the uniform buffer
		uint32_t dataOffset = sizeof(uboVS.matrices);
		uint32_t dataSize = layerCount * sizeof(UboInstanceData);
		void * pData = device.mapMemory(uniformData.vertexShader.memory, dataOffset, dataSize, vk::MemoryMapFlags());
		memcpy(pData, uboVS.instance, dataSize);
		device.unmapMemory(uniformData.vertexShader.memory);

		updateUniformBufferMatrices();
	}

	void updateUniformBufferMatrices()
	{
		// Only updates the uniform buffer block part containing the global matrices

		// Projection
		uboVS.matrices.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);

		// View
		uboVS.matrices.view = glm::translate(glm::mat4(), glm::vec3(0.0f, -1.0f, zoom));
		uboVS.matrices.view = glm::rotate(uboVS.matrices.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.matrices.view = glm::rotate(uboVS.matrices.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.matrices.view = glm::rotate(uboVS.matrices.view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		// Only update the matrices part of the uniform buffer
		void *pData = device.mapMemory(uniformData.vertexShader.memory, 0, sizeof(uboVS.matrices), vk::MemoryMapFlags());
		memcpy(pData, &uboVS.matrices, sizeof(uboVS.matrices));
		device.unmapMemory(uniformData.vertexShader.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		setupVertexDescriptions();
		loadTextures();
		generateQuad();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		vkDeviceWaitIdle(device);
		draw();
		vkDeviceWaitIdle(device);
	}

	virtual void viewChanged()
	{
		updateUniformBufferMatrices();
	}
};

RUN_EXAMPLE(VulkanExample)
