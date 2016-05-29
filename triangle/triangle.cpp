/*
* Vulkan Example - Basic indexed triangle rendering
*
* Note :
*	This is a "pedal to the metal" example to show off how to get Vulkan up an displaying something
*	Contrary to the other examples, this one won't make use of helper functions or initializers
*	Except in a few cases (swap chain setup e.g.)
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

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"

#define VERTEX_BUFFER_BIND_ID 0
// Set to "true" to enable Vulkan's validation layers
// See vulkandebug.cpp for details
#define ENABLE_VALIDATION false
// Set to "true" to use staging buffers for uploading
// vertex and index data to device local memory
// See "prepareVertices" for details on what's staging
// and on why to use it
#define USE_STAGING true

class VulkanExample : public VulkanExampleBase
{
public:
	struct {
		vk::Buffer buf;
		vk::DeviceMemory mem;
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		int count;
		vk::Buffer buf;
		vk::DeviceMemory mem;
	} indices;

	struct {
		vk::Buffer buffer;
		vk::DeviceMemory memory;
		vk::DescriptorBufferInfo descriptor;
	}  uniformDataVS;

	struct {
		glm::mat4 projectionMatrix;
		glm::mat4 modelMatrix;
		glm::mat4 viewMatrix;
	} uboVS;

	struct {
		vk::Pipeline solid;
	} pipelines;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSet descriptorSet;
	vk::DescriptorSetLayout descriptorSetLayout;

	// Synchronization semaphores
	struct {
		vk::Semaphore presentComplete;
		vk::Semaphore renderComplete;
	} semaphores;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		width = 1280;
		height = 720;
		zoom = -2.5f;
		title = "Vulkan Example - Basic indexed triangle";
		// Values not set here are initialized in the base class constructor
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		device.destroyPipeline(pipelines.solid, nullptr);

		device.destroyPipelineLayout(pipelineLayout, nullptr);
		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		device.destroyBuffer(vertices.buf, nullptr);
		device.freeMemory(vertices.mem, nullptr);

		device.destroyBuffer(indices.buf, nullptr);
		device.freeMemory(indices.mem, nullptr);

		device.destroySemaphore(semaphores.presentComplete, nullptr);
		device.destroySemaphore(semaphores.renderComplete, nullptr);

		device.destroyBuffer(uniformDataVS.buffer, nullptr);
		device.freeMemory(uniformDataVS.memory, nullptr);
	}

	// Build separate command buffers for every framebuffer image
	// Unlike in OpenGL all rendering commands are recorded once
	// into command buffers that are then resubmitted to the queue
	void buildCommandBuffers()
	{
		vk::CommandBufferBeginInfo cmdBufInfo = {};
		cmdBufInfo.sType = vk::StructureType::eCommandBufferBeginInfo;
		cmdBufInfo.pNext = NULL;

		vk::ClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = vk::StructureType::eRenderPassBeginInfo;
		renderPassBeginInfo.pNext = NULL;
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

			// Start the first sub pass specified in our default render pass setup by the base class
			// This will clear the color and depth attachment
			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			// Update dynamic viewport state
			vk::Viewport viewport = {};
			viewport.height = (float)height;
			viewport.width = (float)width;
			viewport.minDepth = (float) 0.0f;
			viewport.maxDepth = (float) 1.0f;
			drawCmdBuffers[i].setViewport(0, viewport);

			// Update dynamic scissor state
			vk::Rect2D scissor = {};
			scissor.extent.width = width;
			scissor.extent.height = height;
			scissor.offset.x = 0;
			scissor.offset.y = 0;
			drawCmdBuffers[i].setScissor(0, scissor);

			// Bind descriptor sets describing shader binding points
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);

			// Bind the rendering pipeline (including the shaders)
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);

			// Bind triangle vertices
			vk::DeviceSize offsets = 0;
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, vertices.buf, offsets);

			// Bind triangle indices
			drawCmdBuffers[i].bindIndexBuffer(indices.buf, 0, vk::IndexType::eUint32);

			// Draw indexed triangle
			drawCmdBuffers[i].drawIndexed(indices.count, 1, 0, 0, 1);

			drawCmdBuffers[i].endRenderPass();

			// Add a present memory barrier to the end of the command buffer
			// This will transform the frame buffer color attachment to a
			// new layout for presenting it to the windowing system integration 
			vk::ImageMemoryBarrier prePresentBarrier = {};
			prePresentBarrier.sType = vk::StructureType::eImageMemoryBarrier;
			prePresentBarrier.pNext = NULL;
			prePresentBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			prePresentBarrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
			prePresentBarrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
			prePresentBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
			prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			prePresentBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };			
			prePresentBarrier.image = swapChain.buffers[i].image;

			vk::ImageMemoryBarrier *pMemoryBarrier = &prePresentBarrier;
			drawCmdBuffers[i].pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlags(), nullptr, nullptr, prePresentBarrier);

			drawCmdBuffers[i].end();
		}
	}

	void draw()
	{
		// Get next image in the swap chain (back/front buffer)
		swapChain.acquireNextImage(semaphores.presentComplete, currentBuffer);

		// Add a post present image memory barrier
		// This will transform the frame buffer color attachment back
		// to it's initial layout after it has been presented to the
		// windowing system
		vk::ImageMemoryBarrier postPresentBarrier = {};
		postPresentBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		postPresentBarrier.oldLayout = vk::ImageLayout::ePresentSrcKHR;
		postPresentBarrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		postPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		postPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		postPresentBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		postPresentBarrier.image = swapChain.buffers[currentBuffer].image;

		// Use dedicated command buffer from example base class for submitting the post present barrier
		vk::CommandBufferBeginInfo cmdBufInfo = {};
		cmdBufInfo.sType = vk::StructureType::eCommandBufferBeginInfo;

		postPresentCmdBuffer.begin(cmdBufInfo);

		// Put post present barrier into command buffer
		postPresentCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), nullptr, nullptr, postPresentBarrier);

		postPresentCmdBuffer.end();

		// Submit the image barrier to the current queue
		submitInfo = {};
		submitInfo.sType = vk::StructureType::eSubmitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &postPresentCmdBuffer;

		queue.submit(submitInfo, VK_NULL_HANDLE);
		
		// Make sure that the image barrier command submitted to the queue 
		// has finished executing
		queue.waitIdle();

		// The submit infor strcuture contains a list of
		// command buffers and semaphores to be submitted to a queue
		// If you want to submit multiple command buffers, pass an array
		vk::PipelineStageFlags pipelineStages = vk::PipelineStageFlagBits::eBottomOfPipe;
		vk::SubmitInfo submitInfo = {};
		submitInfo.sType = vk::StructureType::eSubmitInfo;
		submitInfo.pWaitDstStageMask = &pipelineStages;
		// The wait semaphore ensures that the image is presented 
		// before we start submitting command buffers agein
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &semaphores.presentComplete;
		// Submit the currently active command buffer
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		// The signal semaphore is used during queue presentation
		// to ensure that the image is not rendered before all
		// commands have been submitted
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &semaphores.renderComplete;

		// Submit to the graphics queue
		queue.submit(submitInfo, VK_NULL_HANDLE);

		// Present the current buffer to the swap chain
		// We pass the signal semaphore from the submit info
		// to ensure that the image is not rendered until
		// all commands have been submitted
		swapChain.queuePresent(queue, currentBuffer, semaphores.renderComplete);
	}

	// Create synchronzation semaphores
	void prepareSemaphore()
	{
		vk::SemaphoreCreateInfo semaphoreCreateInfo = {};
		semaphoreCreateInfo.sType = vk::StructureType::eSemaphoreCreateInfo;
		semaphoreCreateInfo.pNext = NULL;

		// This semaphore ensures that the image is complete
		// before starting to submit again
		semaphores.presentComplete = device.createSemaphore(semaphoreCreateInfo, nullptr);

		// This semaphore ensures that all commands submitted
		// have been finished before submitting the image to the queue
		semaphores.renderComplete = device.createSemaphore(semaphoreCreateInfo, nullptr);
	}

	// Setups vertex and index buffers for an indexed triangle,
	// uploads them to the VRAM and sets binding points and attribute
	// descriptions to match locations inside the shaders
	void prepareVertices(bool useStagingBuffers)
	{
		struct Vertex {
			float pos[3];
			float col[3];
		};

		// Setup vertices
		std::vector<Vertex> vertexBuffer = {
			{ { 1.0f,  1.0f, 0.0f },{ 1.0f, 0.0f, 0.0f } },
			{ { -1.0f,  1.0f, 0.0f },{ 0.0f, 1.0f, 0.0f } },
			{ { 0.0f, -1.0f, 0.0f },{ 0.0f, 0.0f, 1.0f } }
		};
		int vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);

		// Setup indices
		std::vector<uint32_t> indexBuffer = { 0, 1, 2 };
		uint32_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
		indices.count = indexBuffer.size();

		vk::MemoryAllocateInfo memAlloc = {};
		memAlloc.sType = vk::StructureType::eMemoryAllocateInfo;
		vk::MemoryRequirements memReqs;

		void *data;

		if (useStagingBuffers)
		{
			// Static data like vertex and index buffer should be stored on the device memory 
			// for optimal (and fastest) access by the GPU
			//
			// To achieve this we use so-called "staging buffers" :
			// - Create a buffer that's visible to the host (and can be mapped)
			// - Copy the data to this buffer
			// - Create another buffer that's local on the device (VRAM) with the same size
			// - Copy the data from the host to the device using a command buffer
			// - Delete the host visible (staging) buffer
			// - Use the device local buffers for rendering

			struct StagingBuffer {
				vk::DeviceMemory memory;
				vk::Buffer buffer;
			};

			struct {
				StagingBuffer vertices;
				StagingBuffer indices;
			} stagingBuffers;

			// Buffer copies are done on the queue, so we need a command buffer for them
			vk::CommandBufferAllocateInfo cmdBufInfo = {};
			cmdBufInfo.sType = vk::StructureType::eCommandBufferAllocateInfo;
			cmdBufInfo.commandPool = cmdPool;
			cmdBufInfo.level = vk::CommandBufferLevel::ePrimary;
			cmdBufInfo.commandBufferCount = 1;

			vk::CommandBuffer copyCommandBuffer = device.allocateCommandBuffers(cmdBufInfo)[0];

			// Vertex buffer
			vk::BufferCreateInfo vertexBufferInfo = {};
			vertexBufferInfo.sType = vk::StructureType::eBufferCreateInfo;
			vertexBufferInfo.size = vertexBufferSize;
			// Buffer is used as the copy source
			vertexBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
			// Create a host-visible buffer to copy the vertex data to (staging buffer)
			stagingBuffers.vertices.buffer = device.createBuffer(vertexBufferInfo, nullptr);
			memReqs = device.getBufferMemoryRequirements(stagingBuffers.vertices.buffer);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
			stagingBuffers.vertices.memory = device.allocateMemory(memAlloc, nullptr);
			// Map and copy
			data = device.mapMemory(stagingBuffers.vertices.memory, 0, memAlloc.allocationSize, vk::MemoryMapFlags());
			memcpy(data, vertexBuffer.data(), vertexBufferSize);
			device.unmapMemory(stagingBuffers.vertices.memory);
			device.bindBufferMemory(stagingBuffers.vertices.buffer, stagingBuffers.vertices.memory, 0);

			// Create the destination buffer with device only visibility
			// Buffer will be used as a vertex buffer and is the copy destination
			vertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
			vertices.buf = device.createBuffer(vertexBufferInfo, nullptr);
			memReqs = device.getBufferMemoryRequirements(vertices.buf);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
			vertices.mem = device.allocateMemory(memAlloc, nullptr);
			device.bindBufferMemory(vertices.buf, vertices.mem, 0);

			// Index buffer
			vk::BufferCreateInfo indexbufferInfo = {};
			indexbufferInfo.sType = vk::StructureType::eBufferCreateInfo;
			indexbufferInfo.size = indexBufferSize;
			indexbufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
			// Copy index data to a buffer visible to the host (staging buffer)
			stagingBuffers.indices.buffer = device.createBuffer(indexbufferInfo, nullptr);
			memReqs = device.getBufferMemoryRequirements(stagingBuffers.indices.buffer);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
			stagingBuffers.indices.memory = device.allocateMemory(memAlloc, nullptr);
			data = device.mapMemory(stagingBuffers.indices.memory, 0, indexBufferSize, vk::MemoryMapFlags());
			memcpy(data, indexBuffer.data(), indexBufferSize);
			device.unmapMemory(stagingBuffers.indices.memory);
			device.bindBufferMemory(stagingBuffers.indices.buffer, stagingBuffers.indices.memory, 0);

			// Create destination buffer with device only visibility
			indexbufferInfo.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
			indices.buf = device.createBuffer(indexbufferInfo, nullptr);
			memReqs = device.getBufferMemoryRequirements(indices.buf);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
			indices.mem = device.allocateMemory(memAlloc, nullptr);
			device.bindBufferMemory(indices.buf, indices.mem, 0);
			indices.count = indexBuffer.size();

			vk::CommandBufferBeginInfo cmdBufferBeginInfo = {};
			cmdBufferBeginInfo.sType = vk::StructureType::eCommandBufferBeginInfo;
			cmdBufferBeginInfo.pNext = NULL;

			vk::BufferCopy copyRegion = {};

			// Put buffer region copies into command buffer
			// Note that the staging buffer must not be deleted before the copies 
			// have been submitted and executed
			copyCommandBuffer.begin(cmdBufferBeginInfo);

			// Vertex buffer
			copyRegion.size = vertexBufferSize;
			copyCommandBuffer.copyBuffer(stagingBuffers.vertices.buffer, vertices.buf, copyRegion);
			// Index buffer
			copyRegion.size = indexBufferSize;
			copyCommandBuffer.copyBuffer(stagingBuffers.indices.buffer, indices.buf, copyRegion);

			copyCommandBuffer.end();

			// Submit copies to the queue
			vk::SubmitInfo copySubmitInfo = {};
			copySubmitInfo.sType = vk::StructureType::eSubmitInfo;
			copySubmitInfo.commandBufferCount = 1;
			copySubmitInfo.pCommandBuffers = &copyCommandBuffer;

			queue.submit(copySubmitInfo, VK_NULL_HANDLE);
			queue.waitIdle();

			device.freeCommandBuffers(cmdPool, copyCommandBuffer);

			// Destroy staging buffers
			device.destroyBuffer(stagingBuffers.vertices.buffer, nullptr);
			device.freeMemory(stagingBuffers.vertices.memory, nullptr);
			device.destroyBuffer(stagingBuffers.indices.buffer, nullptr);
			device.freeMemory(stagingBuffers.indices.memory, nullptr);
		}
		else
		{
			// Don't use staging
			// Create host-visible buffers only and use these for rendering
			// This is not advised for real world applications and will
			// result in lower performances at least on devices that
			// separate between host visible and device local memory

			// Vertex buffer
			vk::BufferCreateInfo vertexBufferInfo = {};
			vertexBufferInfo.sType = vk::StructureType::eBufferCreateInfo;
			vertexBufferInfo.size = vertexBufferSize;
			vertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;

			// Copy vertex data to a buffer visible to the host
			vertices.buf = device.createBuffer(vertexBufferInfo, nullptr);
			memReqs = device.getBufferMemoryRequirements(vertices.buf);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
			vertices.mem = device.allocateMemory(memAlloc, nullptr);
			data = device.mapMemory(vertices.mem, 0, memAlloc.allocationSize, vk::MemoryMapFlags());
			memcpy(data, vertexBuffer.data(), vertexBufferSize);
			device.unmapMemory(vertices.mem);
			device.bindBufferMemory(vertices.buf, vertices.mem, 0);

			// Index buffer
			vk::BufferCreateInfo indexbufferInfo = {};
			indexbufferInfo.sType = vk::StructureType::eBufferCreateInfo;
			indexbufferInfo.size = indexBufferSize;
			indexbufferInfo.usage = vk::BufferUsageFlagBits::eIndexBuffer;

			// Copy index data to a buffer visible to the host
			memset(&indices, 0, sizeof(indices));
			indices.buf = device.createBuffer(indexbufferInfo, nullptr);
			memReqs = device.getBufferMemoryRequirements(indices.buf);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
			indices.mem = device.allocateMemory(memAlloc, nullptr);
			data = device.mapMemory(indices.mem, 0, indexBufferSize, vk::MemoryMapFlags());
			memcpy(data, indexBuffer.data(), indexBufferSize);
			device.unmapMemory(indices.mem);
			device.bindBufferMemory(indices.buf, indices.mem, 0);
			indices.count = indexBuffer.size();
		}

		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0].binding = VERTEX_BUFFER_BIND_ID;
		vertices.bindingDescriptions[0].stride = sizeof(Vertex);
		vertices.bindingDescriptions[0].inputRate = vk::VertexInputRate::eVertex;

		// Attribute descriptions
		// Describes memory layout and shader attribute locations
		vertices.attributeDescriptions.resize(2);
		// Location 0 : Position
		vertices.attributeDescriptions[0].binding = VERTEX_BUFFER_BIND_ID;
		vertices.attributeDescriptions[0].location = 0;
		vertices.attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
		vertices.attributeDescriptions[0].offset = 0;
		// Location 1 : Color
		vertices.attributeDescriptions[1].binding = VERTEX_BUFFER_BIND_ID;
		vertices.attributeDescriptions[1].location = 1;
		vertices.attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
		vertices.attributeDescriptions[1].offset = sizeof(float) * 3;

		// Assign to vertex input state
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// We need to tell the API the number of max. requested descriptors per type
		vk::DescriptorPoolSize typeCounts[1];
		// This example only uses one descriptor type (uniform buffer) and only
		// requests one descriptor of this type
		typeCounts[0].type = vk::DescriptorType::eUniformBuffer;
		typeCounts[0].descriptorCount = 1;
		// For additional types you need to add new entries in the type count list
		// E.g. for two combined image samplers :
		// typeCounts[1].type = vk::DescriptorType::eCombinedImageSampler;
		// typeCounts[1].descriptorCount = 2;

		// Create the global descriptor pool
		// All descriptors used in this example are allocated from this pool
		vk::DescriptorPoolCreateInfo descriptorPoolInfo = {};
		descriptorPoolInfo.sType = vk::StructureType::eDescriptorPoolCreateInfo;
		descriptorPoolInfo.pNext = NULL;
		descriptorPoolInfo.poolSizeCount = 1;
		descriptorPoolInfo.pPoolSizes = typeCounts;
		// Set the max. number of sets that can be requested
		// Requesting descriptors beyond maxSets will result in an error
		descriptorPoolInfo.maxSets = 1;

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		// Setup layout of descriptors used in this example
		// Basically connects the different shader stages to descriptors
		// for binding uniform buffers, image samplers, etc.
		// So every shader binding should map to one descriptor set layout
		// binding

		// Binding 0 : Uniform buffer (Vertex shader)
		vk::DescriptorSetLayoutBinding layoutBinding = {};
		layoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
		layoutBinding.descriptorCount = 1;
		layoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;
		layoutBinding.pImmutableSamplers = NULL;

		vk::DescriptorSetLayoutCreateInfo descriptorLayout = {};
		descriptorLayout.sType = vk::StructureType::eDescriptorSetLayoutCreateInfo;
		descriptorLayout.pNext = NULL;
		descriptorLayout.bindingCount = 1;
		descriptorLayout.pBindings = &layoutBinding;

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, NULL);

		// Create the pipeline layout that is used to generate the rendering pipelines that
		// are based on this descriptor set layout
		// In a more complex scenario you would have different pipeline layouts for different
		// descriptor set layouts that could be reused
		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
		pPipelineLayoutCreateInfo.sType = vk::StructureType::ePipelineLayoutCreateInfo;
		pPipelineLayoutCreateInfo.pNext = NULL;
		pPipelineLayoutCreateInfo.setLayoutCount = 1;
		pPipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;

		pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
	}

	void setupDescriptorSet()
	{
		// Allocate a new descriptor set from the global descriptor pool
		vk::DescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = vk::StructureType::eDescriptorSetAllocateInfo;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptorSetLayout;

		descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

		// Update the descriptor set determining the shader binding points
		// For every binding point used in a shader there needs to be one
		// descriptor set matching that binding point

		vk::WriteDescriptorSet writeDescriptorSet = {};

		// Binding 0 : Uniform buffer
		writeDescriptorSet.sType = vk::StructureType::eWriteDescriptorSet;
		writeDescriptorSet.dstSet = descriptorSet;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.descriptorType = vk::DescriptorType::eUniformBuffer;
		writeDescriptorSet.pBufferInfo = &uniformDataVS.descriptor;
		// Binds this uniform buffer to binding point 0
		writeDescriptorSet.dstBinding = 0;

		device.updateDescriptorSets(writeDescriptorSet, nullptr);
	}

	void preparePipelines()
	{
		// Create our rendering pipeline used in this example
		// Vulkan uses the concept of rendering pipelines to encapsulate
		// fixed states
		// This replaces OpenGL's huge (and cumbersome) state machine
		// A pipeline is then stored and hashed on the GPU making
		// pipeline changes much faster than having to set dozens of 
		// states
		// In a real world application you'd have dozens of pipelines
		// for every shader set used in a scene
		// Note that there are a few states that are not stored with
		// the pipeline. These are called dynamic states and the 
		// pipeline only stores that they are used with this pipeline,
		// but not their states

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo = {};

		pipelineCreateInfo.sType = vk::StructureType::eGraphicsPipelineCreateInfo;
		// The layout used for this pipeline
		pipelineCreateInfo.layout = pipelineLayout;
		// Renderpass this pipeline is attached to
		pipelineCreateInfo.renderPass = renderPass;

		// Vertex input state
		// Describes the topoloy used with this pipeline
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
		inputAssemblyState.sType = vk::StructureType::ePipelineInputAssemblyStateCreateInfo;
		// This pipeline renders vertex data as triangle lists
		inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;

		// Rasterization state
		vk::PipelineRasterizationStateCreateInfo rasterizationState = {};
		rasterizationState.sType = vk::StructureType::ePipelineRasterizationStateCreateInfo;
		// Solid polygon mode
		rasterizationState.polygonMode = vk::PolygonMode::eFill;
		// No culling
		rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
		rasterizationState.frontFace = vk::FrontFace::eCounterClockwise;
		rasterizationState.depthClampEnable = VK_FALSE;
		rasterizationState.rasterizerDiscardEnable = VK_FALSE;
		rasterizationState.depthBiasEnable = VK_FALSE;
		rasterizationState.lineWidth = 1.0f;

		// Color blend state
		// Describes blend modes and color masks
		vk::PipelineColorBlendStateCreateInfo colorBlendState = {};
		colorBlendState.sType = vk::StructureType::ePipelineColorBlendStateCreateInfo;
		// One blend attachment state
		// Blending is not used in this example
		vk::PipelineColorBlendAttachmentState blendAttachmentState[1] = {};
		blendAttachmentState[0].colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		blendAttachmentState[0].blendEnable = VK_FALSE;
		colorBlendState.attachmentCount = 1;
		colorBlendState.pAttachments = blendAttachmentState;

		// Viewport state
		vk::PipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = vk::StructureType::ePipelineViewportStateCreateInfo;
		// One viewport
		viewportState.viewportCount = 1;
		// One scissor rectangle
		viewportState.scissorCount = 1;

		// Enable dynamic states
		// Describes the dynamic states to be used with this pipeline
		// Dynamic states can be set even after the pipeline has been created
		// So there is no need to create new pipelines just for changing
		// a viewport's dimensions or a scissor box
		vk::PipelineDynamicStateCreateInfo dynamicState = {};
		// The dynamic state properties themselves are stored in the command buffer
		std::vector<vk::DynamicState> dynamicStateEnables;
		dynamicStateEnables.push_back(vk::DynamicState::eViewport);
		dynamicStateEnables.push_back(vk::DynamicState::eScissor);
		dynamicState.sType = vk::StructureType::ePipelineDynamicStateCreateInfo;
		dynamicState.pDynamicStates = dynamicStateEnables.data();
		dynamicState.dynamicStateCount = dynamicStateEnables.size();

		// Depth and stencil state
		// Describes depth and stenctil test and compare ops
		vk::PipelineDepthStencilStateCreateInfo depthStencilState = {};
		// Basic depth compare setup with depth writes and depth test enabled
		// No stencil used 
		depthStencilState.sType = vk::StructureType::ePipelineDepthStencilStateCreateInfo;
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
		depthStencilState.depthBoundsTestEnable = VK_FALSE;
		depthStencilState.back.failOp = vk::StencilOp::eKeep;
		depthStencilState.back.passOp = vk::StencilOp::eKeep;
		depthStencilState.back.compareOp = vk::CompareOp::eAlways;
		depthStencilState.stencilTestEnable = VK_FALSE;
		depthStencilState.front = depthStencilState.back;

		// Multi sampling state
		vk::PipelineMultisampleStateCreateInfo multisampleState = {};
		multisampleState.sType = vk::StructureType::ePipelineMultisampleStateCreateInfo;
		multisampleState.pSampleMask = NULL;
		// No multi sampling used in this example
		multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// Load shaders
		// Shaders are loaded from the SPIR-V format, which can be generated from glsl
		std::array<vk::PipelineShaderStageCreateInfo,2> shaderStages;
		shaderStages[0] = loadShader(getAssetPath() + "shaders/triangle.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/triangle.frag.spv", vk::ShaderStageFlagBits::eFragment);

		// Assign states
		// Assign pipeline state create information
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.renderPass = renderPass;
		pipelineCreateInfo.pDynamicState = &dynamicState;

		// Create rendering pipeline
		pipelines.solid = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	void prepareUniformBuffers()
	{
		// Prepare and initialize a uniform buffer block containing shader uniforms
		// In Vulkan there are no more single uniforms like in GL
		// All shader uniforms are passed as uniform buffer blocks 
		vk::MemoryRequirements memReqs;

		// Vertex shader uniform buffer block
		vk::BufferCreateInfo bufferInfo = {};
		vk::MemoryAllocateInfo allocInfo = {};
		allocInfo.sType = vk::StructureType::eMemoryAllocateInfo;
		allocInfo.pNext = NULL;
		allocInfo.allocationSize = 0;
		allocInfo.memoryTypeIndex = 0;

		bufferInfo.sType = vk::StructureType::eBufferCreateInfo;
		bufferInfo.size = sizeof(uboVS);
		bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;

		// Create a new buffer
		uniformDataVS.buffer = device.createBuffer(bufferInfo, nullptr);
		// Get memory requirements including size, alignment and memory type 
		memReqs = device.getBufferMemoryRequirements(uniformDataVS.buffer);
		allocInfo.allocationSize = memReqs.size;
		// Get the memory type index that supports host visibile memory access
		// Most implementations offer multiple memory tpyes and selecting the 
		// correct one to allocate memory from is important
		allocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);
		// Allocate memory for the uniform buffer
		(uniformDataVS.memory) = device.allocateMemory(allocInfo, nullptr);
		// Bind memory to buffer
		device.bindBufferMemory(uniformDataVS.buffer, uniformDataVS.memory, 0);
		
		// Store information in the uniform's descriptor
		uniformDataVS.descriptor.buffer = uniformDataVS.buffer;
		uniformDataVS.descriptor.offset = 0;
		uniformDataVS.descriptor.range = sizeof(uboVS);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// Update matrices
		uboVS.projectionMatrix = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);

		uboVS.viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));

		uboVS.modelMatrix = glm::mat4();
		uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		// Map uniform buffer and update it
		// If you want to keep a handle to the memory and not unmap it afer updating, 
		// create the memory with the vk::MemoryPropertyFlagBits::eHostCoherent 
		void *pData = device.mapMemory(uniformDataVS.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());
		memcpy(pData, &uboVS, sizeof(uboVS));
		device.unmapMemory(uniformDataVS.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		prepareSemaphore();
		prepareVertices(USE_STAGING);
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
		draw();
		vkDeviceWaitIdle(device);
	}

	virtual void viewChanged()
	{
		// Before updating the uniform buffer we want to make
		// sure that the device has finished all operations
		// In a real-world application you would use synchronization
		// objects for this
		vkDeviceWaitIdle(device);
		// This function is called by the base example class 
		// each time the view is changed by user input
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