/*
* Vulkan Example - Compute shader ray tracing
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"


#define TEX_DIM 2048

// Vertex layout for this example
struct Vertex {
	float pos[3];
	float uv[2];
};

class VulkanExample : public VulkanExampleBase
{
private:
	vkTools::VulkanTexture textureComputeTarget;
public:
	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer quad;
	} meshes;

	vkTools::UniformData uniformDataCompute;

	struct {
		glm::vec3 lightPos;
		// Aspect ratio of the viewport
		float aspectRatio;
		glm::vec4 fogColor = glm::vec4(0.0f);
		struct {
			glm::vec3 pos = glm::vec3(0.0f, 1.5f, 4.0f);
			glm::vec3 lookat = glm::vec3(0.0f, 0.5f, 0.0f);
			float fov = 10.0f;
		} camera;
	} uboCompute;

	struct {
		vk::Pipeline display;
		vk::Pipeline compute;
	} pipelines;

	int vertexBufferSize;

	vk::Queue computeQueue;
	vk::CommandBuffer computeCmdBuffer;
	vk::PipelineLayout computePipelineLayout;
	vk::DescriptorSet computeDescriptorSet;
	vk::DescriptorSetLayout computeDescriptorSetLayout;
	vk::DescriptorPool computeDescriptorPool;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSet descriptorSetPostCompute;
	vk::DescriptorSetLayout descriptorSetLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -2.0f;
		title = "Vulkan Example - Compute shader ray tracing";
		uboCompute.aspectRatio = (float)width / (float)height;
		paused = true;
		timerSpeed *= 0.5f;
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		device.destroyPipeline(pipelines.display, nullptr);
		device.destroyPipeline(pipelines.compute, nullptr);

		device.destroyPipelineLayout(pipelineLayout, nullptr);
		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		vkMeshLoader::freeMeshBufferResources(device, &meshes.quad);

		vkTools::destroyUniformData(device, &uniformDataCompute);

		device.freeCommandBuffers(cmdPool, computeCmdBuffer);

		textureLoader->destroyTexture(textureComputeTarget);
	}

	// Prepare a texture target that is used to store compute shader calculations
	void prepareTextureTarget(vkTools::VulkanTexture *tex, uint32_t width, uint32_t height, vk::Format format)
	{
		// Get device properties for the requested texture format
		vk::FormatProperties formatProperties;
		formatProperties = physicalDevice.getFormatProperties(format);
		// Check if requested image format supports image storage operations
		assert(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage);

		// Prepare blit target texture
		tex->width = width;
		tex->height = height;

		vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.format = format;
		imageCreateInfo.extent = vk::Extent3D { width, height, 1 };
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
		imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
		imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
		// Image will be sampled in the fragment shader and used as storage target in the compute shader
		imageCreateInfo.usage = 
			vk::ImageUsageFlagBits::eSampled | 
			vk::ImageUsageFlagBits::eStorage;

		vk::MemoryAllocateInfo memAllocInfo;
		vk::MemoryRequirements memReqs;

		tex->image = device.createImage(imageCreateInfo, nullptr);
		memReqs = device.getImageMemoryRequirements(tex->image);
		memAllocInfo.allocationSize = memReqs.size;
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAllocInfo.memoryTypeIndex);
		tex->deviceMemory = device.allocateMemory(memAllocInfo, nullptr);
		device.bindImageMemory(tex->image, tex->deviceMemory, 0);

		tex->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		vkTools::setImageLayout(
			setupCmdBuffer, tex->image, 
			vk::ImageAspectFlagBits::eColor, 
			vk::ImageLayout::ePreinitialized,
			tex->imageLayout);

		// Create sampler
		vk::SamplerCreateInfo sampler;
		sampler.magFilter = vk::Filter::eLinear;
		sampler.minFilter = vk::Filter::eLinear;
		sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
		sampler.addressModeU = vk::SamplerAddressMode::eRepeat;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 0;
		sampler.compareOp = vk::CompareOp::eNever;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		tex->sampler = device.createSampler(sampler, nullptr);

		// Create image view
		vk::ImageViewCreateInfo view;
		view.viewType = vk::ImageViewType::e2D;
		view.format = format;
		view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		view.image = tex->image;
		tex->view = device.createImageView(view, nullptr);
	}

	void buildCommandBuffers()
	{
		// Destroy command buffers if already present
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}

		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[0].color = vkTools::initializers::clearColor({ 0.0f, 0.0f, 0.2f, 0.0f });
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
			

			// Image memory barrier to make sure that compute
			// shader writes are finished before sampling
			// from the texture
			vk::ImageMemoryBarrier imageMemoryBarrier;
			imageMemoryBarrier.oldLayout = vk::ImageLayout::eGeneral;
			imageMemoryBarrier.newLayout = vk::ImageLayout::eGeneral;
			imageMemoryBarrier.image = textureComputeTarget.image;
			imageMemoryBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
			imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
			imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead;
			drawCmdBuffers[i].pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), nullptr, nullptr, imageMemoryBarrier);

			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			vk::DeviceSize offsets = 0;
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.quad.indices.buf, 0, vk::IndexType::eUint32);

			// Display ray traced image generated by compute shader as a full screen quad

			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSetPostCompute, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.display);

			drawCmdBuffers[i].drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
			
		}

	}

	void buildComputeCommandBuffer()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;
		computeCmdBuffer.begin(cmdBufInfo);
		computeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines.compute);
		computeCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout, 0, computeDescriptorSet, nullptr);
		computeCmdBuffer.dispatch(textureComputeTarget.width / 16, textureComputeTarget.height / 16, 1);
		computeCmdBuffer.end();
	}

	void draw()
	{
		// Get next image in the swap chain (back/front buffer)
		prepareFrame();
		// Command buffer to be sumitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		// Submit to queue
		submitFrame();
		queue.submit(submitInfo, VK_NULL_HANDLE);
		// Present frame
		submitFrame();
		
		// Compute
		vk::SubmitInfo computeSubmitInfo;
		computeSubmitInfo.commandBufferCount = 1;
		computeSubmitInfo.pCommandBuffers = &computeCmdBuffer;
		computeQueue.submit(computeSubmitInfo, VK_NULL_HANDLE);
		computeQueue.waitIdle();
	}

	// Setup vertices for a single uv-mapped quad
	void generateQuad()
	{
#define dim 1.0f
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

		// Assign to vertex buffer
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
			// Graphics pipeline uses image samplers for display
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4),
			// Compute pipeline uses storage images image loads and stores
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eStorageImage, 1),
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 3);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Fragment shader image sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				0)
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

		descriptorSetPostCompute = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the color map texture
		vk::DescriptorImageInfo texDescriptor =
			vkTools::initializers::descriptorImageInfo(textureComputeTarget.sampler, textureComputeTarget.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Fragment shader texture sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSetPostCompute,
				vk::DescriptorType::eCombinedImageSampler,
				0,
				&texDescriptor)
		};

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	// Create a separate command buffer for compute commands
	void createComputeCommandBuffer()
	{
		vk::CommandBufferAllocateInfo cmdBufAllocateInfo =
			vkTools::initializers::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		computeCmdBuffer = device.allocateCommandBuffers(cmdBufAllocateInfo)[0];
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

		// Display pipeline
		std::array<vk::PipelineShaderStageCreateInfo,2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/raytracing/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/raytracing/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);

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
		pipelineCreateInfo.renderPass = renderPass;

		pipelines.display = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
	}

	// Prepare the compute pipeline that generates the ray traced image
	void prepareCompute()
	{
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Sampled image (write)
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eStorageImage,
				vk::ShaderStageFlagBits::eCompute,
				0),
			// Binding 1 : Uniform buffer block
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eCompute,
				1)
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		computeDescriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);
		

		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&computeDescriptorSetLayout, 1);

		computePipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
		

		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &computeDescriptorSetLayout, 1);

		computeDescriptorSet = device.allocateDescriptorSets(allocInfo)[0];
		

		std::vector<vk::DescriptorImageInfo> computeTexDescriptors =
		{
			vkTools::initializers::descriptorImageInfo(
				VK_NULL_HANDLE,
				textureComputeTarget.view,
				vk::ImageLayout::eGeneral)
		};

		std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets =
		{
			// Binding 0 : Output storage image
			vkTools::initializers::writeDescriptorSet(
				computeDescriptorSet,
				vk::DescriptorType::eStorageImage,
				0,
				&computeTexDescriptors[0]),
			// Binding 1 : Uniform buffer block
			vkTools::initializers::writeDescriptorSet(
				computeDescriptorSet,
				vk::DescriptorType::eUniformBuffer,
				1,
				&uniformDataCompute.descriptor)
		};

		device.updateDescriptorSets(computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, NULL);


		// Create compute shader pipelines
		vk::ComputePipelineCreateInfo computePipelineCreateInfo =
			vkTools::initializers::computePipelineCreateInfo(computePipelineLayout);

		computePipelineCreateInfo.stage = loadShader(getAssetPath() + "shaders/raytracing/raytracing.comp.spv", vk::ShaderStageFlagBits::eCompute);
		pipelines.compute = device.createComputePipelines(pipelineCache, computePipelineCreateInfo, nullptr)[0];
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboCompute),
			&uboCompute,
			uniformDataCompute.buffer,
			uniformDataCompute.memory,
			uniformDataCompute.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		uboCompute.lightPos.x = 0.0f + sin(glm::radians(timer * 360.0f)) * 2.0f;
		uboCompute.lightPos.y = 5.0f;
		uboCompute.lightPos.z = 1.0f;
		uboCompute.lightPos.z = 0.0f + cos(glm::radians(timer * 360.0f)) * 2.0f;
		void *pData = device.mapMemory(uniformDataCompute.memory, 0, sizeof(uboCompute), vk::MemoryMapFlags());
		memcpy(pData, &uboCompute, sizeof(uboCompute));
		device.unmapMemory(uniformDataCompute.memory);
	}

	// Find and create a compute capable device queue
	void getComputeQueue()
	{
		uint32_t queueIndex = 0;

		std::vector<vk::QueueFamilyProperties> queueProps = physicalDevice.getQueueFamilyProperties();
		uint32_t queueCount = queueProps.size();

		for (queueIndex = 0; queueIndex < queueCount; queueIndex++)
		{
			if (queueProps[queueIndex].queueFlags & vk::QueueFlagBits::eCompute)
				break;
		}
		assert(queueIndex < queueCount);

		vk::DeviceQueueCreateInfo queueCreateInfo;
		queueCreateInfo.queueFamilyIndex = queueIndex;
		queueCreateInfo.queueCount = 1;
		computeQueue = device.getQueue(queueIndex, 0);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		generateQuad();
		getComputeQueue();
		createComputeCommandBuffer();
		setupVertexDescriptions();
		prepareUniformBuffers();
		prepareTextureTarget(
			&textureComputeTarget, 
			TEX_DIM,
			TEX_DIM,
			vk::Format::eR8G8B8A8Unorm);
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		prepareCompute();
		buildCommandBuffers(); 
		buildComputeCommandBuffer();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		if (!paused)
		{
			updateUniformBuffers();
		}
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
