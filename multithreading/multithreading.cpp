/*
* Vulkan Example - Multi threaded command buffer generation and rendering
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
#include <thread>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"

#include "threadpool.hpp"
#include "frustum.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

// Vertex layout used in this example
// Vertex layout for this example
std::vector<vkMeshLoader::VertexLayout> vertexLayout =
{
	vkMeshLoader::VERTEX_LAYOUT_POSITION,
	vkMeshLoader::VERTEX_LAYOUT_NORMAL,
	vkMeshLoader::VERTEX_LAYOUT_COLOR,
};

class VulkanExample : public VulkanExampleBase
{
public:
	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer ufo;
		vkMeshLoader::MeshBuffer skysphere;
	} meshes;

	// Shared matrices used for thread push constant blocks
	struct {
		glm::mat4 projection;
		glm::mat4 view;
	} matrices;

	struct {
		vk::Pipeline phong;
		vk::Pipeline starsphere;
	} pipelines;

	vk::PipelineLayout pipelineLayout;

	vk::CommandBuffer primaryCommandBuffer;
	vk::CommandBuffer secondaryCommandBuffer;

	// Number of animated objects to be renderer
	// by using threads and secondary command buffers
	uint32_t numObjectsPerThread;

	// Multi threaded stuff
	// Max. number of concurrent threads
	uint32_t numThreads;

	// Use push constants to update shader
	// parameters on a per-thread base
	struct ThreadPushConstantBlock {
		glm::mat4 mvp;
		glm::vec3 color;
	};
	
	struct ObjectData {
		glm::mat4 model;
		glm::vec3 pos;
		glm::vec3 rotation;
		float rotationDir;
		float rotationSpeed;
		float scale;
		float deltaT;
		float stateT = 0;
		bool visible = true;
	};

	struct ThreadData {
		vkMeshLoader::MeshBuffer mesh;
		vk::CommandPool commandPool;
		// One command buffer per render object
		std::vector<vk::CommandBuffer> commandBuffer;
		// One push constant block per render object
		std::vector<ThreadPushConstantBlock> pushConstBlock;
		// Per object information (position, rotation, etc.)
		std::vector<ObjectData> objectData;
	};
	std::vector<ThreadData> threadData;

	vkTools::ThreadPool threadPool;

	// Fence to wait for all command buffers to finish before
	// presenting to the swap chain
	VkFence renderFence = {};

	// Max. dimension of the ufo mesh for use as the sphere
	// radius for frustum culling
	float objectSphereDim;

	// View frustum for culling invisible objects
	vkTools::Frustum frustum;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -32.5f;
		zoomSpeed = 2.5f;
		rotationSpeed = 0.5f;
		rotation = { 0.0f, 37.5f, 0.0f };
		enableTextOverlay = true;
		title = "Vulkan Example - Multi threaded rendering";
		// Get number of max. concurrrent threads
		numThreads = std::thread::hardware_concurrency();
		assert(numThreads > 0);
#if defined(__ANDROID__)
		LOGD("numThreads = %d", numThreads);
#else
		std::cout << "numThreads = " << numThreads << std::endl;
#endif
		srand(time(NULL));

		threadPool.setThreadCount(numThreads);

		numObjectsPerThread = 256 / numThreads;
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		device.destroyPipeline(pipelines.phong, nullptr);
		device.destroyPipeline(pipelines.starsphere, nullptr);

		device.destroyPipelineLayout(pipelineLayout, nullptr);

		device.freeCommandBuffers(cmdPool, primaryCommandBuffer);
		device.freeCommandBuffers(cmdPool, secondaryCommandBuffer);

		vkMeshLoader::freeMeshBufferResources(device, &meshes.ufo);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.skysphere);

		for (auto& thread : threadData)
		{
			device.freeCommandBuffers(thread.commandPool, thread.commandBuffer.size(), thread.commandBuffer.data());
			device.destroyCommandPool(thread.commandPool, nullptr);
			vkMeshLoader::freeMeshBufferResources(device, &thread.mesh);
		}

		vkDestroyFence(device, renderFence, nullptr);
	}

	float rnd(float range)
	{
		return range * (rand() / double(RAND_MAX));
	}

	// Create all threads and initialize shader push constants
	void prepareMultiThreadedRenderer()
	{
		// Since this demo updates the command buffers on each frame
		// we don't use the per-framebuffer command buffers from the
		// base class, and create a single primary command buffer instead
		vk::CommandBufferAllocateInfo cmdBufAllocateInfo =
			vkTools::initializers::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		primaryCommandBuffer = device.allocateCommandBuffers(cmdBufAllocateInfo)[0];

		// Create a secondary command buffer for rendering the star sphere
		cmdBufAllocateInfo.level = vk::CommandBufferLevel::eSecondary;
		secondaryCommandBuffer = device.allocateCommandBuffers(cmdBufAllocateInfo)[0];
		
		threadData.resize(numThreads);

		createSetupCommandBuffer();

		float maxX = std::floor(std::sqrt(numThreads * numObjectsPerThread));
		uint32_t posX = 0;
		uint32_t posZ = 0;

		for (uint32_t i = 0; i < numThreads; i++)
		{
			ThreadData *thread = &threadData[i];
			
			// Create one command pool for each thread
			vk::CommandPoolCreateInfo cmdPoolInfo;
			cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
			cmdPoolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
			thread->commandPool = device.createCommandPool(cmdPoolInfo);

			// One secondary command buffer per object that is updated by this thread
			thread->commandBuffer.resize(numObjectsPerThread);
			// Generate secondary command buffers for each thread
			vk::CommandBufferAllocateInfo secondaryCmdBufAllocateInfo =
				vkTools::initializers::commandBufferAllocateInfo(
				        thread->commandPool,
				        vk::CommandBufferLevel::eSecondary,
				        thread->commandBuffer.size());
			thread->commandBuffer = device.allocateCommandBuffers(secondaryCmdBufAllocateInfo);

			// Unique vertex and index buffers per thread
                                                createBuffer(
                                                        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                                        meshes.ufo.vertices.size,
                                                        nullptr,
                                                        thread->mesh.vertices.buf,
                                                        thread->mesh.vertices.mem);
                                                createBuffer(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                                                meshes.ufo.indices.size,
                                                                nullptr,
                                                                thread->mesh.indices.buf,
                                                                thread->mesh.indices.mem);

			// Copy from mesh buffer
			vk::BufferCopy copyRegion = {};

			// Vertex buffer
			copyRegion.size = meshes.ufo.vertices.size;
			setupCmdBuffer.copyBuffer(meshes.ufo.vertices.buf, thread->mesh.vertices.buf, copyRegion);
			// Index buffer
			copyRegion.size = meshes.ufo.indices.size;
			setupCmdBuffer.copyBuffer(meshes.ufo.indices.buf, thread->mesh.indices.buf, copyRegion);

			// todo : staging

			thread->mesh.indexCount = meshes.ufo.indexCount;

			thread->pushConstBlock.resize(numObjectsPerThread);
			thread->objectData.resize(numObjectsPerThread);

			float step = 360.0f / (float)(numThreads * numObjectsPerThread);
			for (uint32_t j = 0; j < numObjectsPerThread; j++)
			{
				float radius = 8.0f + rnd(8.0f) - rnd(4.0f);

				thread->objectData[j].pos.x = (posX - maxX / 2.0f) * 3.0f + rnd(1.5f) - rnd(1.5f);
				thread->objectData[j].pos.z = (posZ - maxX / 2.0f) * 3.0f + rnd(1.5f) - rnd(1.5f);

				posX += 1.0f;
				if (posX >= maxX)
				{
					posX = 0.0f;
					posZ += 1.0f;
				}

				thread->objectData[j].rotation = glm::vec3(0.0f, rnd(360.0f), 0.0f);
				thread->objectData[j].deltaT = rnd(1.0f);
				thread->objectData[j].rotationDir = (rnd(100.0f) < 50.0f) ? 1.0f : -1.0f;
				thread->objectData[j].rotationSpeed = (2.0f + rnd(4.0f)) * thread->objectData[j].rotationDir;
				thread->objectData[j].scale = 0.75f + rnd(0.5f);

				thread->pushConstBlock[j].color = glm::vec3(rnd(1.0f), rnd(1.0f), rnd(1.0f));
			}
		}
		
		// Submit buffer copies to the queue
		flushSetupCommandBuffer();
		// todo : fence?
	}

	// Builds the secondary command buffer for each thread
	void threadRenderCode(uint32_t threadIndex, uint32_t cmdBufferIndex, vk::CommandBufferInheritanceInfo inheritanceInfo)
	{
		ThreadData *thread = &threadData[threadIndex];
		ObjectData *objectData = &thread->objectData[cmdBufferIndex];

		// Check visibility against view frustum
		objectData->visible = frustum.checkSphere(objectData->pos, objectSphereDim * 0.5f); 

		if (!objectData->visible)
		{
			return;
		}

		vk::CommandBufferBeginInfo commandBufferBeginInfo;
		commandBufferBeginInfo.flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue;
		commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

		vk::CommandBuffer cmdBuffer = thread->commandBuffer[cmdBufferIndex];
		cmdBuffer.begin(commandBufferBeginInfo);

		vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		cmdBuffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
		cmdBuffer.setScissor(0, scissor);

		cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.phong);

		// Update
		objectData->rotation.y += 2.5f * objectData->rotationSpeed * frameTimer;
		if (objectData->rotation.y > 360.0f)
		{
			objectData->rotation.y -= 360.0f;
		}
		objectData->deltaT += 0.15f * frameTimer;
		if (objectData->deltaT > 1.0f)
			objectData->deltaT -= 1.0f;
		objectData->pos.y = sin(glm::radians(objectData->deltaT * 360.0f)) * 2.5f;

		objectData->model = glm::translate(glm::mat4(), objectData->pos);
		objectData->model = glm::rotate(objectData->model, -sinf(glm::radians(objectData->deltaT * 360.0f)) * 0.25f, glm::vec3(objectData->rotationDir, 0.0f, 0.0f));
		objectData->model = glm::rotate(objectData->model, glm::radians(objectData->rotation.y), glm::vec3(0.0f, objectData->rotationDir, 0.0f));
		objectData->model = glm::rotate(objectData->model, glm::radians(objectData->deltaT * 360.0f), glm::vec3(0.0f, objectData->rotationDir, 0.0f));
		objectData->model = glm::scale(objectData->model, glm::vec3(objectData->scale));

		thread->pushConstBlock[cmdBufferIndex].mvp = matrices.projection * matrices.view * objectData->model;

		// Update shader push constant block
		// Contains model view matrix
		cmdBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(ThreadPushConstantBlock), &thread->pushConstBlock[cmdBufferIndex]);

		vk::DeviceSize offsets = 0;
		cmdBuffer.bindVertexBuffers(0, thread->mesh.vertices.buf, offsets);
		cmdBuffer.bindIndexBuffer(thread->mesh.indices.buf, 0, vk::IndexType::eUint32);
		cmdBuffer.drawIndexed(thread->mesh.indexCount, 1, 0, 0, 0);

		cmdBuffer.end();
	}

	void updateSecondaryCommandBuffer(vk::CommandBufferInheritanceInfo inheritanceInfo)
	{
		// Secondary command buffer for the sky sphere
		vk::CommandBufferBeginInfo commandBufferBeginInfo;
		commandBufferBeginInfo.flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue;
		commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

		secondaryCommandBuffer.begin(commandBufferBeginInfo);

		vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		secondaryCommandBuffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
		secondaryCommandBuffer.setScissor(0, scissor);

		secondaryCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.starsphere);


		glm::mat4 view = glm::mat4();
		view = glm::rotate(view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		view = glm::rotate(view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		view = glm::rotate(view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		glm::mat4 mvp = matrices.projection * view;

		secondaryCommandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(mvp), &mvp);

		vk::DeviceSize offsets = 0;
		secondaryCommandBuffer.bindVertexBuffers(0, meshes.skysphere.vertices.buf, offsets);
		secondaryCommandBuffer.bindIndexBuffer(meshes.skysphere.indices.buf, 0, vk::IndexType::eUint32);
		secondaryCommandBuffer.drawIndexed(meshes.skysphere.indexCount, 1, 0, 0, 0);

		secondaryCommandBuffer.end();
	}

	// Updates the secondary command buffers using a thread pool 
	// and puts them into the primary command buffer that's 
	// lat submitted to the queue for rendering
	void updateCommandBuffers(vk::Framebuffer frameBuffer)
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[0].color = { std::array<float, 4> {0.0f, 0.0f, 0.2f, 0.0f} };
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = frameBuffer;

		// Set target frame buffer
		primaryCommandBuffer.begin(cmdBufInfo);

		// The primary command buffer does not contain any rendering commands
		// These are stored (and retrieved) from the secondary command buffers
		primaryCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eSecondaryCommandBuffers);

		// Inheritance info for the secondary command buffers
		vk::CommandBufferInheritanceInfo inheritanceInfo;
		inheritanceInfo.renderPass = renderPass;
		// Secondary command buffer also use the currently active framebuffer
		inheritanceInfo.framebuffer = frameBuffer;

		// Contains the list of secondary command buffers to be executed
		std::vector<vk::CommandBuffer> commandBuffers;

		// Secondary command buffer with star background sphere
		updateSecondaryCommandBuffer(inheritanceInfo);
		commandBuffers.push_back(secondaryCommandBuffer);

		// Add a job to the thread's queue for each object to be rendered
		for (uint32_t t = 0; t < numThreads; t++)
		{
			for (uint32_t i = 0; i < numObjectsPerThread; i++)
			{
				threadPool.threads[t]->addJob([=] { threadRenderCode(t, i, inheritanceInfo); });
			}
		}
			
		threadPool.wait();

		// Only submit if object is within the current view frustum
		for (uint32_t t = 0; t < numThreads; t++)
		{
			for (uint32_t i = 0; i < numObjectsPerThread; i++)
			{
				if (threadData[t].objectData[i].visible)
				{
					commandBuffers.push_back(threadData[t].commandBuffer[i]);
				}
			}
		}

		// Execute render commands from the secondary command buffer
		primaryCommandBuffer.executeCommands(commandBuffers.size(), commandBuffers.data());

		primaryCommandBuffer.endRenderPass();

		primaryCommandBuffer.end();
	}

                void draw()
                {
                                VulkanExampleBase::prepareFrame();

                                updateCommandBuffers(frameBuffers[currentBuffer]);

                                submitInfo.commandBufferCount = 1;
                                submitInfo.pCommandBuffers = &primaryCommandBuffer;

                                queue.submit(submitInfo, renderFence);

                                // Wait for fence to signal that all command buffers are ready
                                vk::Result fenceRes;
                                do
                                {
                                                fenceRes = device.waitForFences(renderFence, VK_TRUE, 100000000);
                                } while (fenceRes == vk::Result::eTimeout);
                                vkTools::checkResult(fenceRes);
                                VK_CHECK_RESULT(fenceRes);
                                vkResetFences(device, 1, &renderFence);

                                VulkanExampleBase::submitFrame();
                }


	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/retroufo_red_lowpoly.dae", &meshes.ufo, vertexLayout, 0.12f);
		loadMesh(getAssetPath() + "models/sphere.obj", &meshes.skysphere, vertexLayout, 1.0f);
		objectSphereDim = std::max(std::max(meshes.ufo.dim.x, meshes.ufo.dim.y), meshes.ufo.dim.z);
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
		// Location 3 : Color
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32Sfloat, sizeof(float) * 6);

		vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupPipelineLayout()
	{
		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
		// Push constants for model matrices
		vk::PushConstantRange pushConstantRange =
			vkTools::initializers::pushConstantRange(vk::ShaderStageFlagBits::eVertex, sizeof(ThreadPushConstantBlock), 0);

		// Push constant ranges are part of the pipeline layout
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);
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

		// Solid rendering pipeline
		// Load shaders
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/multithreading/phong.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/multithreading/phong.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

		pipelines.phong = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo)[0];

		// Star sphere rendering pipeline
		rasterizationState.cullMode = vk::CullModeFlagBits::eFront;
		depthStencilState.depthWriteEnable = VK_FALSE;
		shaderStages[0] = loadShader(getAssetPath() + "shaders/multithreading/starsphere.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/multithreading/starsphere.frag.spv", vk::ShaderStageFlagBits::eFragment);
		pipelines.starsphere = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	void updateMatrices()
	{
		matrices.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
		matrices.view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));
		matrices.view = glm::rotate(matrices.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		matrices.view = glm::rotate(matrices.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		matrices.view = glm::rotate(matrices.view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		frustum.update(matrices.projection * matrices.view);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		// Create a fence for synchronization
		VkFenceCreateInfo fenceCreateInfo = vkTools::initializers::fenceCreateInfo(VK_FLAGS_NONE);
		vkCreateFence(device, &fenceCreateInfo, NULL, &renderFence);
		loadMeshes();
		setupVertexDescriptions();
		setupPipelineLayout();
		preparePipelines();
		prepareMultiThreadedRenderer();
		updateMatrices();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
	}

	virtual void viewChanged()
	{
		updateMatrices();
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
		textOverlay->addText("Using " + std::to_string(numThreads) + " threads", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
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
