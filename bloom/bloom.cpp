/*
* Vulkan Example - Multi pass offscreen rendering (bloom)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"

// Texture properties
#define TEX_DIM 256
#define TEX_FORMAT vk::Format::eR8G8B8A8Unorm
#define TEX_FILTER vk::Filter::eLinear;

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
	bool bloom = true;

	struct {
		vkTools::VulkanTexture cubemap;
	} textures;

	struct {
		vkMeshLoader::MeshBuffer ufo;
		vkMeshLoader::MeshBuffer ufoGlow;
		vkMeshLoader::MeshBuffer skyBox;
		vkMeshLoader::MeshBuffer quad;
	} meshes;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkTools::UniformData vsScene;
		vkTools::UniformData vsFullScreen;
		vkTools::UniformData vsSkyBox;
		vkTools::UniformData fsVertBlur;
		vkTools::UniformData fsHorzBlur;
	} uniformData;

	struct UBO {
		glm::mat4 projection;
		glm::mat4 model;
	};

	struct UBOBlur {
		int32_t texWidth = TEX_DIM;
		int32_t texHeight = TEX_DIM;
		float blurScale = 1.0f;
		float blurStrength = 1.5f;
		uint32_t horizontal;
	};

	struct {
		UBO scene, fullscreen, skyBox;
		UBOBlur vertBlur, horzBlur;
	} ubos;

	struct {
		vk::Pipeline blurVert;
		vk::Pipeline colorPass;
		vk::Pipeline phongPass;
		vk::Pipeline skyBox;
	} pipelines;

	struct {
		vk::PipelineLayout radialBlur;
		vk::PipelineLayout scene;
	} pipelineLayouts;

	struct {
		vk::DescriptorSet scene;
		vk::DescriptorSet verticalBlur;
		vk::DescriptorSet horizontalBlur;
		vk::DescriptorSet skyBox;
	} descriptorSets;

	// Descriptor set layout is shared amongst
	// all descriptor sets
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
		// Texture target for framebuffer blit
		vkTools::VulkanTexture textureTarget;
	} offScreenFrameBuf, offScreenFrameBufB;

	// Used to store commands for rendering and blitting
	// the offscreen scene
	vk::CommandBuffer offScreenCmdBuffer;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -10.25f;
		rotation = { 7.5f, -343.0f, 0.0f };
		timerSpeed *= 0.5f;
		enableTextOverlay = true;
		title = "Vulkan Example - Bloom";
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		// Texture target
		textureLoader->destroyTexture(offScreenFrameBuf.textureTarget);
		textureLoader->destroyTexture(offScreenFrameBufB.textureTarget);

		// Frame buffer
		device.destroyImageView(offScreenFrameBuf.color.view, nullptr);
		device.destroyImage(offScreenFrameBuf.color.image, nullptr);
		device.freeMemory(offScreenFrameBuf.color.mem, nullptr);

		device.destroyImageView(offScreenFrameBuf.depth.view, nullptr);
		device.destroyImage(offScreenFrameBuf.depth.image, nullptr);
		device.freeMemory(offScreenFrameBuf.depth.mem, nullptr);

		device.destroyImageView(offScreenFrameBufB.color.view, nullptr);
		device.destroyImage(offScreenFrameBufB.color.image, nullptr);
		device.freeMemory(offScreenFrameBufB.color.mem, nullptr);

		device.destroyImageView(offScreenFrameBufB.depth.view, nullptr);
		device.destroyImage(offScreenFrameBufB.depth.image, nullptr);
		device.freeMemory(offScreenFrameBufB.depth.mem, nullptr);

		device.destroyFramebuffer(offScreenFrameBuf.frameBuffer, nullptr);
		device.destroyFramebuffer(offScreenFrameBufB.frameBuffer, nullptr);

		device.destroyPipeline(pipelines.blurVert, nullptr);
		device.destroyPipeline(pipelines.phongPass, nullptr);
		device.destroyPipeline(pipelines.colorPass, nullptr);
		device.destroyPipeline(pipelines.skyBox, nullptr);

		device.destroyPipelineLayout(pipelineLayouts.radialBlur, nullptr);
		device.destroyPipelineLayout(pipelineLayouts.scene, nullptr);

		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		// Meshes
		vkMeshLoader::freeMeshBufferResources(device, &meshes.ufo);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.ufoGlow);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.skyBox);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.quad);

		// Uniform buffers
		vkTools::destroyUniformData(device, &uniformData.vsScene);
		vkTools::destroyUniformData(device, &uniformData.vsFullScreen);
		vkTools::destroyUniformData(device, &uniformData.vsSkyBox);
		vkTools::destroyUniformData(device, &uniformData.fsVertBlur);
		vkTools::destroyUniformData(device, &uniformData.fsHorzBlur);

		device.freeCommandBuffers(cmdPool, offScreenCmdBuffer);

		textureLoader->destroyTexture(textures.cubemap);
	}

	// Preapre an empty texture as the blit target from 
	// the offscreen framebuffer
	void prepareTextureTarget(vkTools::VulkanTexture *tex, uint32_t width, uint32_t height, vk::Format format, vk::CommandBuffer cmdBuffer)
	{
		vk::FormatProperties formatProperties;

		// Get device properites for the requested texture format
		formatProperties = physicalDevice.getFormatProperties(format);
		// Check if blit destination is supported for the requested format
		// Only try for optimal tiling, linear tiling usually won't support blit as destination anyway
		assert(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);

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
		// Texture will be sampled in a shader and is also the blit destination
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

		vk::MemoryAllocateInfo memAllocInfo;
		vk::MemoryRequirements memReqs;

		tex->image = device.createImage(imageCreateInfo, nullptr);
		memReqs = device.getImageMemoryRequirements(tex->image);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		(tex->deviceMemory) = device.allocateMemory(memAllocInfo, nullptr);
		device.bindImageMemory(tex->image, tex->deviceMemory, 0);

		tex->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		vkTools::setImageLayout(
			cmdBuffer,
			tex->image,
			vk::ImageAspectFlagBits::eColor, 
			vk::ImageLayout::eUndefined, 
			tex->imageLayout);

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
		tex->sampler = device.createSampler(sampler, nullptr);

		// Create image view
		vk::ImageViewCreateInfo view ;
		view.image;
		view.viewType = vk::ImageViewType::e2D;
		view.format = format;
		view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		view.image = tex->image;
		tex->view = device.createImageView(view, nullptr);
	}

	// Prepare a new framebuffer for offscreen rendering
	// The contents of this framebuffer are then
	// blitted to our render target
	void prepareOffscreenFramebuffer(FrameBuffer *frameBuf, vk::CommandBuffer cmdBuffer)
	{
		frameBuf->width = FB_DIM;
		frameBuf->height = FB_DIM;

		vk::Format fbColorFormat = FB_COLOR_FORMAT;

		// Find a suitable depth format
		vk::Format fbDepthFormat = vkTools::getSupportedDepthFormat(physicalDevice);

		// Color attachment
		vk::ImageCreateInfo image;
		image.imageType = vk::ImageType::e2D;
		image.format = fbColorFormat;
		image.extent.width = frameBuf->width;
		image.extent.height = frameBuf->height;
		image.extent.depth = 1;
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
		colorImageView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.layerCount = 1;

		frameBuf->color.image = device.createImage(image, nullptr);
		memReqs = device.getImageMemoryRequirements(frameBuf->color.image);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		frameBuf->color.mem = device.allocateMemory(memAlloc, nullptr);
		device.bindImageMemory(frameBuf->color.image, frameBuf->color.mem, 0);

		vkTools::setImageLayout(
			cmdBuffer, 
			frameBuf->color.image, 
			vk::ImageAspectFlagBits::eColor, 
			vk::ImageLayout::eUndefined, 
			vk::ImageLayout::eColorAttachmentOptimal);

		colorImageView.image = frameBuf->color.image;
		frameBuf->color.view = device.createImageView(colorImageView, nullptr);

		// Depth stencil attachment
		image.format = fbDepthFormat;
		image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;

		vk::ImageViewCreateInfo depthStencilView;
		depthStencilView.viewType = vk::ImageViewType::e2D;
		depthStencilView.format = fbDepthFormat;
		depthStencilView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.layerCount = 1;

		frameBuf->depth.image = device.createImage(image, nullptr);
		memReqs = device.getImageMemoryRequirements(frameBuf->depth.image);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		frameBuf->depth.mem = device.allocateMemory(memAlloc, nullptr);
		device.bindImageMemory(frameBuf->depth.image, frameBuf->depth.mem, 0);

		vkTools::setImageLayout(
			cmdBuffer,
			frameBuf->depth.image, 
			vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil, 
			vk::ImageLayout::eUndefined, 
			vk::ImageLayout::eDepthStencilAttachmentOptimal);

		depthStencilView.image = frameBuf->depth.image;
		frameBuf->depth.view = device.createImageView(depthStencilView, nullptr);

		vk::ImageView attachments[2];
		attachments[0] = frameBuf->color.view;
		attachments[1] = frameBuf->depth.view;

		vk::FramebufferCreateInfo fbufCreateInfo;
		fbufCreateInfo.renderPass = renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = frameBuf->width;
		fbufCreateInfo.height = frameBuf->height;
		fbufCreateInfo.layers = 1;

		frameBuf->frameBuffer = device.createFramebuffer(fbufCreateInfo, nullptr);
	}

	// Prepare the ping-pong texture targets for the vertical- and horizontal blur
	void prepareTextureTargets()
	{
		vk::CommandBuffer cmdBuffer = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
		prepareTextureTarget(&offScreenFrameBuf.textureTarget, TEX_DIM, TEX_DIM, TEX_FORMAT, cmdBuffer);
		prepareTextureTarget(&offScreenFrameBufB.textureTarget, TEX_DIM, TEX_DIM, TEX_FORMAT, cmdBuffer);
		VulkanExampleBase::flushCommandBuffer(cmdBuffer, queue, true);
	}

	// Prepare the offscreen framebuffers used for the vertical- and horizontal blur 
	void prepareOffscreenFramebuffers()
	{
		vk::CommandBuffer cmdBuffer = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);
		prepareOffscreenFramebuffer(&offScreenFrameBuf, cmdBuffer);
		prepareOffscreenFramebuffer(&offScreenFrameBufB, cmdBuffer);
		VulkanExampleBase::flushCommandBuffer(cmdBuffer, queue, true);
	}

	void createOffscreenCommandBuffer()
	{
		vk::CommandBufferAllocateInfo cmd = vkTools::initializers::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
		offScreenCmdBuffer = device.allocateCommandBuffers(cmd)[0];
	}

	// Render the 3D scene into a texture target
	void buildOffscreenCommandBuffer()
	{
		vk::CommandBufferBeginInfo cmdBufInfo;

		// Horizontal blur
		vk::ClearValue clearValues[2];
		clearValues[0].color = vkTools::initializers::clearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf.height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		offScreenCmdBuffer.begin(cmdBufInfo);

		vk::Viewport viewport = vkTools::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
		offScreenCmdBuffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkTools::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
		offScreenCmdBuffer.setScissor(0, scissor);

		offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);
		offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.phongPass);

		vk::DeviceSize offsets = 0 ;
		offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.ufoGlow.vertices.buf, offsets);
		offScreenCmdBuffer.bindIndexBuffer(meshes.ufoGlow.indices.buf, 0, vk::IndexType::eUint32);
		offScreenCmdBuffer.drawIndexed(meshes.ufoGlow.indexCount, 1, 0, 0, 0);

		offScreenCmdBuffer.endRenderPass();

		// Make sure color writes to the framebuffer are finished before using it as transfer source
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal);

		// Transform texture target to transfer destination
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

		imgBlit.srcOffsets[1].x = offScreenFrameBuf.width;
		imgBlit.srcOffsets[1].y = offScreenFrameBuf.height;
		imgBlit.srcOffsets[1].z = 1;

		imgBlit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		imgBlit.dstSubresource.mipLevel = 0;
		imgBlit.dstSubresource.baseArrayLayer = 0;
		imgBlit.dstSubresource.layerCount = 1;

		imgBlit.dstOffsets[1].x = offScreenFrameBuf.textureTarget.width;
		imgBlit.dstOffsets[1].y = offScreenFrameBuf.textureTarget.height;
		imgBlit.dstOffsets[1].z = 1;

		// Blit from framebuffer image to texture image
		// vkCmdBlitImage does scaling and (if necessary and possible) also does format conversions
		offScreenCmdBuffer.blitImage(offScreenFrameBuf.color.image, vk::ImageLayout::eTransferSrcOptimal, offScreenFrameBuf.textureTarget.image, vk::ImageLayout::eTransferDstOptimal, imgBlit, vk::Filter::eLinear);

		// Transform framebuffer color attachment back 
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eColorAttachmentOptimal);

		// Transform texture target back to shader read
		// Makes sure that writes to the texture are finished before
		// it's accessed in the shader
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.textureTarget.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal);

		// Vertical blur
		// Render the textured quad containing the scene into
		// another offscreen buffer applying a vertical blur
		renderPassBeginInfo.framebuffer = offScreenFrameBufB.frameBuffer;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBufB.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBufB.height;

		viewport.width = offScreenFrameBuf.width;
		viewport.height = offScreenFrameBuf.height;
		offScreenCmdBuffer.setViewport(0, viewport);

		offScreenCmdBuffer.setScissor(0, scissor);

		offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		// Draw horizontally blurred texture 
		offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.radialBlur, 0, descriptorSets.verticalBlur, nullptr);
		offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.blurVert);
		offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buf, offsets);
		offScreenCmdBuffer.bindIndexBuffer(meshes.quad.indices.buf, 0, vk::IndexType::eUint32);
		offScreenCmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);

		offScreenCmdBuffer.endRenderPass();

		// Make sure color writes to the framebuffer are finished before using it as transfer source
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBufB.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal);

		// Transform texture target to transfer destination
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBufB.textureTarget.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageLayout::eTransferDstOptimal);


		// Blit from framebuffer image to texture image
		// vkCmdBlitImage does scaling and (if necessary and possible) also does format conversions
		offScreenCmdBuffer.blitImage(offScreenFrameBufB.color.image, vk::ImageLayout::eTransferSrcOptimal, offScreenFrameBufB.textureTarget.image, vk::ImageLayout::eTransferDstOptimal, imgBlit, vk::Filter::eLinear);

		// Transform framebuffer color attachment back 
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBufB.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eColorAttachmentOptimal);

		// Transform texture target back to shader read
		// Makes sure that writes to the texture are finished before
		// it's accessed in the shader
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBufB.textureTarget.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal);

		offScreenCmdBuffer.end();
	}

	void loadTextures()
	{
		textureLoader->loadCubemap(
			getAssetPath() + "textures/cubemap_space.ktx",
			vk::Format::eR8G8B8A8Unorm,
			&textures.cubemap);
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

			vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			vk::DeviceSize offsets = 0;

			// Skybox 
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.skyBox, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skyBox);

			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.skyBox.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.skyBox.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].drawIndexed(meshes.skyBox.indexCount, 1, 0, 0, 0);
		
			// 3D scene
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.phongPass);

			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.ufo.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.ufo.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].drawIndexed(meshes.ufo.indexCount, 1, 0, 0, 0);

			// Render vertical blurred scene applying a horizontal blur
			if (bloom)
			{
				drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.radialBlur, 0, descriptorSets.horizontalBlur, nullptr);
				drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.blurVert);
				drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buf, offsets);
				drawCmdBuffers[i].bindIndexBuffer(meshes.quad.indices.buf, 0, vk::IndexType::eUint32);
				drawCmdBuffers[i].drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
			}

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
		}

		if (bloom) 
		{
			buildOffscreenCommandBuffer();
		}
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/retroufo.dae", &meshes.ufo, vertexLayout, 0.05f);
		loadMesh(getAssetPath() + "models/retroufo_glow.dae", &meshes.ufoGlow, vertexLayout, 0.05f);
		loadMesh(getAssetPath() + "models/cube.obj", &meshes.skyBox, vertexLayout, 1.0f);
	}

	// Setup vertices for a single uv-mapped quad
	void generateQuad()
	{
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
		// Same for all meshes used in this example
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
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 8),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 6)
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
			// Binding 2 : Framgnet shader image sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eFragment,
				2),
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);

		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = 
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayouts.radialBlur = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);

		// Offscreen pipeline layout
		pipelineLayouts.scene = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);
	}

	void setupDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		// Full screen blur descriptor sets
		// Vertical blur
		descriptorSets.verticalBlur = device.allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo texDescriptorVert =
			vkTools::initializers::descriptorImageInfo(offScreenFrameBuf.textureTarget.sampler, offScreenFrameBuf.textureTarget.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.verticalBlur,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsScene.descriptor),
			// Binding 1 : Fragment shader texture sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.verticalBlur,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptorVert),
			// Binding 2 : Fragment shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.verticalBlur,
				vk::DescriptorType::eUniformBuffer,
				2,
				&uniformData.fsVertBlur.descriptor)
		};

		device.updateDescriptorSets(writeDescriptorSets, nullptr);

		// Horizontal blur
		descriptorSets.horizontalBlur = device.allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo texDescriptorHorz =
			vkTools::initializers::descriptorImageInfo(offScreenFrameBufB.textureTarget.sampler, offScreenFrameBufB.textureTarget.view, vk::ImageLayout::eGeneral);

		writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.horizontalBlur,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsScene.descriptor),
			// Binding 1 : Fragment shader texture sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.horizontalBlur,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptorHorz),
			// Binding 2 : Fragment shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.horizontalBlur,
				vk::DescriptorType::eUniformBuffer,
				2,
				&uniformData.fsHorzBlur.descriptor)
		};

		device.updateDescriptorSets(writeDescriptorSets, nullptr);

		// 3D scene
		descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

		writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.scene,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsFullScreen.descriptor)
		};

		device.updateDescriptorSets(writeDescriptorSets, nullptr);

		// Skybox
		descriptorSets.skyBox = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the cube map texture
		vk::DescriptorImageInfo cubeMapDescriptor =
			vkTools::initializers::descriptorImageInfo(textures.cubemap.sampler, textures.cubemap.view, vk::ImageLayout::eGeneral);

		writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.skyBox,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsSkyBox.descriptor),
			// Binding 1 : Fragment shader texture sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.skyBox,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&cubeMapDescriptor),
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
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);

		vk::PipelineViewportStateCreateInfo viewportState =
			vkTools::initializers::pipelineViewportStateCreateInfo(1, 1);

		vk::PipelineMultisampleStateCreateInfo multisampleState;

		std::vector<vk::DynamicState> dynamicStateEnables = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};
		vk::PipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

		// Vertical gauss blur
		// Load shaders
		shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/gaussblur.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/gaussblur.frag.spv", vk::ShaderStageFlagBits::eFragment);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(pipelineLayouts.radialBlur, renderPass);

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

		// Additive blending
		blendAttachmentState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
		blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;

		pipelines.blurVert = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Phong pass (3D model)
		shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/phongpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/phongpass.frag.spv", vk::ShaderStageFlagBits::eFragment);

		pipelineCreateInfo.layout = pipelineLayouts.scene;
		blendAttachmentState.blendEnable = VK_FALSE;
		depthStencilState.depthWriteEnable = VK_TRUE;

		pipelines.phongPass = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Color only pass (offscreen blur base)
		shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/colorpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/colorpass.frag.spv", vk::ShaderStageFlagBits::eFragment);

		pipelines.colorPass = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Skybox (cubemap
		shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment);
		depthStencilState.depthWriteEnable = VK_FALSE;
		pipelines.skyBox = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Phong and color pass vertex shader uniform buffer
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			sizeof(ubos.scene),
			&ubos.scene,
			uniformData.vsScene.buffer,
			uniformData.vsScene.memory,
			uniformData.vsScene.descriptor);

		// Fullscreen quad display vertex shader uniform buffer
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			sizeof(ubos.fullscreen),
			&ubos.fullscreen,
			uniformData.vsFullScreen.buffer,
			uniformData.vsFullScreen.memory,
			uniformData.vsFullScreen.descriptor);

		// Fullscreen quad fragment shader uniform buffers
		// Vertical blur
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			sizeof(ubos.vertBlur),
			&ubos.vertBlur,
			uniformData.fsVertBlur.buffer,
			uniformData.fsVertBlur.memory,
			uniformData.fsVertBlur.descriptor);
		// Horizontal blur
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			sizeof(ubos.horzBlur),
			&ubos.horzBlur,
			uniformData.fsHorzBlur.buffer,
			uniformData.fsHorzBlur.memory,
			uniformData.fsHorzBlur.descriptor);

		// Skybox
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			sizeof(ubos.skyBox),
			&ubos.skyBox,
			uniformData.vsSkyBox.buffer,
			uniformData.vsSkyBox.memory,
			uniformData.vsSkyBox.descriptor);

		// Intialize uniform buffers
		updateUniformBuffersScene();
		updateUniformBuffersScreen();
	}

	// Update uniform buffers for rendering the 3D scene
	void updateUniformBuffersScene()
	{
		// UFO
		ubos.fullscreen.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
		glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, -1.0f, zoom));

		ubos.fullscreen.model = viewMatrix *
			glm::translate(glm::mat4(), glm::vec3(sin(glm::radians(timer * 360.0f)) * 0.25f, 0.0f, cos(glm::radians(timer * 360.0f)) * 0.25f) + cameraPos);

		ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, -sinf(glm::radians(timer * 360.0f)) * 0.15f, glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(timer * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		void *pData;
		pData = device.mapMemory(uniformData.vsFullScreen.memory, 0, sizeof(ubos.fullscreen), vk::MemoryMapFlags());
		memcpy(pData, &ubos.fullscreen, sizeof(ubos.fullscreen));
		device.unmapMemory(uniformData.vsFullScreen.memory);

		// Skybox
		ubos.skyBox.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);

		ubos.skyBox.model = glm::mat4();
		ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		pData = device.mapMemory(uniformData.vsSkyBox.memory, 0, sizeof(ubos.skyBox), vk::MemoryMapFlags());
		memcpy(pData, &ubos.skyBox, sizeof(ubos.skyBox));
		device.unmapMemory(uniformData.vsSkyBox.memory);
	}

	// Update uniform buffers for the fullscreen quad
	void updateUniformBuffersScreen()
	{
		// Vertex shader
		ubos.scene.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
		ubos.scene.model = glm::mat4();

		void*pData;
		pData = device.mapMemory(uniformData.vsScene.memory, 0, sizeof(ubos.scene), vk::MemoryMapFlags());
		memcpy(pData, &ubos.scene, sizeof(ubos.scene));
		device.unmapMemory(uniformData.vsScene.memory);

		// Fragment shader
		// Vertical
		ubos.vertBlur.horizontal = 0;
		pData = device.mapMemory(uniformData.fsVertBlur.memory, 0, sizeof(ubos.vertBlur), vk::MemoryMapFlags());
		memcpy(pData, &ubos.vertBlur, sizeof(ubos.vertBlur));
		device.unmapMemory(uniformData.fsVertBlur.memory);
		// Horizontal
		ubos.horzBlur.horizontal = 1;
		pData = device.mapMemory(uniformData.fsHorzBlur.memory, 0, sizeof(ubos.horzBlur), vk::MemoryMapFlags());
		memcpy(pData, &ubos.horzBlur, sizeof(ubos.horzBlur));
		device.unmapMemory(uniformData.fsHorzBlur.memory);
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Gather command buffers to be sumitted to the queue
		std::vector<vk::CommandBuffer> submitCmdBuffers;
		// Submit offscreen rendering command buffer 
		// todo : use event to ensure that offscreen result is finished bfore render command buffer is started
		if (bloom)
		{
			submitCmdBuffers.push_back(offScreenCmdBuffer);
		}
		submitCmdBuffers.push_back(drawCmdBuffers[currentBuffer]);
		submitInfo.commandBufferCount = submitCmdBuffers.size();
		submitInfo.pCommandBuffers = submitCmdBuffers.data();

		queue.submit(submitInfo, VK_NULL_HANDLE);

		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadTextures();
		generateQuad();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		prepareTextureTargets();
		prepareOffscreenFramebuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		createOffscreenCommandBuffer(); 
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		if (!paused)
		{
			updateUniformBuffersScene();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBuffersScene();
		updateUniformBuffersScreen();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case 0x6B:
		case GAMEPAD_BUTTON_R1:
			changeBlurScale(0.25f);
			break;
		case 0x6D:
		case GAMEPAD_BUTTON_L1:
			changeBlurScale(-0.25f);
			break;
		case 0x42:
		case GAMEPAD_BUTTON_A:
			toggleBloom();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
#if defined(__ANDROID__)
		textOverlay->addText("Press \"L1/R1\" to change blur scale", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"Button A\" to toggle bloom", 5.0f, 105.0f, VulkanTextOverlay::alignLeft);
#else
		textOverlay->addText("Press \"NUMPAD +/-\" to change blur scale", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		textOverlay->addText("Press \"B\" to toggle bloom", 5.0f, 105.0f, VulkanTextOverlay::alignLeft);
#endif
	}

	void changeBlurScale(float delta)
	{
		ubos.vertBlur.blurScale += delta;
		ubos.horzBlur.blurScale += delta;
		updateUniformBuffersScreen();
	}

	void toggleBloom()
	{
		bloom = !bloom;
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
