/*
* Vulkan Example - Deferred shading multiple render targets (aka G-Buffer) example
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
	bool debugDisplay = true;

	struct {
		vkTools::VulkanTexture colorMap;
	} textures;

	struct {
		vkMeshLoader::MeshBuffer example;
		vkMeshLoader::MeshBuffer quad;
	} meshes;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
	} uboVS, uboOffscreenVS;

	struct Light {
		glm::vec4 position;
		glm::vec4 color;
		float radius;
		float quadraticFalloff;
		float linearFalloff;
		float _pad;
	};

	struct {
		Light lights[5];
		glm::vec4 viewPos;
	} uboFragmentLights;

	struct {
		vkTools::UniformData vsFullScreen;
		vkTools::UniformData vsOffscreen;
		vkTools::UniformData fsLights;
	} uniformData;

	struct {
		vk::Pipeline deferred;
		vk::Pipeline offscreen;
		vk::Pipeline debug;
	} pipelines;

	struct {
		vk::PipelineLayout deferred; 
		vk::PipelineLayout offscreen;
	} pipelineLayouts;

	struct {
		vk::DescriptorSet offscreen;
	} descriptorSets;

	vk::DescriptorSet descriptorSet;
	vk::DescriptorSetLayout descriptorSetLayout;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		vk::Image image;
		vk::DeviceMemory mem;
		vk::ImageView view;
		vk::Format format;
	};
	struct FrameBuffer {
		int32_t width, height;
		vk::Framebuffer frameBuffer;		
		FrameBufferAttachment position, normal, albedo;
		FrameBufferAttachment depth;
		vk::RenderPass renderPass;
	} offScreenFrameBuf;
	
	// Texture targets
	struct {
		vkTools::VulkanTexture position;
		vkTools::VulkanTexture normal;
		vkTools::VulkanTexture albedo;
	} textureTargets;

	vk::CommandBuffer offScreenCmdBuffer;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -8.0f;
		rotation = { 0.0f, 0.0f, 0.0f };
		width = 1024;
		height = 1024;
		title = "Vulkan Example - Deferred shading";
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		// Texture targets
		textureLoader->destroyTexture(textureTargets.position);
		textureLoader->destroyTexture(textureTargets.normal);
		textureLoader->destroyTexture(textureTargets.albedo);

		// Frame buffer

		// Color attachments
		device.destroyImageView(offScreenFrameBuf.position.view);
		device.destroyImage(offScreenFrameBuf.position.image);
		device.freeMemory(offScreenFrameBuf.position.mem);

		device.destroyImageView(offScreenFrameBuf.normal.view);
		device.destroyImage(offScreenFrameBuf.normal.image);
		device.freeMemory(offScreenFrameBuf.normal.mem);

		device.destroyImageView(offScreenFrameBuf.albedo.view);
		device.destroyImage(offScreenFrameBuf.albedo.image);
		device.freeMemory(offScreenFrameBuf.albedo.mem);

		// Depth attachment
		device.destroyImageView(offScreenFrameBuf.depth.view);
		device.destroyImage(offScreenFrameBuf.depth.image);
		device.freeMemory(offScreenFrameBuf.depth.mem);

		device.destroyFramebuffer(offScreenFrameBuf.frameBuffer);

		device.destroyPipeline(pipelines.deferred);
		device.destroyPipeline(pipelines.offscreen);
		device.destroyPipeline(pipelines.debug);

		device.destroyPipelineLayout(pipelineLayouts.deferred);
		device.destroyPipelineLayout(pipelineLayouts.offscreen);

		device.destroyDescriptorSetLayout(descriptorSetLayout);

		// Meshes
		vkMeshLoader::freeMeshBufferResources(device, &meshes.example);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.quad);

		// Uniform buffers
		vkTools::destroyUniformData(device, &uniformData.vsOffscreen);
		vkTools::destroyUniformData(device, &uniformData.vsFullScreen);
		vkTools::destroyUniformData(device, &uniformData.fsLights);

		device.freeCommandBuffers(cmdPool, offScreenCmdBuffer);

		device.destroyRenderPass(offScreenFrameBuf.renderPass);

		textureLoader->destroyTexture(textures.colorMap);
	}

	// Preapre an empty texture as the blit target from 
	// the offscreen framebuffer
	void prepareTextureTarget(vkTools::VulkanTexture *target, vk::Format format)
	{
		vk::FormatProperties formatProperties;

		uint32_t width = TEX_DIM;
		uint32_t height = TEX_DIM;

		// Prepare blit target texture
		target->width = width;
		target->height = height;

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

		target->image = device.createImage(imageCreateInfo);
		
		memReqs = device.getImageMemoryRequirements(target->image);
		memAllocInfo.allocationSize = memReqs.size;
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAllocInfo.memoryTypeIndex);
		target->deviceMemory = device.allocateMemory(memAllocInfo);
		
		device.bindImageMemory(target->image, target->deviceMemory, 0);
		

		// Image memory barrier
		// Set initial layout for the offscreen texture to shader read
		// Will be transformed while updating the texture
		textureTargets.position.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		vkTools::setImageLayout(
			setupCmdBuffer, 
			target->image,
			vk::ImageAspectFlagBits::eColor, 
			vk::ImageLayout::eUndefined, 
			textureTargets.position.imageLayout);

		// Create sampler
		vk::SamplerCreateInfo sampler;
		sampler.magFilter = TEX_FILTER;
		sampler.minFilter = TEX_FILTER;
		sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
		sampler.addressModeU = vk::SamplerAddressMode::eClampToBorder;
		sampler.addressModeV = sampler.addressModeV;
		sampler.addressModeW = sampler.addressModeV;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 0;
		sampler.compareOp = vk::CompareOp::eNever;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		target->sampler = device.createSampler(sampler);
		

		// Create image view
		vk::ImageViewCreateInfo view;
		view.image;
		view.viewType = vk::ImageViewType::e2D;
		view.format = format;
		view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		view.image = target->image;
		target->view = device.createImageView(view);
		
	}

	void prepareTextureTargets()
	{
		createSetupCommandBuffer();

		prepareTextureTarget(&textureTargets.position, vk::Format::eR16G16B16A16Sfloat);
		prepareTextureTarget(&textureTargets.normal, vk::Format::eR16G16B16A16Sfloat);
		prepareTextureTarget(&textureTargets.albedo, vk::Format::eR8G8B8A8Unorm);

		flushSetupCommandBuffer();
	}

	// Create a frame buffer attachment
	void createAttachment(
		vk::Format format,  
		vk::ImageUsageFlags usage,
		FrameBufferAttachment &attachment)
	{
		vk::ImageAspectFlags aspectMask;
		vk::ImageLayout imageLayout;

		attachment.format = format;

		if (usage & vk::ImageUsageFlagBits::eColorAttachment)
		{
			aspectMask = vk::ImageAspectFlagBits::eColor;
			imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
		}
		if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)
		{
			aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
			imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		}

		assert(aspectMask != vk::ImageAspectFlags());

		vk::ImageCreateInfo image;
		image.imageType = vk::ImageType::e2D;
		image.format = format;
		image.extent.width = offScreenFrameBuf.width;
		image.extent.height = offScreenFrameBuf.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = vk::SampleCountFlagBits::e1;
		image.tiling = vk::ImageTiling::eOptimal;
		image.usage = usage | vk::ImageUsageFlagBits::eTransferSrc;

		vk::MemoryAllocateInfo memAlloc;

		vk::ImageViewCreateInfo imageView;
		imageView.viewType = vk::ImageViewType::e2D;
		imageView.format = format;
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.layerCount = 1;

		vk::MemoryRequirements memReqs;

		attachment.image = device.createImage(image);
		
		memReqs = device.getImageMemoryRequirements(attachment.image);
		memAlloc.allocationSize = memReqs.size;
		getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAlloc.memoryTypeIndex);
		attachment.mem = device.allocateMemory(memAlloc);
		

		device.bindImageMemory(attachment.image, attachment.mem, 0);
		
		
		vkTools::setImageLayout(
			setupCmdBuffer,
			attachment.image,
			aspectMask,
			vk::ImageLayout::eUndefined,
			imageLayout);

		imageView.image = attachment.image;
		attachment.view = device.createImageView(imageView);
		
	}

	// Prepare a new framebuffer for offscreen rendering
	// The contents of this framebuffer are then
	// blitted to our render target
	void prepareOffscreenFramebuffer()
	{
		offScreenFrameBuf.width = FB_DIM;
		offScreenFrameBuf.height = FB_DIM;

		// Color attachments

		// (World space) Positions
		createAttachment(
			vk::Format::eR16G16B16A16Sfloat,
			vk::ImageUsageFlagBits::eColorAttachment,
			offScreenFrameBuf.position);

		// (World space) Normals
		createAttachment(
			vk::Format::eR16G16B16A16Sfloat,
			vk::ImageUsageFlagBits::eColorAttachment,
			offScreenFrameBuf.normal);

		// Albedo (color)
		createAttachment(
			vk::Format::eR8G8B8A8Unorm,
			vk::ImageUsageFlagBits::eColorAttachment,
			offScreenFrameBuf.albedo);

		// Depth attachment

		// Find a suitable depth format
		vk::Format attDepthFormat  = vkTools::getSupportedDepthFormat(physicalDevice);;

		createAttachment(
			attDepthFormat,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			offScreenFrameBuf.depth);

		// Set up separate renderpass with references
		// to the color and depth attachments

		std::array<vk::AttachmentDescription, 4> attachmentDescs = {};

		// Init attachment properties
		for (uint32_t i = 0; i < 4; ++i)
		{
			attachmentDescs[i].samples = vk::SampleCountFlagBits::e1;
			attachmentDescs[i].loadOp = vk::AttachmentLoadOp::eClear;
			attachmentDescs[i].storeOp = vk::AttachmentStoreOp::eStore;
			attachmentDescs[i].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			attachmentDescs[i].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			if (i == 3)
			{
				attachmentDescs[i].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
				attachmentDescs[i].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			}
			else
			{
				attachmentDescs[i].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
				attachmentDescs[i].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
			}
		}

		// Formats
		attachmentDescs[0].format = offScreenFrameBuf.position.format;
		attachmentDescs[1].format = offScreenFrameBuf.normal.format;
		attachmentDescs[2].format = offScreenFrameBuf.albedo.format;
		attachmentDescs[3].format = offScreenFrameBuf.depth.format;

		std::vector<vk::AttachmentReference> colorReferences;
		colorReferences.push_back({ 0, vk::ImageLayout::eColorAttachmentOptimal });
		colorReferences.push_back({ 1, vk::ImageLayout::eColorAttachmentOptimal });
		colorReferences.push_back({ 2, vk::ImageLayout::eColorAttachmentOptimal });

		vk::AttachmentReference depthReference;
		depthReference.attachment = 3;
		depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.pColorAttachments = colorReferences.data();
		subpass.colorAttachmentCount = colorReferences.size();
		subpass.pDepthStencilAttachment = &depthReference;

		vk::RenderPassCreateInfo renderPassInfo;
		renderPassInfo.pAttachments = attachmentDescs.data();
		renderPassInfo.attachmentCount = attachmentDescs.size();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
	
		offScreenFrameBuf.renderPass = device.createRenderPass(renderPassInfo);
		
	
		std::array<vk::ImageView,4> attachments;
		attachments[0] = offScreenFrameBuf.position.view;
		attachments[1] = offScreenFrameBuf.normal.view;
		attachments[2] = offScreenFrameBuf.albedo.view;
		// depth
		attachments[3] = offScreenFrameBuf.depth.view;

		vk::FramebufferCreateInfo fbufCreateInfo;
		fbufCreateInfo.renderPass = offScreenFrameBuf.renderPass;
		fbufCreateInfo.pAttachments = attachments.data();
		fbufCreateInfo.attachmentCount = attachments.size();
		fbufCreateInfo.width = offScreenFrameBuf.width;
		fbufCreateInfo.height = offScreenFrameBuf.height;
		fbufCreateInfo.layers = 1;

		offScreenFrameBuf.frameBuffer = device.createFramebuffer(fbufCreateInfo);
		

		flushSetupCommandBuffer();
		createSetupCommandBuffer();
	}

	// Blit frame buffer attachment to texture target
	void blit(vk::Image source, vk::Image dest)
	{
		// Image memory barrier
		// Transform frame buffer color attachment to transfer source layout
		// Makes sure that writes to the color attachment are finished before
		// using it as source for the blit
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			source,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal);

		// Image memory barrier
		// Transform texture from shader read (initial layout) to transfer destination layout
		// Makes sure that reads from texture are finished before
		// using it as a transfer destination for the blit
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			dest,
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

		imgBlit.dstOffsets[1].x = textureTargets.position.width;
		imgBlit.dstOffsets[1].y = textureTargets.position.height;
		imgBlit.dstOffsets[1].z = 1;

		// Blit from framebuffer image to texture image
		// vkCmdBlitImage does scaling and (if necessary and possible) also does format conversions
		offScreenCmdBuffer.blitImage(source, vk::ImageLayout::eTransferSrcOptimal, dest, vk::ImageLayout::eTransferDstOptimal, imgBlit, vk::Filter::eLinear);

		// Image memory barrier
		// Transform texture from transfer destination to shader read
		// Makes sure that writes to the texture are finished before
		// using it as the source for a sampler in the shader
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			dest,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal);

		// Image memory barrier
		// Transform the framebuffer color attachment back
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			source,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eColorAttachmentOptimal);
	}

	// Build command buffer for rendering the scene to the offscreen frame buffer 
	// and blitting it to the different texture targets
	void buildDeferredCommandBuffer()
	{
		// Create separate command buffer for offscreen 
		// rendering
		if (!offScreenCmdBuffer)
		{
			vk::CommandBufferAllocateInfo cmd = vkTools::initializers::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
			offScreenCmdBuffer = device.allocateCommandBuffers(cmd)[0];
		}

		vk::CommandBufferBeginInfo cmdBufInfo;

		// Clear values for all attachments written in the fragment sahder
		std::array<vk::ClearValue,4> clearValues;
		clearValues[0].color = vkTools::initializers::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[1].color = vkTools::initializers::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[2].color = vkTools::initializers::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
		clearValues[3].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass =  offScreenFrameBuf.renderPass;
		renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf.height;
		renderPassBeginInfo.clearValueCount = clearValues.size();
		renderPassBeginInfo.pClearValues = clearValues.data();

		offScreenCmdBuffer.begin(cmdBufInfo);
		

		offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		vk::Viewport viewport = vkTools::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
		offScreenCmdBuffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkTools::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
		offScreenCmdBuffer.setScissor(0, scissor);

		offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.offscreen, nullptr);
		offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.offscreen);

		vk::DeviceSize offsets = { 0 };
		offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buf, offsets);
		offScreenCmdBuffer.bindIndexBuffer(meshes.example.indices.buf, 0, vk::IndexType::eUint32);
		offScreenCmdBuffer.drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);

		offScreenCmdBuffer.endRenderPass();

		blit(offScreenFrameBuf.position.image, textureTargets.position.image);
		blit(offScreenFrameBuf.normal.image, textureTargets.normal.image);
		blit(offScreenFrameBuf.albedo.image, textureTargets.albedo.image);

		offScreenCmdBuffer.end();
		
	}

	void loadTextures()
	{
		textureLoader->loadTexture(
			getAssetPath() + "models/armor/colormap.ktx",
			vk::Format::eBc3UnormBlock,
			&textures.colorMap);
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
			

			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			vk::DeviceSize offsets { 0 };
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.deferred, 0, descriptorSet, nullptr);

			if (debugDisplay)
			{
				drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.debug);
				drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buf, offsets);
				drawCmdBuffers[i].bindIndexBuffer(meshes.quad.indices.buf, 0, vk::IndexType::eUint32);
				drawCmdBuffers[i].drawIndexed(meshes.quad.indexCount, 1, 0, 0, 1);
				// Move viewport to display final composition in lower right corner
				viewport.x = viewport.width * 0.5f;
				viewport.y = viewport.height * 0.5f;
				drawCmdBuffers[i].setViewport(0, viewport);
			}

			// Final composition as full screen quad
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.deferred);
			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.quad.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].drawIndexed(6, 1, 0, 0, 1);

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
		loadMesh(getAssetPath() + "models/armor/armor.dae", &meshes.example, vertexLayout, 1.0f);
	}

	void generateQuads()
	{
		// Setup vertices for multiple screen aligned quads
		// Used for displaying final result and debug 
		struct Vertex {
			float pos[3];
			float uv[2];
			float col[3];
			float normal[3];
		};

		std::vector<Vertex> vertexBuffer;

		float x = 0.0f;
		float y = 0.0f;
		for (uint32_t i = 0; i < 3; i++)
		{
			// Last component of normal is used for debug display sampler index
			vertexBuffer.push_back({ { x+1.0f, y+1.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x,      y+1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x,      y,      0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x+1.0f, y,      0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
			x += 1.0f;
			if (x > 1.0f)
			{
				x = 0.0f;
				y += 1.0f;
			}
		}
		createBuffer(vk::BufferUsageFlagBits::eVertexBuffer,
			vertexBuffer.size() * sizeof(Vertex),
			vertexBuffer.data(),
			meshes.quad.vertices.buf,
			meshes.quad.vertices.mem);

		// Setup indices
		std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
		for (uint32_t i = 0; i < 3; ++i)
		{
			uint32_t indices[6] = { 0,1,2, 2,3,0 };
			for (auto index : indices)
			{
				indexBuffer.push_back(i * 4 + index);
			}
		}
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
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 8),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 8)
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
	}

	void setupDescriptorSetLayout()
	{
		// Deferred shading layout
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eVertex,
				0),
			// Binding 1 : Position texture target / Scene colormap
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				1),
			// Binding 2 : Normals texture target
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				2),
			// Binding 3 : Albedo texture target
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				3),
			// Binding 4 : Fragment shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eFragment,
				4),
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);
		

		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayouts.deferred = device.createPipelineLayout(pPipelineLayoutCreateInfo);
		

		// Offscreen (scene) rendering pipeline layout
		pipelineLayouts.offscreen = device.createPipelineLayout(pPipelineLayoutCreateInfo);
		
	}

	void setupDescriptorSet()
	{
		// Textured quad descriptor set
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the offscreen texture targets
		vk::DescriptorImageInfo texDescriptorPosition =
			vkTools::initializers::descriptorImageInfo(textureTargets.position.sampler, textureTargets.position.view, vk::ImageLayout::eGeneral);

		vk::DescriptorImageInfo texDescriptorNormal =
			vkTools::initializers::descriptorImageInfo(textureTargets.normal.sampler, textureTargets.normal.view, vk::ImageLayout::eGeneral);

		vk::DescriptorImageInfo texDescriptorAlbedo =
			vkTools::initializers::descriptorImageInfo(textureTargets.albedo.sampler, textureTargets.albedo.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
			descriptorSet,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsFullScreen.descriptor),
			// Binding 1 : Position texture target
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptorPosition),
			// Binding 2 : Normals texture target
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				2,
				&texDescriptorNormal),
			// Binding 3 : Albedo texture target
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				3,
				&texDescriptorAlbedo),
			// Binding 4 : Fragment shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eUniformBuffer,
				4,
				&uniformData.fsLights.descriptor),
		};

		device.updateDescriptorSets(writeDescriptorSets, nullptr);

		// Offscreen (scene)
		descriptorSets.offscreen = device.allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo texDescriptorSceneColormap =
			vkTools::initializers::descriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.offscreen,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsOffscreen.descriptor),
			// Binding 1 : Scene color map
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.offscreen,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptorSceneColormap)
		};
		device.updateDescriptorSets(offScreenWriteDescriptorSets, nullptr);
	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList);

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

		vk::PipelineMultisampleStateCreateInfo multisampleState;

		std::vector<vk::DynamicState> dynamicStateEnables = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};
		vk::PipelineDynamicStateCreateInfo dynamicState;
		dynamicState.dynamicStateCount = dynamicStateEnables.size();
		dynamicState.pDynamicStates = dynamicStateEnables.data();

		// Final fullscreen pass pipeline
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/deferred/deferred.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/deferred/deferred.frag.spv", vk::ShaderStageFlagBits::eFragment);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vkTools::initializers::pipelineCreateInfo(pipelineLayouts.deferred, renderPass);
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

		pipelines.deferred = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		

		// Debug display pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/deferred/debug.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/deferred/debug.frag.spv", vk::ShaderStageFlagBits::eFragment);
		pipelines.debug = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
		
		
		// Offscreen pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/deferred/mrt.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/deferred/mrt.frag.spv", vk::ShaderStageFlagBits::eFragment);

		// Separate render pass
		pipelineCreateInfo.renderPass = offScreenFrameBuf.renderPass;

		// Separate layout
		pipelineCreateInfo.layout = pipelineLayouts.offscreen;

		// Blend attachment states required for all color attachments
		// This is important, as color write mask will otherwise be 0x0 and you
		// won't see anything rendered to the attachment
		std::array<vk::PipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vkTools::initializers::pipelineColorBlendAttachmentState(),
			vkTools::initializers::pipelineColorBlendAttachmentState(),
			vkTools::initializers::pipelineColorBlendAttachmentState()
		};

		colorBlendState.attachmentCount = blendAttachmentStates.size();
		colorBlendState.pAttachments = blendAttachmentStates.data();

		pipelines.offscreen = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Fullscreen vertex shader
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVS),
			&uboVS,
			uniformData.vsFullScreen.buffer,
			uniformData.vsFullScreen.memory,
			uniformData.vsFullScreen.descriptor);

		// Deferred vertex shader
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboOffscreenVS),
			&uboOffscreenVS,
			uniformData.vsOffscreen.buffer,
			uniformData.vsOffscreen.memory,
			uniformData.vsOffscreen.descriptor);

		// Deferred fragment shader
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboFragmentLights),
			&uboFragmentLights,
			uniformData.fsLights.buffer,
			uniformData.fsLights.memory,
			uniformData.fsLights.descriptor);

		// Update
		updateUniformBuffersScreen();
		updateUniformBufferDeferredMatrices();
		updateUniformBufferDeferredLights();
	}

	void updateUniformBuffersScreen()
	{
		if (debugDisplay)
		{
			uboVS.projection = glm::ortho(0.0f, 2.0f, 0.0f, 2.0f, -1.0f, 1.0f);
		} 
		else
		{
			uboVS.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
		}
		uboVS.model = glm::mat4();

		void *pData = device.mapMemory(uniformData.vsFullScreen.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());
		memcpy(pData, &uboVS, sizeof(uboVS));
		device.unmapMemory(uniformData.vsFullScreen.memory);
	}

	void updateUniformBufferDeferredMatrices()
	{
		uboOffscreenVS.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
		uboOffscreenVS.view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));
		uboOffscreenVS.view = glm::rotate(uboOffscreenVS.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboOffscreenVS.view = glm::rotate(uboOffscreenVS.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboOffscreenVS.view = glm::rotate(uboOffscreenVS.view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uboOffscreenVS.model = glm::mat4();
		uboOffscreenVS.model = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.25f, 0.0f));

		void *pData = device.mapMemory(uniformData.vsOffscreen.memory, 0, sizeof(uboOffscreenVS), vk::MemoryMapFlags());
		memcpy(pData, &uboOffscreenVS, sizeof(uboOffscreenVS));
		device.unmapMemory(uniformData.vsOffscreen.memory);
	}

	// Update fragment shader light position uniform block
	void updateUniformBufferDeferredLights()
	{
		// White light from above
		uboFragmentLights.lights[0].position = glm::vec4(0.0f, 3.0f, 1.0f, 0.0f);
		uboFragmentLights.lights[0].color = glm::vec4(1.5f);
		uboFragmentLights.lights[0].radius = 15.0f;
		uboFragmentLights.lights[0].linearFalloff = 0.3f;
		uboFragmentLights.lights[0].quadraticFalloff = 0.4f;
		// Red light
		uboFragmentLights.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
		uboFragmentLights.lights[1].color = glm::vec4(1.5f, 0.0f, 0.0f, 0.0f);
		uboFragmentLights.lights[1].radius = 15.0f;
		uboFragmentLights.lights[1].linearFalloff = 0.4f;
		uboFragmentLights.lights[1].quadraticFalloff = 0.3f;
		// Blue light
		uboFragmentLights.lights[2].position = glm::vec4(2.0f, 1.0f, 0.0f, 0.0f);
		uboFragmentLights.lights[2].color = glm::vec4(0.0f, 0.0f, 2.5f, 0.0f);
		uboFragmentLights.lights[2].radius = 10.0f;
		uboFragmentLights.lights[2].linearFalloff = 0.45f;
		uboFragmentLights.lights[2].quadraticFalloff = 0.35f;
		// Belt glow
		uboFragmentLights.lights[3].position = glm::vec4(0.0f, 0.7f, 0.5f, 0.0f);
		uboFragmentLights.lights[3].color = glm::vec4(2.5f, 2.5f, 0.0f, 0.0f);
		uboFragmentLights.lights[3].radius = 5.0f;
		uboFragmentLights.lights[3].linearFalloff = 8.0f;
		uboFragmentLights.lights[3].quadraticFalloff = 6.0f;
		// Green light
		uboFragmentLights.lights[4].position = glm::vec4(3.0f, 2.0f, 1.0f, 0.0f);
		uboFragmentLights.lights[4].color = glm::vec4(0.0f, 1.5f, 0.0f, 0.0f);
		uboFragmentLights.lights[4].radius = 10.0f;
		uboFragmentLights.lights[4].linearFalloff = 0.8f;
		uboFragmentLights.lights[4].quadraticFalloff = 0.6f;

		// Current view position
		uboFragmentLights.viewPos = glm::vec4(0.0f, 0.0f, -zoom, 0.0f);

		void *pData = device.mapMemory(uniformData.fsLights.memory, 0, sizeof(uboFragmentLights), vk::MemoryMapFlags());
		memcpy(pData, &uboFragmentLights, sizeof(uboFragmentLights));
		device.unmapMemory(uniformData.fsLights.memory);
	}


	void prepare()
	{
		VulkanExampleBase::prepare();
		loadTextures();
		generateQuads();
		loadMeshes();
		setupVertexDescriptions();
		prepareOffscreenFramebuffer();
		prepareUniformBuffers();
		prepareTextureTargets();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		buildDeferredCommandBuffer(); 
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
		updateUniformBufferDeferredMatrices();
	}

	void toggleDebugDisplay()
	{
		debugDisplay = !debugDisplay;
		reBuildCommandBuffers();
		updateUniformBuffersScreen();
	}


	void keyPressed(uint32_t key) override {
		switch (key) {
		case 0x44:
			toggleDebugDisplay();
			break;
		}
	}
};

RUN_EXAMPLE(VulkanExample)

