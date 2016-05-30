/*
* Vulkan Example - Offscreen rendering using a separate framebuffer
*
*	p - Toggle light source animation
*	l - Toggle between scene and light's POV
*	s - Toggle shadowmap display
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"


// 16 bits of depth is enough for such a small scene
#define DEPTH_FORMAT vk::Format::eD16Unorm

// Texture properties
#define TEX_DIM 2048
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
	bool displayShadowMap = false;
	bool lightPOV = false;

	// Keep depth range as small as possible
	// for better shadow map precision
	float zNear = 1.0f;
	float zFar = 96.0f;

	// Constant depth bias factor (always applied)
	float depthBiasConstant = 1.25f;
	// Slope depth bias factor, applied depending on polygon's slope
	float depthBiasSlope = 1.75f;

	glm::vec3 lightPos = glm::vec3();
	float lightFOV = 45.0f;

	struct {
		vkMeshLoader::MeshBuffer scene;
		vkMeshLoader::MeshBuffer quad;
	} meshes;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	vkTools::UniformData uniformDataVS, uniformDataOffscreenVS;

	struct {
		vkTools::UniformData scene;
	} uniformData;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
	} uboVSquad;

	struct {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::mat4 depthBiasMVP;
		glm::vec3 lightPos;
	} uboVSscene;

	struct {
		glm::mat4 depthMVP;
	} uboOffscreenVS;

	struct {
		vk::Pipeline quad;
		vk::Pipeline offscreen;
		vk::Pipeline scene;
	} pipelines;

	struct {
		vk::PipelineLayout quad;
		vk::PipelineLayout offscreen;
	} pipelineLayouts;

	struct {
		vk::DescriptorSet offscreen;
		vk::DescriptorSet scene;
	} descriptorSets;

	vk::DescriptorSet descriptorSet;
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
		vk::RenderPass renderPass;
		vkTools::VulkanTexture textureTarget;
	} offScreenFrameBuf;

	vk::CommandBuffer offScreenCmdBuffer;
	
	// Semaphore used to synchronize offscreen rendering before using it's texture target for sampling
	vk::Semaphore offscreenSemaphore;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -20.0f;
		rotation = { -15.0f, -390.0f, 0.0f };
		title = "Vulkan Example - Projected shadow mapping";
		timerSpeed *= 0.5f;
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class

		// Texture target
		textureLoader->destroyTexture(offScreenFrameBuf.textureTarget);

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

		device.destroyRenderPass(offScreenFrameBuf.renderPass, nullptr);

		device.destroyPipeline(pipelines.quad, nullptr);
		device.destroyPipeline(pipelines.offscreen, nullptr);
		device.destroyPipeline(pipelines.scene, nullptr);

		device.destroyPipelineLayout(pipelineLayouts.quad, nullptr);
		device.destroyPipelineLayout(pipelineLayouts.offscreen, nullptr);

		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		// Meshes
		vkMeshLoader::freeMeshBufferResources(device, &meshes.scene);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.quad);

		// Uniform buffers
		vkTools::destroyUniformData(device, &uniformDataVS);
		vkTools::destroyUniformData(device, &uniformDataOffscreenVS);

		device.freeCommandBuffers(cmdPool, offScreenCmdBuffer);
		device.destroySemaphore(offscreenSemaphore, nullptr);
	}

	// Preapre an empty texture as the blit target from 
	// the offscreen framebuffer
	void prepareTextureTarget(uint32_t width, uint32_t height, vk::Format format)
	{
		// Get device properites for the requested texture format
		vk::FormatProperties formatProperties;
		formatProperties = physicalDevice.getFormatProperties(format);
		// Check if format is supported for optimal tiling
		assert(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment);

		// Prepare blit target texture
		offScreenFrameBuf.textureTarget.width = width;
		offScreenFrameBuf.textureTarget.height = height;

		vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.format = format;
		imageCreateInfo.extent = vk::Extent3D { width, height, 1 };
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
		imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
		imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
		imageCreateInfo.initialLayout = vk::ImageLayout::eTransferDstOptimal;
		imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
		imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;

		vk::MemoryAllocateInfo memAllocInfo;
		vk::MemoryRequirements memReqs;

		offScreenFrameBuf.textureTarget.image = device.createImage(imageCreateInfo, nullptr);
		memReqs = device.getImageMemoryRequirements(offScreenFrameBuf.textureTarget.image);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		offScreenFrameBuf.textureTarget.deviceMemory = device.allocateMemory(memAllocInfo, nullptr);
		device.bindImageMemory(offScreenFrameBuf.textureTarget.image, offScreenFrameBuf.textureTarget.deviceMemory, 0);

		vk::CommandBuffer layoutCmd = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

		offScreenFrameBuf.textureTarget.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		vkTools::setImageLayout(
			layoutCmd,
			offScreenFrameBuf.textureTarget.image,
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageLayout::ePreinitialized,
			offScreenFrameBuf.textureTarget.imageLayout);

		VulkanExampleBase::flushCommandBuffer(layoutCmd, queue, true);

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
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
		offScreenFrameBuf.textureTarget.sampler = device.createSampler(sampler, nullptr);

		// Create image view
		vk::ImageViewCreateInfo view;
		view.viewType = vk::ImageViewType::e2D;
		view.format = format;
		view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		view.subresourceRange = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 };
		view.image = offScreenFrameBuf.textureTarget.image;
		offScreenFrameBuf.textureTarget.view = device.createImageView(view, nullptr);
	}

	// Set up a separate render pass for the offscreen frame buffer
	// This is necessary as the offscreen frame buffer attachments
	// use formats different to the ones from the visible frame buffer
	// and at least the depth one may not be compatible
	void setupOffScreenRenderPass()
	{
		vk::AttachmentDescription attDesc[2];
		attDesc[0].format = FB_COLOR_FORMAT;
		attDesc[0].samples = vk::SampleCountFlagBits::e1;
		attDesc[0].loadOp = vk::AttachmentLoadOp::eClear;
		attDesc[0].storeOp = vk::AttachmentStoreOp::eStore;
		attDesc[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attDesc[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attDesc[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		attDesc[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		attDesc[1].format = DEPTH_FORMAT;
		attDesc[1].samples = vk::SampleCountFlagBits::e1;
		attDesc[1].loadOp = vk::AttachmentLoadOp::eClear;
		// Since we need to copy the depth attachment contents to our texture
		// used for shadow mapping we must use STORE_OP_STORE to make sure that
		// the depth attachment contents are preserved after rendering to it 
		// has finished
		attDesc[1].storeOp = vk::AttachmentStoreOp::eStore;
		attDesc[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attDesc[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attDesc[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		attDesc[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentReference colorReference;
		colorReference.attachment = 0;
		colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference depthReference;
		depthReference.attachment = 1;
		depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		subpass.pDepthStencilAttachment = &depthReference;

		vk::RenderPassCreateInfo renderPassCreateInfo;
		renderPassCreateInfo.attachmentCount = 2;
		renderPassCreateInfo.pAttachments = attDesc;
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;

		offScreenFrameBuf.renderPass = device.createRenderPass(renderPassCreateInfo, nullptr);
	}

	void prepareOffscreenFramebuffer()
	{
		offScreenFrameBuf.width = FB_DIM;
		offScreenFrameBuf.height = FB_DIM;

		vk::Format fbColorFormat = FB_COLOR_FORMAT;

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
		colorImageView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.layerCount = 1;
		offScreenFrameBuf.color.image = device.createImage(image, nullptr);

		memReqs = device.getImageMemoryRequirements(offScreenFrameBuf.color.image);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		offScreenFrameBuf.color.mem = device.allocateMemory(memAlloc, nullptr);
		device.bindImageMemory(offScreenFrameBuf.color.image, offScreenFrameBuf.color.mem, 0);

		vk::CommandBuffer layoutCmd = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

		vkTools::setImageLayout(
			layoutCmd,
			offScreenFrameBuf.color.image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal);

		colorImageView.image = offScreenFrameBuf.color.image;
		offScreenFrameBuf.color.view = device.createImageView(colorImageView, nullptr);

		// Depth stencil attachment
		image.format = DEPTH_FORMAT;
		image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc;

		vk::ImageViewCreateInfo depthStencilView;
		depthStencilView.viewType = vk::ImageViewType::e2D;
		depthStencilView.format = DEPTH_FORMAT;
		depthStencilView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.layerCount = 1;
		offScreenFrameBuf.depth.image = device.createImage(image, nullptr);

		memReqs = device.getImageMemoryRequirements(offScreenFrameBuf.depth.image);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		offScreenFrameBuf.depth.mem = device.allocateMemory(memAlloc, nullptr);
		device.bindImageMemory(offScreenFrameBuf.depth.image, offScreenFrameBuf.depth.mem, 0);

		vkTools::setImageLayout(
			layoutCmd,
			offScreenFrameBuf.depth.image,
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);

		VulkanExampleBase::flushCommandBuffer(layoutCmd, queue, true);

		depthStencilView.image = offScreenFrameBuf.depth.image;
		offScreenFrameBuf.depth.view = device.createImageView(depthStencilView, nullptr);

		vk::ImageView attachments[2];
		attachments[0] = offScreenFrameBuf.color.view;
		attachments[1] = offScreenFrameBuf.depth.view;

		setupOffScreenRenderPass();

		// Create frame buffer
		vk::FramebufferCreateInfo fbufCreateInfo;
		fbufCreateInfo.renderPass = offScreenFrameBuf.renderPass; 
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = offScreenFrameBuf.width;
		fbufCreateInfo.height = offScreenFrameBuf.height;
		fbufCreateInfo.layers = 1;

		offScreenFrameBuf.frameBuffer = device.createFramebuffer(fbufCreateInfo, nullptr);
	}

	void buildOffscreenCommandBuffer()
	{
		// Create separate command buffer for offscreen 
		// rendering
		if (!offScreenCmdBuffer)
		{
			vk::CommandBufferAllocateInfo cmd = vkTools::initializers::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
			offScreenCmdBuffer = device.allocateCommandBuffers(cmd)[0];
		}

		// Create a semaphore used to synchronize offscreen rendering and usage
		vk::SemaphoreCreateInfo semaphoreCreateInfo;
		offscreenSemaphore = device.createSemaphore(semaphoreCreateInfo, nullptr);

		vk::CommandBufferBeginInfo cmdBufInfo;

		vk::ClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo.renderPass = offScreenFrameBuf.renderPass;
		renderPassBeginInfo.framebuffer = offScreenFrameBuf.frameBuffer;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = offScreenFrameBuf.width;
		renderPassBeginInfo.renderArea.extent.height = offScreenFrameBuf.height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		offScreenCmdBuffer.begin(cmdBufInfo);

		vk::Viewport viewport = vkTools::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
		offScreenCmdBuffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkTools::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
		offScreenCmdBuffer.setScissor(0, scissor);

		// Set depth bias (aka "Polygon offset")
		offScreenCmdBuffer.setDepthBias(depthBiasConstant, 0.0f, depthBiasSlope);

		offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.offscreen);
		offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.offscreen, nullptr);

		vk::DeviceSize offsets = 0;
		offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.scene.vertices.buf, offsets);
		offScreenCmdBuffer.bindIndexBuffer(meshes.scene.indices.buf, 0, vk::IndexType::eUint32);
		offScreenCmdBuffer.drawIndexed(meshes.scene.indexCount, 1, 0, 0, 0);

		offScreenCmdBuffer.endRenderPass();

		updateTexture();

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

			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSet, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.quad);

			// Visualize shadow map
			if (displayShadowMap)
			{
				drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buf, offsets);
				drawCmdBuffers[i].bindIndexBuffer(meshes.quad.indices.buf, 0, vk::IndexType::eUint32);
				drawCmdBuffers[i].drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
			}

			// 3D scene
			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.scene, nullptr);
			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.scene);

			drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.scene.vertices.buf, offsets);
			drawCmdBuffers[i].bindIndexBuffer(meshes.scene.indices.buf, 0, vk::IndexType::eUint32);
			drawCmdBuffers[i].drawIndexed(meshes.scene.indexCount, 1, 0, 0, 0);

			drawCmdBuffers[i].endRenderPass();

			drawCmdBuffers[i].end();
		}
	}

	void draw()
	{
		// Get next image in the swap chain (back/front buffer)
		prepareFrame();
		// Submit offscreen command buffer for rendering depth buffer from light's pov

		// Wait for swap chain presentation to finish
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &semaphores.presentComplete;
		// Signal ready with offscreen semaphore
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &offscreenSemaphore;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &offScreenCmdBuffer;

		queue.submit(submitInfo, VK_NULL_HANDLE);

		// Submit current render command buffer
		
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Wait for offscreen semaphore
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &offscreenSemaphore;
		// Signal ready with render complete semaphpre
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &semaphores.renderComplete;

		// Submit to queue
		queue.submit(submitInfo, VK_NULL_HANDLE);

		submitFrame();
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/vulkanscene_shadow.dae", &meshes.scene, vertexLayout, 4.0f);
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
		// Example uses three ubos and two image samplers
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 6),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4)
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 3);

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
				1)
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

	void setupDescriptorSets()
	{
		// Textured quad descriptor set
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the shadow map texture
		vk::DescriptorImageInfo texDescriptor =
			vkTools::initializers::descriptorImageInfo(offScreenFrameBuf.textureTarget.sampler, offScreenFrameBuf.textureTarget.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformDataVS.descriptor),
			// Binding 1 : Fragment shader texture sampler
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptor)
		};

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Offscreen
		descriptorSets.offscreen = device.allocateDescriptorSets(allocInfo)[0];

		std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.offscreen,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformDataOffscreenVS.descriptor),
		};
		device.updateDescriptorSets(offScreenWriteDescriptorSets.size(), offScreenWriteDescriptorSets.data(), 0, NULL);

		// 3D scene
		descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

		// Image descriptor for the shadow map texture
		texDescriptor.sampler = offScreenFrameBuf.textureTarget.sampler;
		texDescriptor.imageView = offScreenFrameBuf.textureTarget.view;

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

	}

	void preparePipelines()
	{
		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

		vk::PipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eFront, vk::FrontFace::eClockwise);

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

		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/quad.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/quad.frag.spv", vk::ShaderStageFlagBits::eFragment);

		vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(pipelineLayouts.quad, renderPass);

		rasterizationState.cullMode = vk::CullModeFlagBits::eNone;

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

		pipelines.quad = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// 3D scene
		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/scene.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/scene.frag.spv", vk::ShaderStageFlagBits::eFragment);
		rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
		pipelines.scene = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Offscreen pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/offscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/offscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);
		pipelineCreateInfo.layout = pipelineLayouts.offscreen;
		// Cull front faces
		depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
		// Enable depth bias
		rasterizationState.depthBiasEnable = VK_TRUE;
		// Add depth bias to dynamic state, so we can change it at runtime
		dynamicStateEnables.push_back(vk::DynamicState::eDepthBias);
		dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

		pipelines.offscreen = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Debug quad vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVSscene),
			nullptr,
			uniformDataVS.buffer,
			uniformDataVS.memory,
			uniformDataVS.descriptor);

		// Offsvreen vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboOffscreenVS),
			nullptr,
			uniformDataOffscreenVS.buffer,
			uniformDataOffscreenVS.memory,
			uniformDataOffscreenVS.descriptor);

		// Scene vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			sizeof(uboVSscene),
			nullptr,
			uniformData.scene.buffer,
			uniformData.scene.memory,
			uniformData.scene.descriptor);

		updateLight();
		updateUniformBufferOffscreen();
		updateUniformBuffers();
	}

	void updateLight()
	{
		// Animate the light source
		lightPos.x = cos(glm::radians(timer * 360.0f)) * 40.0f;
		lightPos.y = -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f;
		lightPos.z = 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f;
	}

	void updateUniformBuffers()
	{
		// Shadow map debug quad
		float AR = (float)height / (float)width;

		uboVSquad.projection = glm::ortho(0.0f, 2.5f / AR, 0.0f, 2.5f, -1.0f, 1.0f);
		uboVSquad.model = glm::mat4();

		void *pData = device.mapMemory(uniformDataVS.memory, 0, sizeof(uboVSquad), vk::MemoryMapFlags());
		memcpy(pData, &uboVSquad, sizeof(uboVSquad));
		device.unmapMemory(uniformDataVS.memory);

		// 3D scene
		uboVSscene.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, zNear, zFar);

		uboVSscene.view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));
		uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uboVSscene.model = glm::mat4();

		uboVSscene.lightPos = lightPos;

		// Render scene from light's point of view
		if (lightPOV)
		{
			uboVSscene.projection = glm::perspective(glm::radians(lightFOV), (float)width / (float)height, zNear, zFar);
			uboVSscene.view = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
		}

	
		uboVSscene.depthBiasMVP = uboOffscreenVS.depthMVP;

		pData = device.mapMemory(uniformData.scene.memory, 0, sizeof(uboVSscene), vk::MemoryMapFlags());
		memcpy(pData, &uboVSscene, sizeof(uboVSscene));
		device.unmapMemory(uniformData.scene.memory);
	}

	void updateUniformBufferOffscreen()
	{
		// Matrix from light's point of view
		glm::mat4 depthProjectionMatrix = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
		glm::mat4 depthViewMatrix = glm::lookAt(lightPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
		glm::mat4 depthModelMatrix = glm::mat4();

		uboOffscreenVS.depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;

		void *pData = device.mapMemory(uniformDataOffscreenVS.memory, 0, sizeof(uboOffscreenVS), vk::MemoryMapFlags());
		memcpy(pData, &uboOffscreenVS, sizeof(uboOffscreenVS));
		device.unmapMemory(uniformDataOffscreenVS.memory);
	}

	// Copy offscreen depth frame buffer contents to the depth texture
	void updateTexture()
	{
		// Make sure color writes to the framebuffer are finished before using it as transfer source
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.depth.image,
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::ImageLayout::eTransferSrcOptimal);

		// Transform texture target to transfer source
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.textureTarget.image,
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageLayout::eShaderReadOnlyOptimal,
			vk::ImageLayout::eTransferDstOptimal);

		vk::ImageCopy imgCopy;

		imgCopy.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
		imgCopy.srcSubresource.layerCount = 1;

		imgCopy.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
		imgCopy.dstSubresource.layerCount = 1;

		imgCopy.extent.width = TEX_DIM;
		imgCopy.extent.height = TEX_DIM;
		imgCopy.extent.depth = 1;

		offScreenCmdBuffer.copyImage(offScreenFrameBuf.depth.image, vk::ImageLayout::eTransferSrcOptimal, offScreenFrameBuf.textureTarget.image, vk::ImageLayout::eTransferDstOptimal, imgCopy);

		// Transform framebuffer color attachment back 
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.depth.image,
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageLayout::eTransferSrcOptimal,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);

		// Transform texture target back to shader read
		// Makes sure that writes to the textuer are finished before
		// it's accessed in the shader
		vkTools::setImageLayout(
			offScreenCmdBuffer,
			offScreenFrameBuf.textureTarget.image,
			vk::ImageAspectFlagBits::eDepth,
			vk::ImageLayout::eTransferDstOptimal,
			vk::ImageLayout::eShaderReadOnlyOptimal);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		generateQuad();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		prepareTextureTarget(TEX_DIM, TEX_DIM, DEPTH_FORMAT);
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
		draw();
		if (!paused)
		{
			vkDeviceWaitIdle(device);
			updateLight();
			updateUniformBufferOffscreen();
			updateUniformBuffers();
		}
	}

	virtual void viewChanged()
	{
		vkDeviceWaitIdle(device);
		updateUniformBufferOffscreen();
		updateUniformBuffers();
	}

	void toggleShadowMapDisplay()
	{
		displayShadowMap = !displayShadowMap;
		buildCommandBuffers();
	}

	void toogleLightPOV()
	{
		lightPOV = !lightPOV;
		viewChanged();
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
			case 0x53:
				vulkanExample->toggleShadowMapDisplay();
				break;
			case 0x4C:
				vulkanExample->toogleLightPOV();
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
