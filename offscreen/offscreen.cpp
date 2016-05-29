/*
* Vulkan Example - Offscreen rendering using a separate framebuffer
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"


// Texture properties
#define TEX_DIM 512
#define TEX_FORMAT vk::Format::eR8G8B8A8Unorm
#define TEX_FILTER vk::Filter::eLinear

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM
#define FB_COLOR_FORMAT vk::Format::eR8G8B8A8Unorm

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
	bool debugDisplay = false;

	struct {
		vkTools::VulkanTexture colorMap;
	} textures;
	
	struct {
		vkMeshLoader::MeshBuffer example;
		vkMeshLoader::MeshBuffer quad;
		vkMeshLoader::MeshBuffer plane;
	} meshes;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkTools::UniformData vsShared;
		vkTools::UniformData vsMirror;
		vkTools::UniformData vsOffScreen;
		vkTools::UniformData vsDebugQuad;
	} uniformData;

	struct UBO {
		glm::mat4 projection;
		glm::mat4 model;
		glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	};

	struct {
		UBO vsShared;
	} ubos;

	struct {
		vk::Pipeline debug;
		vk::Pipeline shaded;
		vk::Pipeline mirror;
	} pipelines;

	struct {
		vk::PipelineLayout quad;
		vk::PipelineLayout offscreen;
	} pipelineLayouts;

	struct {
		vk::DescriptorSet offscreen;
		vk::DescriptorSet mirror;
		vk::DescriptorSet model;
		vk::DescriptorSet debugQuad;
	} descriptorSets;

	vk::DescriptorSetLayout descriptorSetLayout;

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
		// Texture target for framebugger blut
		vkTools::VulkanTexture textureTarget;
	} offScreenFrameBuf;

	vk::CommandBuffer offScreenCmdBuffer;

	glm::vec3 meshPos = glm::vec3(0.0f, -1.5f, 0.0f);

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -6.5f;
		rotation = { -11.25f, 45.0f, 0.0f };
		timerSpeed *= 0.25f;
		title = "Vulkan Example - Offscreen rendering";
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		// Textures
		textureLoader->destroyTexture(offScreenFrameBuf.textureTarget);
		textureLoader->destroyTexture(textures.colorMap);

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

		device.destroyPipeline(pipelines.debug, nullptr);
		device.destroyPipeline(pipelines.shaded, nullptr);
		device.destroyPipeline(pipelines.mirror, nullptr);

		device.destroyPipelineLayout(pipelineLayouts.quad, nullptr);
		device.destroyPipelineLayout(pipelineLayouts.offscreen, nullptr);

		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		// Meshes
		vkMeshLoader::freeMeshBufferResources(device, &meshes.example);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.quad);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.plane);

		// Uniform buffers
		vkTools::destroyUniformData(device, &uniformData.vsShared);
		vkTools::destroyUniformData(device, &uniformData.vsMirror);
		vkTools::destroyUniformData(device, &uniformData.vsOffScreen);
		vkTools::destroyUniformData(device, &uniformData.vsDebugQuad);

		device.freeCommandBuffers(cmdPool, offScreenCmdBuffer);
	}

	// Preapre an empty texture as the blit target from 
	// the offscreen framebuffer
	void prepareTextureTarget(uint32_t width, uint32_t height, vk::Format format)
	{
		createSetupCommandBuffer();
		// Get device properites for the requested texture format
		vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(format);
		// Check if blit destination is supported for the requested format
		// Only try for optimal tiling, linear tiling usually won't support blit as destination anyway
		assert(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);

		// Prepare blit target texture
		offScreenFrameBuf.textureTarget.width = width;
		offScreenFrameBuf.textureTarget.height = height;

		vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.format = format;
		imageCreateInfo.extent = { width, height, 1 };
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
		imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
		// Texture will be sampled in a shader and is also the blit destination
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;

		vk::MemoryAllocateInfo memAllocInfo;
		vk::MemoryRequirements memReqs;

		offScreenFrameBuf.textureTarget.image = device.createImage(imageCreateInfo, nullptr);
		
		memReqs = device.getImageMemoryRequirements(offScreenFrameBuf.textureTarget.image);
		memAllocInfo.allocationSize = memReqs.size;
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAllocInfo.memoryTypeIndex);
		offScreenFrameBuf.textureTarget.deviceMemory = device.allocateMemory(memAllocInfo, nullptr);
		
		device.bindImageMemory(offScreenFrameBuf.textureTarget.image, offScreenFrameBuf.textureTarget.deviceMemory, 0);
		

		// Image memory barrier
		// Set initial layout for the offscreen texture transfer destination
		// Will be transformed while updating the texture
		offScreenFrameBuf.textureTarget.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		vkTools::setImageLayout(
			setupCmdBuffer, 
			offScreenFrameBuf.textureTarget.image,
			vk::ImageAspectFlagBits::eColor, 
			vk::ImageLayout::ePreinitialized,
			offScreenFrameBuf.textureTarget.imageLayout);

		// Create sampler
		vk::SamplerCreateInfo sampler;
		sampler.magFilter = TEX_FILTER;
		sampler.minFilter = TEX_FILTER;
		sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
		sampler.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 0;
		sampler.compareOp = vk::CompareOp::eNever;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		offScreenFrameBuf.textureTarget.sampler = device.createSampler(sampler, nullptr);
		

		// Create image view
		vk::ImageViewCreateInfo view = {};
		view.sType = vk::StructureType::eImageViewCreateInfo;
		view.pNext = NULL;
		view.image;
		view.viewType = vk::ImageViewType::e2D;
		view.format = format;
		view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		view.image = offScreenFrameBuf.textureTarget.image;
		offScreenFrameBuf.textureTarget.view = device.createImageView(view, nullptr);
		

		flushSetupCommandBuffer();
	}

	// Prepare a new framebuffer for offscreen rendering
	// The contents of this framebuffer are then
	// blitted to our render target
	void prepareOffscreenFramebuffer()
	{

		createSetupCommandBuffer();

		offScreenFrameBuf.width = FB_DIM;
		offScreenFrameBuf.height = FB_DIM;

		vk::Format fbColorFormat = FB_COLOR_FORMAT;

		// Find a suitable depth format
		vk::Format fbDepthFormat = vkTools::getSupportedDepthFormat(physicalDevice);

		// Color attachment
		vk::ImageCreateInfo image;
		image.imageType = vk::ImageType::e2D;
		image.format = fbColorFormat;
		image.extent.width = offScreenFrameBuf.width;
		image.extent.height = offScreenFrameBuf.height;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = vk::SampleCountFlagBits::e1;
		image.tiling = vk::ImageTiling::eOptimal;
		// Image of the framebuffer is blit source
		image.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;

		vk::MemoryAllocateInfo memAlloc;
		vk::MemoryRequirements memReqs;

		vk::ImageViewCreateInfo colorImageView;
		colorImageView.viewType = vk::ImageViewType::e2D;
		colorImageView.format = fbColorFormat;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;

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
		image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;

		vk::ImageViewCreateInfo depthStencilView;
		depthStencilView.viewType = vk::ImageViewType::e2D;
		depthStencilView.format = fbDepthFormat;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
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
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);

		depthStencilView.image = offScreenFrameBuf.depth.image;
		offScreenFrameBuf.depth.view = device.createImageView(depthStencilView, nullptr);
		

		flushSetupCommandBuffer();

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

	void createOffscreenCommandBuffer()
	{
		vk::CommandBufferAllocateInfo cmd = vkTools::initializers::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		offScreenCmdBuffer = device.allocateCommandBuffers(cmd)[0];
	}

	// The command buffer to copy for rendering 
	// the offscreen scene and blitting it into
	// the texture target is only build once
	// and gets resubmitted 
	void buildOffscreenCommandBuffer()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[0].color = { std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf.height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		offScreenCmdBuffer.begin(cmdBufInfo);
		

		offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		vk::Viewport viewport = vkTools::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
		offScreenCmdBuffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkTools::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
		offScreenCmdBuffer.setScissor(0, scissor);

		vk::DeviceSize offsets = 0;

		// Model
		offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.offscreen, nullptr);
		offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.shaded);
		offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buf, offsets);
		offScreenCmdBuffer.bindIndexBuffer(meshes.example.indices.buf, 0, vk::IndexType::eUint32);
		offScreenCmdBuffer.drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);

		offScreenCmdBuffer.endRenderPass();

		// Make sure color writes to the framebuffer are finished before using it as transfer source
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal);

		// Transform texture target to transfer source
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.textureTarget.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageLayout::eTransferDstOptimal);

		// Blit offscreen color buffer to our texture target
		vk::ImageBlit imgBlit;

		imgBlit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		imgBlit.srcSubresource.mipLevel = 0;
		imgBlit.srcSubresource.baseArrayLayer = 0;
		imgBlit.srcSubresource.layerCount = 1;

		imgBlit.srcOffsets[0] = { 0, 0, 0 };
		imgBlit.srcOffsets[1].x = offScreenFrameBuf.width;
		imgBlit.srcOffsets[1].y = offScreenFrameBuf.height;
		imgBlit.srcOffsets[1].z = 1;

		imgBlit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		imgBlit.dstSubresource.mipLevel = 0;
		imgBlit.dstSubresource.baseArrayLayer = 0;
		imgBlit.dstSubresource.layerCount = 1;

		imgBlit.dstOffsets[0] = { 0, 0, 0 };
		imgBlit.dstOffsets[1].x = offScreenFrameBuf.textureTarget.width;
		imgBlit.dstOffsets[1].y = offScreenFrameBuf.textureTarget.height;
		imgBlit.dstOffsets[1].z = 1;

		// Blit from framebuffer image to texture image
		// vkCmdBlitImage does scaling and (if necessary and possible) also does format conversions
		offScreenCmdBuffer.blitImage(
			offScreenFrameBuf.color.image, 
			vk::ImageLayout::eTransferSrcOptimal, 
			offScreenFrameBuf.textureTarget.image, 
			vk::ImageLayout::eTransferDstOptimal, 
			imgBlit, 
			vk::Filter::eLinear);

		// Transform framebuffer color attachment back 
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eColorAttachmentOptimal);

		// Transform texture target back to shader read
		// Makes sure that writes to the textuer are finished before
		// it's accessed in the shader
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.textureTarget.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal);

		offScreenCmdBuffer.end();
		
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

			if (debugDisplay)
			{
				drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.debugQuad, nullptr);
				drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.debug);
				drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buf, offsets);
				drawCmdBuffers[i].bindIndexBuffer(meshes.quad.indices.buf, 0, vk::IndexType::eUint32);
				drawCmdBuffers[i].drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
			}

			// Scene
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.debug);

			// Reflection plane
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.mirror, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.mirror);

			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.plane.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.plane.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].drawIndexed(meshes.plane.indexCount, 1, 0, 0, 0);

			// Model
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.model, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.shaded);

			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.example.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
			
		}
	}

	void draw()
	{
		// Get next image in the swap chain (back/front buffer)
		swapChain.acquireNextImage(semaphores.presentComplete, currentBuffer);
		

		submitPostPresentBarrier(swapChain.buffers[currentBuffer].image);

		// Gather command buffers to be sumitted to the queue
		std::vector<vk::CommandBuffer> submitCmdBuffers = {
			offScreenCmdBuffer,
			drawCmdBuffers[currentBuffer],
		};
		submitInfo.commandBufferCount = submitCmdBuffers.size();
		submitInfo.pCommandBuffers = submitCmdBuffers.data();

		// Submit to queue
		queue.submit(submitInfo, VK_NULL_HANDLE);
		

		submitPrePresentBarrier(swapChain.buffers[currentBuffer].image);

		swapChain.queuePresent(queue, currentBuffer, semaphores.renderComplete);
		

		queue.waitIdle();
		
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/plane.obj", &meshes.plane, vertexLayout, 0.4f);
		loadMesh(getAssetPath() + "models/chinesedragon.dae", &meshes.example, vertexLayout, 0.3f);
	}

	void loadTextures()
	{
		textureLoader->loadTexture(
			getAssetPath() + "textures/darkmetal_bc3.ktx",
			vk::Format::eBc3UnormBlock,
			&textures.colorMap);
	}

	void generateQuad()
	{
		// Setup vertices for a single uv-mapped quad
		struct Vertex {
			float pos[3];
			float uv[2];
			float col[3];
			float normal[3];
		};

#define QUAD_COLOR_NORMAL { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }
		std::vector<Vertex> vertexBuffer =
		{
			{ { 1.0f, 1.0f, 0.0f },{ 1.0f, 1.0f }, QUAD_COLOR_NORMAL },
			{ { 0.0f, 1.0f, 0.0f },{ 0.0f, 1.0f }, QUAD_COLOR_NORMAL },
			{ { 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f }, QUAD_COLOR_NORMAL },
			{ { 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f }, QUAD_COLOR_NORMAL }
		};
#undef QUAD_COLOR_NORMAL
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
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 6),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 8)
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 5);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		// Textured quad pipeline layout
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
				1),
			// Binding 2 : Fragment shader image sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				2)
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);
		

		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayouts.quad = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
		

		// Offscreen pipeline layout
		pipelineLayouts.offscreen = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
		
	}

	void setupDescriptorSet()
	{
		// Mirror plane descriptor set
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSets.mirror = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the offscreen mirror texture
		vk::DescriptorImageInfo texDescriptorMirror =
			vkTools::initializers::descriptorImageInfo(offScreenFrameBuf.textureTarget.sampler, offScreenFrameBuf.textureTarget.view, vk::ImageLayout::eGeneral);

		// Image descriptor for the color map
		vk::DescriptorImageInfo texDescriptorColorMap =
			vkTools::initializers::descriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.mirror,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsMirror.descriptor),
			// Binding 1 : Fragment shader texture sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.mirror,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptorMirror),
			// Binding 2 : Fragment shader texture sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.mirror,
				vk::DescriptorType::eCombinedImageSampler,
				2,
				&texDescriptorColorMap)
		};

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Model
		// No texture
		descriptorSets.model = device.allocateDescriptorSets(allocInfo)[0];

		std::vector<vk::WriteDescriptorSet> modelWriteDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.model,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsShared.descriptor)
		};
		device.updateDescriptorSets(modelWriteDescriptorSets.size(), modelWriteDescriptorSets.data(), 0, NULL);

		// Offscreen
		descriptorSets.offscreen = device.allocateDescriptorSets(allocInfo)[0];

		std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
			descriptorSets.offscreen,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsOffScreen.descriptor)
		};
		device.updateDescriptorSets(offScreenWriteDescriptorSets.size(), offScreenWriteDescriptorSets.data(), 0, NULL);

		// Debug quad
		descriptorSets.debugQuad = device.allocateDescriptorSets(allocInfo)[0];

		std::vector<vk::WriteDescriptorSet> debugQuadWriteDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.debugQuad,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsDebugQuad.descriptor),
			// Binding 1 : Fragment shader texture sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.debugQuad,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptorMirror)
		};
		device.updateDescriptorSets(debugQuadWriteDescriptorSets.size(), debugQuadWriteDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise);

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

		shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/quad.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/quad.frag.spv", vk::ShaderStageFlagBits::eFragment);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(pipelineLayouts.quad, renderPass);

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

		pipelines.debug = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		

		// Mirror
		shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/mirror.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/mirror.frag.spv", vk::ShaderStageFlagBits::eFragment);

		pipelines.mirror = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		

		// Solid shading pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/offscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/offscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);

		pipelineCreateInfo.layout = pipelineLayouts.offscreen;

		pipelines.shaded = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Mesh vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(ubos.vsShared),
			nullptr,
			uniformData.vsShared.buffer,
			uniformData.vsShared.memory,
			uniformData.vsShared.descriptor);

		// Mirror plane vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(ubos.vsShared),
			nullptr,
			uniformData.vsMirror.buffer,
			uniformData.vsMirror.memory,
			uniformData.vsMirror.descriptor);

		// Offscreen vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(ubos.vsShared),
			nullptr,
			uniformData.vsOffScreen.buffer,
			uniformData.vsOffScreen.memory,
			uniformData.vsOffScreen.descriptor);

		// Debug quad vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(ubos.vsShared),
			nullptr,
			uniformData.vsDebugQuad.buffer,
			uniformData.vsDebugQuad.memory,
			uniformData.vsDebugQuad.descriptor);

		updateUniformBuffers();
		updateUniformBufferOffscreen();
	}

	void updateUniformBuffers()
	{
		// Mesh
		ubos.vsShared.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
		glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));

		ubos.vsShared.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(0, 0, 0));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		ubos.vsShared.model = glm::translate(ubos.vsShared.model, meshPos);

		void *pData = device.mapMemory(uniformData.vsShared.memory, 0, sizeof(ubos.vsShared), vk::MemoryMapFlags());
		
		memcpy(pData, &ubos.vsShared, sizeof(ubos.vsShared));
		device.unmapMemory(uniformData.vsShared.memory);

		// Mirror
		ubos.vsShared.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(0, 0, 0));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		pData = device.mapMemory(uniformData.vsMirror.memory, 0, sizeof(ubos.vsShared), vk::MemoryMapFlags());
		
		memcpy(pData, &ubos.vsShared, sizeof(ubos.vsShared));
		device.unmapMemory(uniformData.vsMirror.memory);

		// Debug quad
		ubos.vsShared.projection = glm::ortho(0.0f, 4.0f, 0.0f, 4.0f*(float)height / (float)width, -1.0f, 1.0f);
		ubos.vsShared.model = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, 0.0f));

		pData = device.mapMemory(uniformData.vsDebugQuad.memory, 0, sizeof(ubos.vsShared), vk::MemoryMapFlags());
		
		memcpy(pData, &ubos.vsShared, sizeof(ubos.vsShared));
		device.unmapMemory(uniformData.vsDebugQuad.memory);
	}

	void updateUniformBufferOffscreen()
	{
		ubos.vsShared.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
		glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));

		ubos.vsShared.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(0, 0, 0));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		ubos.vsShared.model = glm::scale(ubos.vsShared.model, glm::vec3(1.0f, -1.0f, 1.0f));
		ubos.vsShared.model = glm::translate(ubos.vsShared.model, meshPos);

		void *pData = device.mapMemory(uniformData.vsOffScreen.memory, 0, sizeof(ubos.vsShared), vk::MemoryMapFlags());
		
		memcpy(pData, &ubos.vsShared, sizeof(ubos.vsShared));
		device.unmapMemory(uniformData.vsOffScreen.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadTextures();
		generateQuad();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		prepareTextureTarget(TEX_DIM, TEX_DIM, TEX_FORMAT);
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		createOffscreenCommandBuffer();
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
			updateUniformBuffers();
			updateUniformBufferOffscreen();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
		updateUniformBufferOffscreen();
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
