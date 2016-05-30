/*
* Vulkan Example - Omni directional shadows using a dynamic cube map
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/



#include "vulkanexamplebase.h"


// Texture properties
#define TEX_DIM 1024
#define TEX_FILTER vk::Filter::eLinear

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM
#define FB_COLOR_FORMAT vk::Format::eR32Sfloat 

// Vertex layout for this example
std::vector<vkMeshLoader::VertexLayout> vertexLayout = 
{
	vkMeshLoader::VERTEX_LAYOUT_POSITION,
	vkMeshLoader::VERTEX_LAYOUT_UV,
	vkMeshLoader::VERTEX_LAYOUT_COLOR,
	vkMeshLoader::VERTEX_LAYOUT_NORMAL
};

class VulkanExample : public VulkanExampleBase
{
public:
	bool displayCubeMap = false;

	float zNear = 0.1f;
	float zFar = 1024.0f;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer skybox;
		vkMeshLoader::MeshBuffer scene;
	} meshes;

	struct {
		vkTools::UniformData scene;
		vkTools::UniformData offscreen;
	} uniformData;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
	} uboVSquad;

	glm::vec4 lightPos = glm::vec4(0.0f, -25.0f, 0.0f, 1.0); 

	struct {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos;
	} uboVSscene;

	struct {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos;
	} uboOffscreenVS;

	struct {
		vk::Pipeline scene;
		vk::Pipeline offscreen;
		vk::Pipeline cubeMap;
	} pipelines;

	struct {
		vk::PipelineLayout scene; 
		vk::PipelineLayout offscreen;
	} pipelineLayouts;

	struct {
		vk::DescriptorSet scene;
		vk::DescriptorSet offscreen;
	} descriptorSets;

	vk::DescriptorSetLayout descriptorSetLayout;

	vkTools::VulkanTexture shadowCubeMap;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		vk::Image image;
		vk::DeviceMemory mem;
		vk::ImageView view;
	};
	struct FrameBuffer {
		int32_t width, height;
		vk::Framebuffer frameBuffer;
		FrameBufferAttachment color, depth;
	} offScreenFrameBuf;

	vk::CommandBuffer offScreenCmdBuffer;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -175.0f;
		zoomSpeed = 10.0f;
		timerSpeed *= 0.25f;
		rotation = { -20.5f, -673.0f, 0.0f };
		title = "Vulkan Example - Point light shadows";
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		// Cube map
		device.destroyImageView(shadowCubeMap.view, nullptr);
		device.destroyImage(shadowCubeMap.image, nullptr);
		device.destroySampler(shadowCubeMap.sampler, nullptr);
		device.freeMemory(shadowCubeMap.deviceMemory, nullptr);

		// Frame buffer

		// Color attachment
		device.destroyImageView(offScreenFrameBuf.color.view, nullptr);
		device.destroyImage(offScreenFrameBuf.color.image, nullptr);
		device.freeMemory(offScreenFrameBuf.color.mem, nullptr);

		// Depth attachment
		device.destroyImageView(offScreenFrameBuf.depth.view, nullptr);
		device.destroyImage(offScreenFrameBuf.depth.image, nullptr);
		device.freeMemory(offScreenFrameBuf.depth.mem, nullptr);

		device.destroyFramebuffer(offScreenFrameBuf.frameBuffer, nullptr);

		// Pipelibes
		device.destroyPipeline(pipelines.scene, nullptr);
		device.destroyPipeline(pipelines.offscreen, nullptr);
		device.destroyPipeline(pipelines.cubeMap, nullptr);

		device.destroyPipelineLayout(pipelineLayouts.scene, nullptr);
		device.destroyPipelineLayout(pipelineLayouts.offscreen, nullptr);

		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		// Meshes
		vkMeshLoader::freeMeshBufferResources(device, &meshes.scene);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.skybox);

		// Uniform buffers
		vkTools::destroyUniformData(device, &uniformData.offscreen);
		vkTools::destroyUniformData(device, &uniformData.scene);

		device.freeCommandBuffers(cmdPool, offScreenCmdBuffer);
	}

	void prepareCubeMap()
	{
		shadowCubeMap.width = TEX_DIM;
		shadowCubeMap.height = TEX_DIM;
		
		// 32 bit float format for higher precision
		vk::Format format = vk::Format::eR32Sfloat;

		// Cube map image description
		vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.format = format;
		imageCreateInfo.extent = vk::Extent3D { shadowCubeMap.width, shadowCubeMap.height, 1 };
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 6;
		imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
		imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
		imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
		imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
		imageCreateInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;

		vk::MemoryAllocateInfo memAllocInfo;
		vk::MemoryRequirements memReqs;

		// Allocate command buffer for image copies and layouts
		vk::CommandBuffer cmdBuffer;
		vk::CommandBufferAllocateInfo cmdBufAlllocatInfo =
			vkTools::initializers::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		cmdBuffer = device.allocateCommandBuffers(cmdBufAlllocatInfo)[0];
		

		vk::CommandBufferBeginInfo cmdBufInfo;
		cmdBuffer.begin(cmdBufInfo);
		

		// Create cube map image
		shadowCubeMap.image = device.createImage(imageCreateInfo, nullptr);
		

		memReqs = device.getImageMemoryRequirements(shadowCubeMap.image);

		memAllocInfo.allocationSize = memReqs.size;

		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAllocInfo.memoryTypeIndex);
		shadowCubeMap.deviceMemory = device.allocateMemory(memAllocInfo, nullptr);
		
		device.bindImageMemory(shadowCubeMap.image, shadowCubeMap.deviceMemory, 0);
		

		// Image barrier for optimal image (target)
		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 6;
		vkTools::setImageLayout(
			cmdBuffer,
			shadowCubeMap.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::ePreinitialized,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			subresourceRange);

		cmdBuffer.end();
		

		vk::Fence nullFence = { VK_NULL_HANDLE };

		// Submit command buffer to graphis queue
		vk::SubmitInfo submitInfo;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdBuffer;

		queue.submit(submitInfo, nullFence);
		

		queue.waitIdle();
		

		// Create sampler
		vk::SamplerCreateInfo sampler;
		sampler.magFilter = TEX_FILTER;
		sampler.minFilter = TEX_FILTER;
		sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
		sampler.addressModeU = vk::SamplerAddressMode::eClampToBorder;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 0;
		sampler.compareOp = vk::CompareOp::eNever;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		shadowCubeMap.sampler = device.createSampler(sampler, nullptr);
		

		// Create image view
		vk::ImageViewCreateInfo view;
		view.image;
		view.viewType = vk::ImageViewType::eCube;
		view.format = format;
		view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		view.subresourceRange.layerCount = 6;
		view.image = shadowCubeMap.image;
		shadowCubeMap.view = device.createImageView(view, nullptr);
		
	}

	// Prepare a new framebuffer for offscreen rendering
	// The contents of this framebuffer are then
	// copied to the different cube map faces
	void prepareOffscreenFramebuffer()
	{
		offScreenFrameBuf.width = FB_DIM;
		offScreenFrameBuf.height = FB_DIM;

		vk::Format fbColorFormat = FB_COLOR_FORMAT;

		// Find a suitable depth format
		vk::Format fbDepthFormat = vkTools::getSupportedDepthFormat(physicalDevice);

		createSetupCommandBuffer();

		// Color attachment
		vk::ImageCreateInfo image;
		image.imageType = vk::ImageType::e2D;
		image.format = fbColorFormat;
		image.extent.width = offScreenFrameBuf.width;
		image.extent.height = offScreenFrameBuf.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = vk::SampleCountFlagBits::e1;
		image.tiling = vk::ImageTiling::eOptimal;
		// Image of the framebuffer is blit source
		image.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;

		vk::MemoryAllocateInfo memAlloc;

		vk::ImageViewCreateInfo colorImageView;
		colorImageView.viewType = vk::ImageViewType::e2D;
		colorImageView.format = fbColorFormat;
		colorImageView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.layerCount = 1;

		vk::MemoryRequirements memReqs;

		offScreenFrameBuf.color.image = device.createImage(image, nullptr);
		
		memReqs = device.getImageMemoryRequirements(offScreenFrameBuf.color.image);
		memAlloc.allocationSize = memReqs.size;
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAlloc.memoryTypeIndex);
		offScreenFrameBuf.color.mem = device.allocateMemory(memAlloc, nullptr);
		

		device.bindImageMemory(offScreenFrameBuf.color.image, offScreenFrameBuf.color.mem, 0);
		
		vkTools::setImageLayout(
			setupCmdBuffer, 
			offScreenFrameBuf.color.image, 
			vk::ImageAspectFlagBits::eColor, 
			vk::ImageLayout::eUndefined, 
			vk::ImageLayout::eColorAttachmentOptimal);

		colorImageView.image = offScreenFrameBuf.color.image;
		offScreenFrameBuf.color.view = device.createImageView(colorImageView, nullptr);
		

		// Depth stencil attachment
		image.format = fbDepthFormat;
		image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc;
		image.initialLayout = vk::ImageLayout::ePreinitialized;

		vk::ImageViewCreateInfo depthStencilView;
		depthStencilView.viewType = vk::ImageViewType::e2D;
		depthStencilView.format = fbDepthFormat;
		depthStencilView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.layerCount = 1;

		offScreenFrameBuf.depth.image = device.createImage(image, nullptr);
		
		memReqs = device.getImageMemoryRequirements(offScreenFrameBuf.depth.image);
		memAlloc.allocationSize = memReqs.size;
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAlloc.memoryTypeIndex);
		offScreenFrameBuf.depth.mem = device.allocateMemory(memAlloc, nullptr);
		

		device.bindImageMemory(offScreenFrameBuf.depth.image, offScreenFrameBuf.depth.mem, 0);
		

		vkTools::setImageLayout(
			setupCmdBuffer, 
			offScreenFrameBuf.depth.image, 
			vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
			vk::ImageLayout::ePreinitialized,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);

		flushSetupCommandBuffer();

		depthStencilView.image = offScreenFrameBuf.depth.image;
		offScreenFrameBuf.depth.view = device.createImageView(depthStencilView, nullptr);
		

		vk::ImageView attachments[2];
		attachments[0] = offScreenFrameBuf.color.view;
		attachments[1] = offScreenFrameBuf.depth.view;

		vk::FramebufferCreateInfo fbufCreateInfo;
		fbufCreateInfo.renderPass = renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = offScreenFrameBuf.width;
		fbufCreateInfo.height = offScreenFrameBuf.height;
		fbufCreateInfo.layers = 1;

		offScreenFrameBuf.frameBuffer = device.createFramebuffer(fbufCreateInfo, nullptr);
		
	}

	// Updates a single cube map face
	// Renders the scene with face's view and does 
	// a copy from framebuffer to cube face
	// Uses push constants for quick update of
	// view matrix for the current cube map face
	void updateCubeFace(uint32_t faceIndex)
	{
		vk::ClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		// Reuse render pass from example pass
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf.height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		// Update view matrix via push constant

		glm::mat4 viewMatrix = glm::mat4();
		switch (faceIndex)
		{
		case 0: // POSITIVE_X
			viewMatrix = glm::rotate(viewMatrix, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 1:	// NEGATIVE_X
			viewMatrix = glm::rotate(viewMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 2:	// POSITIVE_Y
			viewMatrix = glm::rotate(viewMatrix, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 3:	// NEGATIVE_Y
			viewMatrix = glm::rotate(viewMatrix, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 4:	// POSITIVE_Z
			viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			break;
		case 5:	// NEGATIVE_Z
			viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			break;
		}

		// Render scene from cube face's point of view
		offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		// Update shader push constant block
		// Contains current face view matrix
		offScreenCmdBuffer.pushConstants(pipelineLayouts.offscreen, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &viewMatrix);

		offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.offscreen);
		offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.offscreen, nullptr);

		vk::DeviceSize offsets = 0;
		offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.scene.vertices.buf, offsets);
		offScreenCmdBuffer.bindIndexBuffer(meshes.scene.indices.buf, 0, vk::IndexType::eUint32);
		offScreenCmdBuffer.drawIndexed(meshes.scene.indexCount, 1, 0, 0, 0);

		offScreenCmdBuffer.endRenderPass();
		// Make sure color writes to the framebuffer are finished before using it as transfer source
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal);

		// Copy region for transfer from framebuffer to cube face
		vk::ImageCopy copyRegion;

		copyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		copyRegion.srcSubresource.baseArrayLayer = 0;
		copyRegion.srcSubresource.layerCount = 1;

		copyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		copyRegion.dstSubresource.baseArrayLayer = faceIndex;
		copyRegion.dstSubresource.layerCount = 1;

		copyRegion.extent.width = shadowCubeMap.width;
		copyRegion.extent.height = shadowCubeMap.height;
		copyRegion.extent.depth = 1;

		// Put image copy into command buffer
		offScreenCmdBuffer.copyImage(offScreenFrameBuf.color.image, vk::ImageLayout::eTransferSrcOptimal, shadowCubeMap.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

		// Transform framebuffer color attachment back 
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eColorAttachmentOptimal);
	}

	// Command buffer for rendering and copying all cube map faces
	void buildOffscreenCommandBuffer()
	{
		// Create separate command buffer for offscreen 
		// rendering
		if (!offScreenCmdBuffer)
		{
			vk::CommandBufferAllocateInfo cmd = vkTools::initializers::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
			offScreenCmdBuffer = device.allocateCommandBuffers(cmd)[0];
		}

		vk::CommandBufferBeginInfo cmdBufInfo;

		offScreenCmdBuffer.begin(cmdBufInfo);
		

		vk::Viewport viewport = vkTools::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
		offScreenCmdBuffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkTools::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
		offScreenCmdBuffer.setScissor(0, scissor);

		vk::ImageSubresourceRange subresourceRange;
		subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 6;

		// Change image layout for all cubemap faces to transfer destination
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			shadowCubeMap.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageLayout::eTransferDstOptimal,
			subresourceRange);

		for (uint32_t face = 0; face < 6; ++face)
		{
			updateCubeFace(face);
		}

		// Change image layout for all cubemap faces to shader read after they have been copied
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			shadowCubeMap.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			subresourceRange);

		offScreenCmdBuffer.end();
		
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
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			drawCmdBuffers[i].begin(cmdBufInfo);
			

			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			vk::DeviceSize offsets = 0;

			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);

			if (displayCubeMap)
			{
				drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.cubeMap);
				drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.skybox.vertices.buf, offsets);
				drawCmdBuffers[i].bindIndexBuffer(meshes.skybox.indices.buf, 0, vk::IndexType::eUint32);
				drawCmdBuffers[i].drawIndexed(meshes.skybox.indexCount, 1, 0, 0, 0);
			}
			else
			{
				drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.scene);
				drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.scene.vertices.buf, offsets);
				drawCmdBuffers[i].bindIndexBuffer(meshes.scene.indices.buf, 0, vk::IndexType::eUint32);
				drawCmdBuffers[i].drawIndexed(meshes.scene.indexCount, 1, 0, 0, 0);
			}

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
			
		}
	}

	void draw()
	{
		// Get next image in the swap chain (back/front buffer)
		prepareFrame();
		// Gather command buffers to be sumitted to the queue
		std::vector<vk::CommandBuffer> submitCmdBuffers = {
			offScreenCmdBuffer,
			drawCmdBuffers[currentBuffer],
		};
		submitInfo.commandBufferCount = submitCmdBuffers.size();
		submitInfo.pCommandBuffers = submitCmdBuffers.data();

		// Submit to queue
		queue.submit(submitInfo, VK_NULL_HANDLE);
		

		submitFrame();
		
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/cube.obj", &meshes.skybox, vertexLayout, 2.0f);
		loadMesh(getAssetPath() + "models/shadowscene_fire.dae", &meshes.scene, vertexLayout, 2.0f);
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
		// Location 1 : Texture coordinates
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32Sfloat, sizeof(float) * 3);
		// Location 2 : Color
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32Sfloat, sizeof(float) * 5);
		// Location 3 : Normal
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
		// Example uses three ubos and two image samplers
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 3),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 3);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		// Shared pipeline layout
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eVertex,
				0),
			// Binding 1 : Fragment shader image sampler (cube map)
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				1)
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);
		

		// 3D scene pipeline layout
		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayouts.scene = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
		

		// Offscreen pipeline layout
		// Push constants for cube map face view matrices
		vk::PushConstantRange pushConstantRange =
			vkTools::initializers::pushConstantRange(vk::ShaderStageFlagBits::eVertex, sizeof(glm::mat4), 0);

		// Push constant ranges are part of the pipeline layout
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		pipelineLayouts.offscreen = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
		
	}

	void setupDescriptorSets()
	{
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		// 3D scene
		descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the cube map 
		vk::DescriptorImageInfo texDescriptor =
			vkTools::initializers::descriptorImageInfo(shadowCubeMap.sampler, shadowCubeMap.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> sceneDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
			descriptorSets.scene,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.scene.descriptor),
			// Binding 1 : Fragment shader shadow sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.scene,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptor)
		};
		device.updateDescriptorSets(sceneDescriptorSets.size(), sceneDescriptorSets.data(), 0, NULL);

		// Offscreen
		descriptorSets.offscreen = device.allocateDescriptorSets(allocInfo)[0];

		std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.offscreen,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.offscreen.descriptor),
		};
		device.updateDescriptorSets(offScreenWriteDescriptorSets.size(), offScreenWriteDescriptorSets.data(), 0, NULL);
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

		// 3D scene pipeline
		// Load shaders
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapomni/scene.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapomni/scene.frag.spv", vk::ShaderStageFlagBits::eFragment);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(pipelineLayouts.scene, renderPass);

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

		pipelines.scene = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		

		// Cube map display pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapomni/cubemapdisplay.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapomni/cubemapdisplay.frag.spv", vk::ShaderStageFlagBits::eFragment);
		rasterizationState.cullMode = vk::CullModeFlagBits::eFront;
		pipelines.cubeMap = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		

		// Offscreen pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapomni/offscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapomni/offscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);
		rasterizationState.cullMode = vk::CullModeFlagBits::eBack;
		pipelineCreateInfo.layout = pipelineLayouts.offscreen;
		pipelines.offscreen = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Offscreen vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboOffscreenVS),
			&uboOffscreenVS,
			uniformData.offscreen.buffer,
			uniformData.offscreen.memory,
			uniformData.offscreen.descriptor);

		// 3D scene
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVSscene),
			&uboVSscene,
			uniformData.scene.buffer,
			uniformData.scene.memory,
			uniformData.scene.descriptor);

		updateUniformBufferOffscreen();
		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// 3D scene
		uboVSscene.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, zNear, zFar);
		uboVSscene.view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, displayCubeMap ? 0.0f : zoom));

		uboVSscene.model = glm::mat4();
		uboVSscene.model = glm::rotate(uboVSscene.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVSscene.model = glm::rotate(uboVSscene.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVSscene.model = glm::rotate(uboVSscene.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uboVSscene.lightPos = lightPos;

		void *pData = device.mapMemory(uniformData.scene.memory, 0, sizeof(uboVSscene), vk::MemoryMapFlags());
		
		memcpy(pData, &uboVSscene, sizeof(uboVSscene));
		device.unmapMemory(uniformData.scene.memory);
	}

	void updateUniformBufferOffscreen()
	{
		lightPos.x = sin(glm::radians(timer * 360.0f)) * 1.0f;
		lightPos.z = cos(glm::radians(timer * 360.0f)) * 1.0f;

		uboOffscreenVS.projection = glm::perspective((float)(M_PI / 2.0), 1.0f, zNear, zFar);

		uboOffscreenVS.view = glm::mat4();
		uboOffscreenVS.model = glm::translate(glm::mat4(), glm::vec3(-lightPos.x, -lightPos.y, -lightPos.z));

		uboOffscreenVS.lightPos = lightPos;

		void *pData = device.mapMemory(uniformData.offscreen.memory, 0, sizeof(uboOffscreenVS), vk::MemoryMapFlags());
		memcpy(pData, &uboOffscreenVS, sizeof(uboOffscreenVS));
		device.unmapMemory(uniformData.offscreen.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		prepareCubeMap();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSets();
		prepareOffscreenFramebuffer();
		buildCommandBuffers();
		buildOffscreenCommandBuffer();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		vkDeviceWaitIdle(device);
		draw();
		vkDeviceWaitIdle(device);
		if (!paused)
		{
			updateUniformBufferOffscreen();
			updateUniformBuffers();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBufferOffscreen();
		updateUniformBuffers();
	}

	void toggleCubeMapDisplay()
	{
		displayCubeMap = !displayCubeMap;
		reBuildCommandBuffers();
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
			case 0x44:
				vulkanExample->toggleCubeMapDisplay();
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
