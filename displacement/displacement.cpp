/*
* Vulkan Example - Displacement mapping with tessellation shaders
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
private:
	struct {
		vkTools::VulkanTexture colorMap;
		vkTools::VulkanTexture heightMap;
	} textures;
public:
	bool splitScreen = true;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer object;
	} meshes;

	vkTools::UniformData uniformDataTC, uniformDataTE;

	struct {
		float tessLevel = 8.0;
	} uboTC;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::vec4 lightPos = glm::vec4(0.0, -25.0, 0.0, 0.0);
		float tessAlpha = 1.0;
		float tessStrength = 1.0;
	} uboTE;

	struct {
		vk::Pipeline solid;
		vk::Pipeline wire;
		vk::Pipeline solidPassThrough;
		vk::Pipeline wirePassThrough;
	} pipelines;
	vk::Pipeline *pipelineLeft = &pipelines.solidPassThrough;
	vk::Pipeline *pipelineRight = &pipelines.solid;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSet descriptorSet;
	vk::DescriptorSetLayout descriptorSetLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -35;
		rotation = glm::vec3(-35.0, 0.0, 0);
		title = "Vulkan Example - Tessellation shader displacement mapping";
		// Support for tessellation shaders is optional, so check first
		if (!deviceFeatures.tessellationShader)
		{
			vkTools::exitFatal("Selected GPU does not support tessellation shaders!", "Feature not supported");
		}
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		device.destroyPipeline(pipelines.solid, nullptr);
		device.destroyPipeline(pipelines.wire, nullptr);
		device.destroyPipeline(pipelines.solidPassThrough, nullptr);
		device.destroyPipeline(pipelines.wirePassThrough, nullptr);

		device.destroyPipelineLayout(pipelineLayout, nullptr);
		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		vkMeshLoader::freeMeshBufferResources(device, &meshes.object);

		device.destroyBuffer(uniformDataTC.buffer, nullptr);
		device.freeMemory(uniformDataTC.memory, nullptr);

		device.destroyBuffer(uniformDataTE.buffer, nullptr);
		device.freeMemory(uniformDataTE.memory, nullptr);

		textureLoader->destroyTexture(textures.colorMap);
		textureLoader->destroyTexture(textures.heightMap);
	}

	void loadTextures()
	{
		textureLoader->loadTexture(
			getAssetPath() + "textures/stonewall_colormap_bc3.dds", 
			vk::Format::eBc3UnormBlock, 
			&textures.colorMap);
		textureLoader->loadTexture(
			getAssetPath() + "textures/stonewall_heightmap_rgba.dds", 
			vk::Format::eR8G8B8A8Unorm, 
			&textures.heightMap);
	}

	void reBuildCommandBuffers()
	{
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}
		buildCommandBuffers();
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

			vk::Viewport viewport = vkTools::initializers::viewport(splitScreen ? (float)width / 2.0f : (float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			drawCmdBuffers[i].setLineWidth(1.0f);

			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);

			vk::DeviceSize offsets = 0;
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.object.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.object.indices.buf, 0, vk::IndexType::eUint32);

			if (splitScreen)
			{
				drawCmdBuffers[i].setViewport(0, viewport);
				drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineLeft);
				drawCmdBuffers[i].drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);
				viewport.x = float(width) / 2;
			}

			drawCmdBuffers[i].setViewport(0, viewport);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineRight);
			drawCmdBuffers[i].drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
			
		}
	}

	void draw()
	{
		// Get next image in the swap chain (back/front buffer)
		swapChain.acquireNextImage(semaphores.presentComplete, currentBuffer);
		

		submitPostPresentBarrier(swapChain.buffers[currentBuffer].image);

		// Command buffer to be sumitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		queue.submit(submitInfo, VK_NULL_HANDLE);
		
		submitPrePresentBarrier(swapChain.buffers[currentBuffer].image);

		swapChain.queuePresent(queue, currentBuffer, semaphores.renderComplete);
		
		queue.waitIdle();
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/torus.obj", &meshes.object, vertexLayout, 0.25f);
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

		// Location 1 : Normals
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3);

		// Location 2 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 6);

		vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses two ubos and two image samplers
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 2),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Tessellation control shader ubo
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eUniformBuffer, 
				vk::ShaderStageFlagBits::eTessellationControl, 
				0),
			// Binding 1 : Tessellation evaluation shader ubo
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eTessellationEvaluation, 
				1),
			// Binding 2 : Tessellation evaluation shader displacement map image sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eTessellationEvaluation,
				2),
			// Binding 3 : Fragment shader color map image sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				3),
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

		descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

		// Displacement map image descriptor
		vk::DescriptorImageInfo texDescriptorDisplacementMap =
			vkTools::initializers::descriptorImageInfo(textures.heightMap.sampler, textures.heightMap.view, vk::ImageLayout::eGeneral);

		// Color map image descriptor
		vk::DescriptorImageInfo texDescriptorColorMap =
			vkTools::initializers::descriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Tessellation control shader ubo
			vkTools::initializers::writeDescriptorSet(
				descriptorSet, 
				vk::DescriptorType::eUniformBuffer, 
				0, 
				&uniformDataTC.descriptor),
			// Binding 1 : Tessellation evaluation shader ubo
			vkTools::initializers::writeDescriptorSet(
				descriptorSet, 
				vk::DescriptorType::eUniformBuffer,
				1, 
				&uniformDataTE.descriptor),
			// Binding 2 : Displacement map
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				2,
				&texDescriptorDisplacementMap),
			// Binding 3 : Color map
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				3,
				&texDescriptorColorMap),
		};

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::ePatchList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

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
			vk::DynamicState::eScissor,
			vk::DynamicState::eLineWidth
		};
		vk::PipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

		vk::PipelineTessellationStateCreateInfo tessellationState =
			vkTools::initializers::pipelineTessellationStateCreateInfo(3);

		// Tessellation pipeline
		// Load shaders
		std::array<vk::PipelineShaderStageCreateInfo, 4> shaderStages;
		shaderStages[0] = loadShader(getAssetPath() + "shaders/displacement/base.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/displacement/base.frag.spv", vk::ShaderStageFlagBits::eFragment);
		shaderStages[2] = loadShader(getAssetPath() + "shaders/displacement/displacement.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl);
		shaderStages[3] = loadShader(getAssetPath() + "shaders/displacement/displacement.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation);

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
		pipelineCreateInfo.pTessellationState = &tessellationState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.renderPass = renderPass;

		// Solid pipeline
		pipelines.solid = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
		// Wireframe pipeline
		rasterizationState.polygonMode = vk::PolygonMode::eLine;
		pipelines.wire = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		

		// Pass through pipelines
		// Load pass through tessellation shaders (Vert and frag are reused)
		shaderStages[2] = loadShader(getAssetPath() + "shaders/displacement/passthrough.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl);
		shaderStages[3] = loadShader(getAssetPath() + "shaders/displacement/passthrough.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation);
		// Solid
		rasterizationState.polygonMode = vk::PolygonMode::eFill;
		pipelines.solidPassThrough = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
		// Wireframe
		rasterizationState.polygonMode = vk::PolygonMode::eLine;
		pipelines.wirePassThrough = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Tessellation evaluation shader uniform buffer
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboTE),
			&uboTE,
			uniformDataTE.buffer,
			uniformDataTE.memory,
			uniformDataTE.descriptor);

		// Tessellation control shader uniform buffer
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboTC),
			&uboTC,
			uniformDataTC.buffer,
			uniformDataTC.memory,
			uniformDataTC.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// Tessellation eval
		glm::mat4 viewMatrix = glm::mat4();
		uboTE.projection = glm::perspective(glm::radians(45.0f), (float)(width* ((splitScreen) ? 0.5f : 1.0f)) / (float)height, 0.1f, 256.0f);
		viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, zoom));

		float offset = 0.5f;
		int uboIndex = 1;
		uboTE.model = glm::mat4();
		uboTE.model = viewMatrix * glm::translate(uboTE.model, glm::vec3(0, 0, 0));
		uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		void *pData = device.mapMemory(uniformDataTE.memory, 0, sizeof(uboTE), vk::MemoryMapFlags());
		memcpy(pData, &uboTE, sizeof(uboTE));
		device.unmapMemory(uniformDataTE.memory);

		// Tessellation control
		pData = device.mapMemory(uniformDataTC.memory, 0, sizeof(uboTC), vk::MemoryMapFlags());
		memcpy(pData, &uboTC, sizeof(uboTC));
		device.unmapMemory(uniformDataTC.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadMeshes();
		loadTextures();
		setupVertexDescriptions();
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
		updateUniformBuffers();
	}

	void changeTessellationLevel(float delta)
	{
		uboTC.tessLevel += delta;
		// Clamp
		uboTC.tessLevel = fmax(1.0, fmin(uboTC.tessLevel, 32.0));
		updateUniformBuffers();
	}

	void togglePipelines()
	{
		if (pipelineRight == &pipelines.solid)
		{
			pipelineRight = &pipelines.wire;
			pipelineLeft = &pipelines.wirePassThrough;
		}
		else
		{
			pipelineRight = &pipelines.solid;
			pipelineLeft = &pipelines.solidPassThrough;
		}
		reBuildCommandBuffers();
	}

	void toggleSplitScreen()
	{
		splitScreen = !splitScreen;
		reBuildCommandBuffers();
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
		if (uMsg == WM_KEYDOWN)
		{
			switch (wParam)
			{
			case VK_ADD:
				vulkanExample->changeTessellationLevel(0.25);
				break;
			case VK_SUBTRACT:
				vulkanExample->changeTessellationLevel(-0.25);
				break;
			case 0x57:
				vulkanExample->togglePipelines();
				break;
			case 0x53:
				vulkanExample->toggleSplitScreen();
				break;
			}
		}
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
