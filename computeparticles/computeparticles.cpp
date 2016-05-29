/*
* Vulkan Example - Attraction based compute shader particle system
*
* Updated compute shader by Lukas Bergdoll (https://github.com/Voultapher)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false
#if defined(__ANDROID__)
// Lower particle count on Android for performance reasons
#define PARTICLE_COUNT 64 * 1024
#else
#define PARTICLE_COUNT 256 * 1024
#endif

class VulkanExample : public VulkanExampleBase
{
public:
	float timer = 0.0f;
	float animStart = 20.0f;
	bool animate = true;

	struct {
		vkTools::VulkanTexture particle;
		vkTools::VulkanTexture gradient;
	} textures;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vk::Pipeline postCompute;
		// Compute pipelines are separated from 
		// graphics pipelines in Vulkan
		vk::Pipeline compute;
	} pipelines;

	vk::Queue computeQueue;
	//vk::CommandBuffer computeCmdBuffer;
	vk::PipelineLayout computePipelineLayout;
	vk::DescriptorSet computeDescriptorSet;
	vk::DescriptorSetLayout computeDescriptorSetLayout;

	vkTools::UniformData computeStorageBuffer;

	struct {
		float deltaT;
		float destX;
		float destY;
		int32_t particleCount = PARTICLE_COUNT;
	} computeUbo;

	struct {
		struct {
			vkTools::UniformData ubo;
		} computeShader;
	} uniformData;

	struct Particle {
		glm::vec2 pos;
		glm::vec2 vel;
		glm::vec4 gradientPos;
	};

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSet descriptorSetPostCompute;
	vk::DescriptorSetLayout descriptorSetLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		enableTextOverlay = true;
		title = "Vulkan Example - Compute shader particle system";
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		device.destroyPipeline(pipelines.postCompute, nullptr);

		device.destroyPipelineLayout(pipelineLayout, nullptr);
		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);
		device.destroyBuffer(computeStorageBuffer.buffer, nullptr);
		device.freeMemory(computeStorageBuffer.memory, nullptr);

		vkTools::destroyUniformData(device, &uniformData.computeShader.ubo);

		device.destroyPipelineLayout(computePipelineLayout, nullptr);
		device.destroyDescriptorSetLayout(computeDescriptorSetLayout, nullptr);
		device.destroyPipeline(pipelines.compute, nullptr);

		textureLoader->destroyTexture(textures.particle);
		textureLoader->destroyTexture(textures.gradient);
	}

	void loadTextures()
	{
		textureLoader->loadTexture(getAssetPath() + "textures/particle01_rgba.ktx", vk::Format::eR8G8B8A8Unorm, &textures.particle, false);
		textureLoader->loadTexture(getAssetPath() + "textures/particle_gradient_rgba.ktx", vk::Format::eR8G8B8A8Unorm, &textures.gradient, false);
	}

	void buildCommandBuffers()
	{
		// Destroy command buffers if already present
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}

		vk::CommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();

		vk::ClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo = vkTools::initializers::renderPassBeginInfo();
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

			// Compute particle movement

			// Add memory barrier to ensure that the (rendering) vertex shader operations have finished
			// Required as the compute shader will overwrite the vertex buffer data
			vk::BufferMemoryBarrier bufferBarrier = vkTools::initializers::bufferMemoryBarrier();
			// Vertex shader invocations have finished reading from the buffer
			bufferBarrier.srcAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
			// Compute shader buffer read and write
			bufferBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
			bufferBarrier.buffer = computeStorageBuffer.buffer;
			bufferBarrier.size = computeStorageBuffer.descriptor.range;
			bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			drawCmdBuffers[i].pipelineBarrier(vk::PipelineStageFlagBits::eVertexShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags(), nullptr, bufferBarrier, nullptr);

			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eCompute, pipelines.compute);
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout, 0, computeDescriptorSet, nullptr);

			// Dispatch the compute job
			drawCmdBuffers[i].dispatch(PARTICLE_COUNT / 16, 1, 1);

			// Add memory barrier to ensure that compute shader has finished writing to the buffer
			// Without this the (rendering) vertex shader may display incomplete results (partial data from last frame)
			// Compute shader has finished writes to the buffer
			bufferBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
			// Vertex shader access (attribute binding)
			bufferBarrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
			bufferBarrier.buffer = computeStorageBuffer.buffer;
			bufferBarrier.size = computeStorageBuffer.descriptor.range;
			bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			drawCmdBuffers[i].pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eVertexShader, vk::DependencyFlags(), nullptr, bufferBarrier, nullptr);

			// Draw the particle system using the update vertex buffer

			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.postCompute);
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSetPostCompute, nullptr);

			vk::DeviceSize offsets = 0;
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, computeStorageBuffer.buffer, offsets);
			drawCmdBuffers[i].draw(PARTICLE_COUNT, 1, 0, 0);

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
		}

	}

	// Setup and fill the compute shader storage buffers for
	// vertex positions and velocities
	void prepareStorageBuffers()
	{

		std::mt19937 rGenerator;
		std::uniform_real_distribution<float> rDistribution(-1.0f, 1.0f);

		// Initial particle positions
		std::vector<Particle> particleBuffer(PARTICLE_COUNT);
		for (auto& particle : particleBuffer)
		{
			particle.pos = glm::vec2(rDistribution(rGenerator), rDistribution(rGenerator));
			particle.vel = glm::vec2(0.0f);
			particle.gradientPos.x = particle.pos.x / 2.0f;
		}

		uint32_t storageBufferSize = particleBuffer.size() * sizeof(Particle);

		// Staging
		// SSBO is static, copy to device local memory 
		// This results in better performance

		struct {
			vk::DeviceMemory memory;
			vk::Buffer buffer;
		} stagingBuffer;
		VulkanExampleBase::createBuffer(vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible,
			storageBufferSize,
			particleBuffer.data(),
			stagingBuffer.buffer,
			stagingBuffer.memory);
		VulkanExampleBase::createBuffer(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			storageBufferSize,
			nullptr,
			computeStorageBuffer.buffer,
			computeStorageBuffer.memory);

		// Copy to staging buffer
		vk::CommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

		vk::BufferCopy copyRegion = {};
		copyRegion.size = storageBufferSize;
		copyCmd.copyBuffer(stagingBuffer.buffer, computeStorageBuffer.buffer, copyRegion);

		VulkanExampleBase::flushCommandBuffer(copyCmd, queue, true);

		device.freeMemory(stagingBuffer.memory, nullptr);
		device.destroyBuffer(stagingBuffer.buffer, nullptr);

		computeStorageBuffer.descriptor.range = storageBufferSize;
		computeStorageBuffer.descriptor.buffer = computeStorageBuffer.buffer;
		computeStorageBuffer.descriptor.offset = 0;

		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Particle), vk::VertexInputRate::eVertex);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(2);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32Sfloat, 0);
		// Location 1 : Gradient position
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32A32Sfloat, 4 * sizeof(float));

		// Assign to vertex buffer
		vertices.inputState = vkTools::initializers::pipelineVertexInputStateCreateInfo();
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
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings;
		// Binding 0 : Particle color map
		setLayoutBindings.push_back(vkTools::initializers::descriptorSetLayoutBinding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0));
		// Binding 1 : Particle gradient ramp
		setLayoutBindings.push_back(vkTools::initializers::descriptorSetLayoutBinding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1));

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);

		vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayout = device.createPipelineLayout(pipelineLayoutCreateInfo, nullptr);
	}

	void setupDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSetPostCompute = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the color map texture
		std::vector<vk::DescriptorImageInfo> texDescriptors;
		texDescriptors.push_back(vkTools::initializers::descriptorImageInfo(textures.particle.sampler, textures.particle.view, vk::ImageLayout::eGeneral));
		texDescriptors.push_back(vkTools::initializers::descriptorImageInfo(textures.gradient.sampler, textures.gradient.view, vk::ImageLayout::eGeneral));

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets;
		// Binding 0 : Particle color map
		writeDescriptorSets.push_back(vkTools::initializers::writeDescriptorSet(descriptorSetPostCompute, vk::DescriptorType::eCombinedImageSampler, 0, &texDescriptors[0]));
		// Binding 1 : Particle gradient ramp
		writeDescriptorSets.push_back(vkTools::initializers::writeDescriptorSet(descriptorSetPostCompute, vk::DescriptorType::eCombinedImageSampler, 1, &texDescriptors[1]));

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::ePointList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

		vk::PipelineColorBlendAttachmentState blendAttachmentState =
			vkTools::initializers::pipelineColorBlendAttachmentState();

		vk::PipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		vk::PipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, vk::CompareOp::eAlways);

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

		// Rendering pipeline
		// Load shaders
		std::array<vk::PipelineShaderStageCreateInfo,2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/computeparticles/particle.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/computeparticles/particle.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

		// Additive blending
		blendAttachmentState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
		blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;

		pipelines.postCompute = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	void prepareCompute()
	{
		// Create compute pipeline
		// Compute pipelines are created separate from graphics pipelines
		// even if they use the same queue

		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Particle position storage buffer
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eStorageBuffer,
				vk::ShaderStageFlagBits::eCompute,
				0),
			// Binding 1 : Uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eCompute,
				1),
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

		std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets =
		{
			// Binding 0 : Particle position storage buffer
			vkTools::initializers::writeDescriptorSet(
				computeDescriptorSet,
				vk::DescriptorType::eStorageBuffer,
				0,
				&computeStorageBuffer.descriptor),
			// Binding 1 : Uniform buffer
			vkTools::initializers::writeDescriptorSet(
				computeDescriptorSet,
				vk::DescriptorType::eUniformBuffer,
				1,
				&uniformData.computeShader.ubo.descriptor)
		};

		device.updateDescriptorSets(computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, NULL);

		// Create pipeline		
		vk::ComputePipelineCreateInfo computePipelineCreateInfo =
			vkTools::initializers::computePipelineCreateInfo(computePipelineLayout);
		computePipelineCreateInfo.stage = loadShader(getAssetPath() + "shaders/computeparticles/particle.comp.spv", vk::ShaderStageFlagBits::eCompute);
		pipelines.compute = device.createComputePipelines(pipelineCache, computePipelineCreateInfo, nullptr)[0];
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Compute shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			sizeof(computeUbo),
			nullptr,
			uniformData.computeShader.ubo.buffer,
			uniformData.computeShader.ubo.memory,
			uniformData.computeShader.ubo.descriptor);

		// Map for host access
		uniformData.computeShader.ubo.mapped = device.mapMemory(uniformData.computeShader.ubo.memory, 0, sizeof(computeUbo), vk::MemoryMapFlags());

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		computeUbo.deltaT = frameTimer * 2.5f;
		if (animate) 
		{
			computeUbo.destX = sin(glm::radians(timer*360.0)) * 0.75f;
			computeUbo.destY = 0.f;
		}
		else
		{
			float normalizedMx = (mousePos.x - static_cast<float>(width / 2)) / static_cast<float>(width / 2);
			float normalizedMy = (mousePos.y - static_cast<float>(height / 2)) / static_cast<float>(height / 2);
			computeUbo.destX = normalizedMx;
			computeUbo.destY = normalizedMy;
		}

		memcpy(uniformData.computeShader.ubo.mapped, &computeUbo, sizeof(computeUbo));
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

		vk::DeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = vk::StructureType::eDeviceQueueCreateInfo;
		queueCreateInfo.pNext = NULL;
		queueCreateInfo.queueFamilyIndex = queueIndex;
		queueCreateInfo.queueCount = 1;
		computeQueue = device.getQueue(queueIndex, 0);
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		queue.submit(submitInfo, VK_NULL_HANDLE);

		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadTextures();
		getComputeQueue();
		prepareStorageBuffers();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		prepareCompute();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();

		if (animate)
		{
			if (animStart > 0.0f)
			{
				animStart -= frameTimer * 5.0f;
			}
			else if (animStart <= 0.0f)
			{
				timer += frameTimer * 0.04f;
				if (timer > 1.f)
					timer = 0.f;
			}
		}
		
		updateUniformBuffers();
	}

	void toggleAnimation()
	{
		animate = !animate;
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
			case 0x41:
				vulkanExample->toggleAnimation();
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