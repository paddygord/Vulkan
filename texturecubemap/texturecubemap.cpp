/*
* Vulkan Example - Cube map texture loading and displaying
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/




#include "vulkanexamplebase.h"


// Vertex layout for this example
std::vector<vkMeshLoader::VertexLayout> vertexLayout =
{
	vkMeshLoader::VERTEX_LAYOUT_POSITION,
	vkMeshLoader::VERTEX_LAYOUT_NORMAL,
	vkMeshLoader::VERTEX_LAYOUT_UV
};

class VulkanExample : public VulkanExampleBase
{
public:
	vkTools::VulkanTexture cubeMap;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer skybox, object;
	} meshes;

	struct {
		vkTools::UniformData objectVS;
		vkTools::UniformData skyboxVS;
	} uniformData;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
	} uboVS;

	struct {
		vk::Pipeline skybox;
		vk::Pipeline reflect;
	} pipelines;

	struct {
		vk::DescriptorSet object;
		vk::DescriptorSet skybox;
	} descriptorSets;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSetLayout descriptorSetLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -4.0f;
		rotationSpeed = 0.25f;
		rotation = { -2.25f, -35.0f, 0.0f };
		title = "Vulkan Example - Cube map";
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		// Clean up texture resources
		device.destroyImageView(cubeMap.view);
		device.destroyImage(cubeMap.image);
		device.destroySampler(cubeMap.sampler);
		device.freeMemory(cubeMap.deviceMemory);

		device.destroyPipeline(pipelines.skybox);
		device.destroyPipeline(pipelines.reflect);

		device.destroyPipelineLayout(pipelineLayout);
		device.destroyDescriptorSetLayout(descriptorSetLayout);

		vkMeshLoader::freeMeshBufferResources(device, &meshes.object);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.skybox);

		vkTools::destroyUniformData(device, &uniformData.objectVS);
		vkTools::destroyUniformData(device, &uniformData.skyboxVS);
	}

	void loadCubemap(std::string filename, vk::Format format, bool forceLinearTiling)
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

		gli::textureCube texCube(gli::load((const char*)textureData, size));
#else
		gli::textureCube texCube(gli::load(filename));
#endif

		assert(!texCube.empty());

		cubeMap.width = texCube[0].dimensions().x;
		cubeMap.height = texCube[0].dimensions().y;

		vk::MemoryAllocateInfo memAllocInfo;
		vk::MemoryRequirements memReqs;

		// Create a host-visible staging buffer that contains the raw image data
		vk::Buffer stagingBuffer;
		vk::DeviceMemory stagingMemory;

		vk::BufferCreateInfo bufferCreateInfo;
		bufferCreateInfo.size = texCube.size();
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
		memcpy(data, texCube.data(), texCube.size());
		device.unmapMemory(stagingMemory);

		// Setup buffer copy regions for the cube faces
		// As all faces of a cube map must have the same dimensions, we can do a single copy
		vk::BufferImageCopy bufferCopyRegion;
		bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 6;
		bufferCopyRegion.imageExtent.width = cubeMap.width;
		bufferCopyRegion.imageExtent.height = cubeMap.height;
		bufferCopyRegion.imageExtent.depth = 1;

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
		imageCreateInfo.extent = vk::Extent3D { cubeMap.width, cubeMap.height, 1 };
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
		// Cube faces count as array layers in Vulkan
		imageCreateInfo.arrayLayers = 6;
		// This flag is required for cube map images
		imageCreateInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;

		cubeMap.image = device.createImage(imageCreateInfo);

		memReqs = device.getImageMemoryRequirements(cubeMap.image);

		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

		cubeMap.deviceMemory = device.allocateMemory(memAllocInfo);
		device.bindImageMemory(cubeMap.image, cubeMap.deviceMemory, 0);

		vk::CommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

		// Image barrier for optimal image (target)
		// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 6;

		vkTools::setImageLayout(
			copyCmd,
			cubeMap.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::ePreinitialized,
			vk::ImageLayout::eTransferDstOptimal,
			subresourceRange);

		// Copy the cube map faces from the staging buffer to the optimal tiled image
		copyCmd.copyBufferToImage(stagingBuffer, cubeMap.image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegion);

		// Change texture image layout to shader read after all faces have been copied
		cubeMap.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		vkTools::setImageLayout(
			copyCmd,
			cubeMap.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			cubeMap.imageLayout,
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
		cubeMap.sampler = device.createSampler(sampler);

		// Create image view
		vk::ImageViewCreateInfo view;
		// Cube map view type
		view.viewType = vk::ImageViewType::eCube;
		view.format = format;
		view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		// 6 array layers (faces)
		view.subresourceRange.layerCount = 6;
		view.image = cubeMap.image;
		cubeMap.view = device.createImageView(view);

		// Clean up staging resources
		device.freeMemory(stagingMemory);
		device.destroyBuffer(stagingBuffer);
	}

	void buildCommandBuffers()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
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

			vk::DeviceSize offsets = 0;

			// Skybox
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.skybox, nullptr);
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.skybox.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.skybox.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skybox);
			drawCmdBuffers[i].drawIndexed(meshes.skybox.indexCount, 1, 0, 0, 0);

			// 3D object
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.object, nullptr);
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.object.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.object.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.reflect);
			drawCmdBuffers[i].drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);

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

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/sphere.obj", &meshes.object, vertexLayout, 0.05f);
		loadMesh(getAssetPath() + "models/cube.obj", &meshes.skybox, vertexLayout, 0.05f);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vkMeshLoader::vertexSize(vertexLayout), vk::VertexInputRate::eVertex);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(3);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);
		// Location 1 : Normal
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3);
		// Location 2 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 5);

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
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
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
			// Binding 1 : Fragment shader image sampler
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

	void setupDescriptorSets()
	{
		// Image descriptor for the cube map texture
		vk::DescriptorImageInfo cubeMapDescriptor =
			vkTools::initializers::descriptorImageInfo(cubeMap.sampler, cubeMap.view, vk::ImageLayout::eGeneral);

		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		// 3D object descriptor set
		descriptorSets.object = device.allocateDescriptorSets(allocInfo)[0];

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.object, 
				vk::DescriptorType::eUniformBuffer, 
				0, 
				&uniformData.objectVS.descriptor),
			// Binding 1 : Fragment shader cubemap sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.object, 
				vk::DescriptorType::eCombinedImageSampler, 
				1, 
				&cubeMapDescriptor)
		};
		device.updateDescriptorSets(writeDescriptorSets, nullptr);

		// Sky box descriptor set
		descriptorSets.skybox = device.allocateDescriptorSets(allocInfo)[0];

		writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.skybox,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.skyboxVS.descriptor),
			// Binding 1 : Fragment shader cubemap sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.skybox,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&cubeMapDescriptor)
		};
		device.updateDescriptorSets(writeDescriptorSets, nullptr);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

		vk::PipelineColorBlendAttachmentState blendAttachmentState =
			vkTools::initializers::pipelineColorBlendAttachmentState();

		vk::PipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		vk::PipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual);

		vk::PipelineViewportStateCreateInfo viewportState =
			vkTools::initializers::pipelineViewportStateCreateInfo(1, 1);

		vk::PipelineMultisampleStateCreateInfo multisampleState;

		std::vector<vk::DynamicState> dynamicStateEnables = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};
		vk::PipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

		// Skybox pipeline (background cube)
		std::array<vk::PipelineShaderStageCreateInfo,2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/cubemap/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/cubemap/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

		pipelines.skybox = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Cube map reflect pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/cubemap/reflect.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/cubemap/reflect.frag.spv", vk::ShaderStageFlagBits::eFragment);
		depthStencilState.depthWriteEnable = VK_TRUE;
		pipelines.reflect = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// 3D objact
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVS),
			&uboVS,
			uniformData.objectVS.buffer,
			uniformData.objectVS.memory,
			uniformData.objectVS.descriptor);

		// Skybox
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVS),
			&uboVS,
			uniformData.skyboxVS.buffer,
			uniformData.skyboxVS.memory,
			uniformData.skyboxVS.descriptor);
	}

	void updateUniformBuffers()
	{
		// 3D object
		glm::mat4 viewMatrix = glm::mat4();
		uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
		viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, zoom));

		uboVS.model = glm::mat4();
		uboVS.model = viewMatrix * glm::translate(uboVS.model, glm::vec3(0, 0, 0));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		void *pData = device.mapMemory(uniformData.objectVS.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());
		memcpy(pData, &uboVS, sizeof(uboVS));
		device.unmapMemory(uniformData.objectVS.memory);

		// Skysphere
		viewMatrix = glm::mat4();
		uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);

		uboVS.model = glm::mat4();
		uboVS.model = viewMatrix * glm::translate(uboVS.model, glm::vec3(0, 0, 0));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		pData = device.mapMemory(uniformData.skyboxVS.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());
		memcpy(pData, &uboVS, sizeof(uboVS));
		device.unmapMemory(uniformData.skyboxVS.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		loadCubemap(
			getAssetPath() + "textures/cubemap_yokohama.ktx",
			vk::Format::eBc3UnormBlock,
			false);
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSets();
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
		updateUniformBuffers();
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
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