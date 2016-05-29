/*
* Vulkan Example - Example for VK_EXT_debug_marker extension. To be used in conjuction with a debugging app like RenderDoc (https://renderdoc.org)
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
#define ENABLE_VALIDATION false

// Offscreen properties
#define OFFSCREEN_DIM 256
#define OFFSCREEN_FORMAT vk::Format::eR8G8B8A8Unorm
#define OFFSCREEN_FILTER vk::Filter::eLinear;

// Extension spec can be found at https://github.com/KhronosGroup/Vulkan-Docs/blob/1.0-VK_EXT_debug_marker/doc/specs/vulkan/appendices/VK_EXT_debug_marker.txt
// Note that the extension will only be present if run from an offline debugging application
// The actual check for extension presence and enabling it on the device is done in the example base class
// See VulkanExampleBase::createInstance and VulkanExampleBase::createDevice (base/vulkanexamplebase.cpp)
namespace DebugMarker
{
	bool active = false;

	PFN_vkDebugMarkerSetObjectTagEXT pfnDebugMarkerSetObjectTag = VK_NULL_HANDLE;
	PFN_vkDebugMarkerSetObjectNameEXT pfnDebugMarkerSetObjectName = VK_NULL_HANDLE;
	PFN_vkCmdDebugMarkerBeginEXT pfnCmdDebugMarkerBegin = VK_NULL_HANDLE;
	PFN_vkCmdDebugMarkerEndEXT pfnCmdDebugMarkerEnd = VK_NULL_HANDLE;
	PFN_vkCmdDebugMarkerInsertEXT pfnCmdDebugMarkerInsert = VK_NULL_HANDLE;

	// Get function pointers for the debug report extensions from the device
	void setup(VkDevice device)
	{
		pfnDebugMarkerSetObjectTag = (PFN_vkDebugMarkerSetObjectTagEXT)vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectTagEXT");
		pfnDebugMarkerSetObjectName = (PFN_vkDebugMarkerSetObjectNameEXT)vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectNameEXT");
		pfnCmdDebugMarkerBegin = (PFN_vkCmdDebugMarkerBeginEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerBeginEXT");
		pfnCmdDebugMarkerEnd = (PFN_vkCmdDebugMarkerEndEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerEndEXT");
		pfnCmdDebugMarkerInsert = (PFN_vkCmdDebugMarkerInsertEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerInsertEXT");

		// Set flag if at least one function pointer is present
		active = (pfnDebugMarkerSetObjectName != VK_NULL_HANDLE);
	}

	// Sets the debug name of an object
	// All Objects in Vulkan are represented by their 64-bit handles which are passed into this function
	// along with the object type
	void setObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char *name)
	{
		// Check for valid function pointer (may not be present if not running in a debugging application)
		if (pfnDebugMarkerSetObjectName)
		{
			VkDebugMarkerObjectNameInfoEXT nameInfo = {};
			nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
			nameInfo.objectType = objectType;
			nameInfo.object = object;
			nameInfo.pObjectName = name;
			pfnDebugMarkerSetObjectName(device, &nameInfo);
		}
	}

	// Set the tag for an object
	void setObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void* tag)
	{
		// Check for valid function pointer (may not be present if not running in a debugging application)
		if (pfnDebugMarkerSetObjectTag)
		{
			VkDebugMarkerObjectTagInfoEXT tagInfo = {};
			tagInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT;
			tagInfo.objectType = objectType;
			tagInfo.object = object;
			tagInfo.tagName = name;
			tagInfo.tagSize = tagSize;
			tagInfo.pTag = tag;
			pfnDebugMarkerSetObjectTag(device, &tagInfo);
		}
	}

	// Start a new debug marker region
	void beginRegion(VkCommandBuffer cmdbuffer, const char* pMarkerName, glm::vec4 color)
	{
		// Check for valid function pointer (may not be present if not running in a debugging application)
		if (pfnCmdDebugMarkerBegin)
		{
			VkDebugMarkerMarkerInfoEXT markerInfo = {};
			markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
			memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
			markerInfo.pMarkerName = pMarkerName;
			pfnCmdDebugMarkerBegin(cmdbuffer, &markerInfo);
		}
	}

	// Insert a new debug marker into the command buffer
	void insert(VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color)
	{
		// Check for valid function pointer (may not be present if not running in a debugging application)
		if (pfnCmdDebugMarkerInsert)
		{
			VkDebugMarkerMarkerInfoEXT markerInfo = {};
			markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
			memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
			markerInfo.pMarkerName = markerName.c_str();
			pfnCmdDebugMarkerInsert(cmdbuffer, &markerInfo);
		}
	}

	// End the current debug marker region
	void endRegion(VkCommandBuffer cmdBuffer)
	{
		// Check for valid function (may not be present if not runnin in a debugging application)
		if (pfnCmdDebugMarkerEnd)
		{
			pfnCmdDebugMarkerEnd(cmdBuffer);
		}
	}
};
// Vertex layout used in this example
struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec3 color;
};

struct Scene {
	struct {
		vk::Buffer buf;
		vk::DeviceMemory mem;
	} vertices;
	struct {
		vk::Buffer buf;
		vk::DeviceMemory mem;
	} indices;

	// Store mesh offsets for vertex and indexbuffers
	struct Mesh
	{
		uint32_t indexStart;
		uint32_t indexCount;
		std::string name;
	};
	std::vector<Mesh> meshes;

	void draw(vk::CommandBuffer cmdBuffer)
	{
		vk::DeviceSize offsets = 0;
		cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, vertices.buf, offsets);
		cmdBuffer.bindIndexBuffer(indices.buf, 0, vk::IndexType::eUint32);
		for (auto mesh : meshes)
		{
			// Add debug marker for mesh name
			DebugMarker::insert(cmdBuffer, "Draw \"" + mesh.name + "\"", glm::vec4(0.0f));
			cmdBuffer.drawIndexed(mesh.indexCount, 1, mesh.indexStart, 0, 0);
		}
	}
};

class VulkanExample : public VulkanExampleBase
{
public:
	bool wireframe = true;
	bool glow = true;

	struct {
		vk::PipelineVertexInputStateCreateInfo inputState;
		std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	Scene scene, sceneGlow;

	struct {
		vkTools::UniformData vsScene;
	} uniformData;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::vec4 lightPos = glm::vec4(0.0f, 5.0f, 15.0f, 1.0f);
	} uboVS;

	struct {
		vk::Pipeline toonshading;
		vk::Pipeline color;
		vk::Pipeline wireframe;
		vk::Pipeline postprocess;
	} pipelines;

	vk::PipelineLayout pipelineLayout;
	vk::DescriptorSetLayout descriptorSetLayout;

	struct {
		vk::DescriptorSet scene;
		vk::DescriptorSet fullscreen;
	} descriptorSets;

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
		vkTools::VulkanTexture textureTarget;
	} offScreenFrameBuf;

	vk::CommandBuffer offScreenCmdBuffer;

	// Random tag data
	struct {
		const char name[17] = "debug marker tag";
	} demoTag;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -8.5f;
		zoomSpeed = 2.5f;
		rotationSpeed = 0.5f;
		rotation = { -4.35f, 16.25f, 0.0f };
		cameraPos = { 0.1f, 1.1f, 0.0f };
		enableTextOverlay = true;
		title = "Vulkan Example - VK_EXT_debug_marker";
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		device.destroyPipeline(pipelines.toonshading, nullptr);
		device.destroyPipeline(pipelines.color, nullptr);
		device.destroyPipeline(pipelines.wireframe, nullptr);
		device.destroyPipeline(pipelines.postprocess, nullptr);

		device.destroyPipelineLayout(pipelineLayout, nullptr);
		device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

		// Destroy and free mesh resources 
		device.destroyBuffer(scene.vertices.buf, nullptr);
		device.freeMemory(scene.vertices.mem, nullptr);
		device.destroyBuffer(scene.indices.buf, nullptr);
		device.freeMemory(scene.indices.mem, nullptr);
		device.destroyBuffer(sceneGlow.vertices.buf, nullptr);
		device.freeMemory(sceneGlow.vertices.mem, nullptr);
		device.destroyBuffer(sceneGlow.indices.buf, nullptr);
		device.freeMemory(sceneGlow.indices.mem, nullptr);

		vkTools::destroyUniformData(device, &uniformData.vsScene);

		// Offscreen
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
	}

	// Prepare a texture target and framebuffer for offscreen rendering
	void prepareOffscreen()
	{
		vk::CommandBuffer cmdBuffer = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

		vk::FormatProperties formatProperties;

		// Get device properites for the requested texture format
		formatProperties = physicalDevice.getFormatProperties(OFFSCREEN_FORMAT);
		// Check if blit destination is supported for the requested format
		// Only try for optimal tiling, linear tiling usually won't support blit as destination anyway
		assert(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);

		// Texture target

		vkTools::VulkanTexture *tex = &offScreenFrameBuf.textureTarget;

		// Prepare blit target texture
		tex->width = OFFSCREEN_DIM;
		tex->height = OFFSCREEN_DIM;

		vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo.imageType = vk::ImageType::e2D;
		imageCreateInfo.format = OFFSCREEN_FORMAT;
		imageCreateInfo.extent = { OFFSCREEN_DIM, OFFSCREEN_DIM, 1 };
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
		imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
		imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
		imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
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

		// Transform image layout to transfer destination
		tex->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		vkTools::setImageLayout(
			cmdBuffer,
			tex->image,
			vk::ImageAspectFlagBits::eColor,
			vk::ImageLayout::ePreinitialized,
			tex->imageLayout);

		// Create sampler
		vk::SamplerCreateInfo sampler;
		sampler.magFilter = OFFSCREEN_FILTER;
		sampler.minFilter = OFFSCREEN_FILTER;
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
		vk::ImageViewCreateInfo view;
		view.image;
		view.viewType = vk::ImageViewType::e2D;
		view.format = OFFSCREEN_FORMAT;
		view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
		view.image = tex->image;
		tex->view = device.createImageView(view, nullptr);

		// Name for debugging
		DebugMarker::setObjectName(device, (uint64_t)(VkImage)tex->image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, "Off-screen texture target image");
		DebugMarker::setObjectName(device, (uint64_t)(VkSampler)tex->sampler, VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, "Off-screen texture target sampler");

		// Frame buffer
		offScreenFrameBuf.width = OFFSCREEN_DIM;
		offScreenFrameBuf.height = OFFSCREEN_DIM;

		// Find a suitable depth format
		vk::Format fbDepthFormat;
		vk::Bool32 validDepthFormat = vkTools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
		assert(validDepthFormat);

		// Color attachment
		vk::ImageCreateInfo image;
		image.imageType = vk::ImageType::e2D;
		image.format = OFFSCREEN_FORMAT;
		image.extent.width = offScreenFrameBuf.width;
		image.extent.height = offScreenFrameBuf.height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = vk::SampleCountFlagBits::e1;
		image.tiling = vk::ImageTiling::eOptimal;
		// Image of the framebuffer is blit source
		image.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;

		vk::ImageViewCreateInfo colorImageView;
		colorImageView.viewType = vk::ImageViewType::e2D;
		colorImageView.format = OFFSCREEN_FORMAT;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;

		offScreenFrameBuf.color.image = device.createImage(image, nullptr);
		memReqs = device.getImageMemoryRequirements(offScreenFrameBuf.color.image);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		offScreenFrameBuf.color.mem = device.allocateMemory(memAllocInfo, nullptr);
		device.bindImageMemory(offScreenFrameBuf.color.image, offScreenFrameBuf.color.mem, 0);

		vkTools::setImageLayout(
			cmdBuffer,
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
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		offScreenFrameBuf.depth.mem = device.allocateMemory(memAllocInfo, nullptr);
		device.bindImageMemory(offScreenFrameBuf.depth.image, offScreenFrameBuf.depth.mem, 0);

		vkTools::setImageLayout(
			cmdBuffer,
			offScreenFrameBuf.depth.image,
			vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthStencilAttachmentOptimal);

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

		VulkanExampleBase::flushCommandBuffer(cmdBuffer, queue, true);

		// Command buffer for offscreen rendering
		offScreenCmdBuffer = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, false);

		// Name for debugging
		DebugMarker::setObjectName(device, (uint64_t)(VkImage)offScreenFrameBuf.color.image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, "Off-screen color framebuffer");
		DebugMarker::setObjectName(device, (uint64_t)(VkImage)offScreenFrameBuf.depth.image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, "Off-screen depth framebuffer");
	}

	// Command buffer for rendering color only scene for glow
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

		// Start a new debug marker region
		DebugMarker::beginRegion(offScreenCmdBuffer, "Off-screen scene rendering", glm::vec4(1.0f, 0.78f, 0.05f, 1.0f));

		vk::Viewport viewport = vkTools::initializers::viewport((float)offScreenFrameBuf.width, (float)offScreenFrameBuf.height, 0.0f, 1.0f);
		offScreenCmdBuffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkTools::initializers::rect2D(offScreenFrameBuf.width, offScreenFrameBuf.height, 0, 0);
		offScreenCmdBuffer.setScissor(0, scissor);

		offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

		offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.scene, nullptr);
		offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.color);

		// Draw glow scene
		sceneGlow.draw(offScreenCmdBuffer);

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

		DebugMarker::endRegion(offScreenCmdBuffer);

		offScreenCmdBuffer.end();
	}

	// Load a model file as separate meshes into a scene
	void loadModel(std::string filename, Scene *scene)
	{
		VulkanMeshLoader *meshLoader = new VulkanMeshLoader();
#if defined(__ANDROID__)
		meshLoader->assetManager = androidApp->activity->assetManager;
#endif
		meshLoader->LoadMesh(filename);

		scene->meshes.resize(meshLoader->m_Entries.size());

		// Generate vertex buffer
		float scale = 1.0f;
		std::vector<Vertex> vertexBuffer;
		// Iterate through all meshes in the file
		// and extract the vertex information used in this demo
		for (uint32_t m = 0; m < meshLoader->m_Entries.size(); m++)
		{
			for (uint32_t i = 0; i < meshLoader->m_Entries[m].Vertices.size(); i++)
			{
				Vertex vertex;

				vertex.pos = meshLoader->m_Entries[m].Vertices[i].m_pos * scale;
				vertex.normal = meshLoader->m_Entries[m].Vertices[i].m_normal;
				vertex.uv = meshLoader->m_Entries[m].Vertices[i].m_tex;
				vertex.color = meshLoader->m_Entries[m].Vertices[i].m_color;

				vertexBuffer.push_back(vertex);
			}
		}
		uint32_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);

		// Generate index buffer from loaded mesh file
		std::vector<uint32_t> indexBuffer;
		for (uint32_t m = 0; m < meshLoader->m_Entries.size(); m++)
		{
			uint32_t indexBase = indexBuffer.size();
			for (uint32_t i = 0; i < meshLoader->m_Entries[m].Indices.size(); i++)
			{
				indexBuffer.push_back(meshLoader->m_Entries[m].Indices[i] + indexBase);
			}
			scene->meshes[m].indexStart = indexBase;
			scene->meshes[m].indexCount = meshLoader->m_Entries[m].Indices.size();
		}
		uint32_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);

		// Static mesh should always be device local

		bool useStaging = true;

		if (useStaging)
		{
			struct {
				vk::Buffer buffer;
				vk::DeviceMemory memory;
			} vertexStaging, indexStaging;

			// Create staging buffers
			// Vertex data
		createBuffer(vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible,
			vertexBufferSize,
			vertexBuffer.data(),
			vertexStaging.buffer,
			vertexStaging.memory);
			// Index data
		createBuffer(vk::BufferUsageFlagBits::eTransferSrc,
			vk::MemoryPropertyFlagBits::eHostVisible,
			indexBufferSize,
			indexBuffer.data(),
			indexStaging.buffer,
			indexStaging.memory);

			// Create device local buffers
			// Vertex buffer
		createBuffer(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			vertexBufferSize,
			nullptr,
			scene->vertices.buf,
			scene->vertices.mem);
			// Index buffer
		createBuffer(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
			vk::MemoryPropertyFlagBits::eDeviceLocal,
			indexBufferSize,
			nullptr,
			scene->indices.buf,
			scene->indices.mem);

			// Copy from staging buffers
			vk::CommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(vk::CommandBufferLevel::ePrimary, true);

			vk::BufferCopy copyRegion = {};

			copyRegion.size = vertexBufferSize;
			copyCmd.copyBuffer(vertexStaging.buffer, scene->vertices.buf, copyRegion);

			copyRegion.size = indexBufferSize;
			copyCmd.copyBuffer(indexStaging.buffer, scene->indices.buf, copyRegion);

			VulkanExampleBase::flushCommandBuffer(copyCmd, queue, true);

			device.destroyBuffer(vertexStaging.buffer, nullptr);
			device.freeMemory(vertexStaging.memory, nullptr);
			device.destroyBuffer(indexStaging.buffer, nullptr);
			device.freeMemory(indexStaging.memory, nullptr);
		}
		else
		{
			// Vertex buffer
		createBuffer(vk::BufferUsageFlagBits::eVertexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible,
			vertexBufferSize,
			vertexBuffer.data(),
			scene->vertices.buf,
			scene->vertices.mem);
			// Index buffer
		createBuffer(vk::BufferUsageFlagBits::eIndexBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible,
			indexBufferSize,
			indexBuffer.data(),
			scene->indices.buf,
			scene->indices.mem);
		}

		delete(meshLoader);
	}

	void loadScene()
	{
		loadModel(getAssetPath() + "models/treasure_smooth.dae", &scene);
		loadModel(getAssetPath() + "models/treasure_glow.dae", &sceneGlow);

		// Name the meshes
		// ASSIMP does not load mesh names from the COLLADA file used in this example
		// so we need to set them manually
		// These names are used in command buffer creation for setting debug markers
		// Scene
		std::vector<std::string> names = { "hill", "rocks", "cave", "tree", "mushroom stems", "blue mushroom caps", "red mushroom caps", "grass blades", "chest box", "chest fittings" };
		for (size_t i = 0; i < names.size(); i++)
		{
			scene.meshes[i].name = names[i];
			sceneGlow.meshes[i].name = names[i];
		}

		// Name the buffers for debugging
		// Scene
		DebugMarker::setObjectName(device, (uint64_t)(VkBuffer)scene.vertices.buf, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Scene vertex buffer");
		DebugMarker::setObjectName(device, (uint64_t)(VkBuffer)scene.indices.buf, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Scene index buffer");
		// Glow
		DebugMarker::setObjectName(device, (uint64_t)(VkBuffer)sceneGlow.vertices.buf, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Glow vertex buffer");
		DebugMarker::setObjectName(device, (uint64_t)(VkBuffer)sceneGlow.indices.buf, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Glow index buffer");
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

			// Start a new debug marker region
			DebugMarker::beginRegion(drawCmdBuffers[i], "Render scene", glm::vec4(0.5f, 0.76f, 0.34f, 1.0f));

			drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkTools::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			drawCmdBuffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkTools::initializers::rect2D(wireframe ? width / 2 : width, height, 0, 0);
			drawCmdBuffers[i].setScissor(0, scissor);

			drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.scene, nullptr);

			// Solid rendering

			// Start a new debug marker region
			DebugMarker::beginRegion(drawCmdBuffers[i], "Toon shading draw", glm::vec4(0.78f, 0.74f, 0.9f, 1.0f));

			drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.toonshading);
			scene.draw(drawCmdBuffers[i]);

			DebugMarker::endRegion(drawCmdBuffers[i]);

			// Wireframe rendering
			if (wireframe)
			{
				// Insert debug marker
				DebugMarker::beginRegion(drawCmdBuffers[i], "Wireframe draw", glm::vec4(0.53f, 0.78f, 0.91f, 1.0f));

				scissor.offset.x = width / 2;
				drawCmdBuffers[i].setScissor(0, scissor);

				drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.wireframe);
				scene.draw(drawCmdBuffers[i]);

				DebugMarker::endRegion(drawCmdBuffers[i]);

				scissor.offset.x = 0;
				scissor.extent.width = width;
				drawCmdBuffers[i].setScissor(0, scissor);
			}

			// Post processing
			if (glow)
			{
				DebugMarker::beginRegion(drawCmdBuffers[i], "Apply post processing", glm::vec4(0.93f, 0.89f, 0.69f, 1.0f));

				drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.postprocess);
				// Full screen quad is generated by the vertex shaders, so we reuse four vertices (for four invocations) from current vertex buffer
				drawCmdBuffers[i].draw(4, 1, 0, 0);

				DebugMarker::endRegion(drawCmdBuffers[i]);
			}


			drawCmdBuffers[i].endRenderPass();

			// End current debug marker region
			DebugMarker::endRegion(drawCmdBuffers[i]);

			drawCmdBuffers[i].end();
		}
	}
	
	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(4);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);
		// Location 1 : Normal
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3);
		// Location 2 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 6);
		// Location 3 : Color
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
		// Example uses one ubo and one combined image sampler
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
			vkTools::initializers::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1),
		};

		vk::DescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 1);

		descriptorPool = device.createDescriptorPool(descriptorPoolInfo, nullptr);
	}

	void setupDescriptorSetLayout()
	{
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(
			vk::DescriptorType::eUniformBuffer,
				vk::ShaderStageFlagBits::eVertex,
				0),
			// Binding 1 : Fragment shader combined sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				vk::DescriptorType::eCombinedImageSampler,
				vk::ShaderStageFlagBits::eFragment,
				1),
		};

		vk::DescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);

		vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo, nullptr);

		// Name for debugging
		DebugMarker::setObjectName(device, (uint64_t)(VkPipelineLayout)pipelineLayout, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT, "Shared pipeline layout");
		DebugMarker::setObjectName(device, (uint64_t)(VkDescriptorSetLayout)descriptorSetLayout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, "Shared descriptor set layout");
	}

	void setupDescriptorSet()
	{
		vk::DescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

		vk::DescriptorImageInfo texDescriptor =
			vkTools::initializers::descriptorImageInfo(offScreenFrameBuf.textureTarget.sampler, offScreenFrameBuf.textureTarget.view, vk::ImageLayout::eGeneral);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Vertex shader uniform buffer
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.scene,
				vk::DescriptorType::eUniformBuffer,
				0,
				&uniformData.vsScene.descriptor),
			// Binding 1 : Color map 
			vkTools::initializers::writeDescriptorSet(
				descriptorSets.scene,
				vk::DescriptorType::eCombinedImageSampler,
				1,
				&texDescriptor)
		};

		device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
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

		// Phong lighting pipeline
		// Load shaders
		std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/debugmarker/toon.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/debugmarker/toon.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

		pipelines.toonshading = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Color only pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/debugmarker/colorpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/debugmarker/colorpass.frag.spv", vk::ShaderStageFlagBits::eFragment);

		pipelines.color = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Wire frame rendering pipeline
		rasterizationState.polygonMode = vk::PolygonMode::eLine;
		rasterizationState.lineWidth = 1.0f;

		pipelines.wireframe = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Post processing effect
		shaderStages[0] = loadShader(getAssetPath() + "shaders/debugmarker/postprocess.vert.spv", vk::ShaderStageFlagBits::eVertex);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/debugmarker/postprocess.frag.spv", vk::ShaderStageFlagBits::eFragment);

		depthStencilState.depthTestEnable = VK_FALSE;
		depthStencilState.depthWriteEnable = VK_FALSE;

		rasterizationState.polygonMode = vk::PolygonMode::eFill;
		rasterizationState.cullMode = vk::CullModeFlagBits::eNone;

		blendAttachmentState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		blendAttachmentState.blendEnable =  VK_TRUE;
		blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
		blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
		blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
		blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;

		pipelines.postprocess = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

		// Name shader moduels for debugging
		// Shader module count starts at 2 when text overlay in base class is enabled
		uint32_t moduleIndex = enableTextOverlay ? 2 : 0;
		DebugMarker::setObjectName(device, (uint64_t)(VkShaderModule)shaderModules[moduleIndex + 0], VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, "Toon shading vertex shader");
		DebugMarker::setObjectName(device, (uint64_t)(VkShaderModule)shaderModules[moduleIndex + 1], VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, "Toon shading fragment shader");
		DebugMarker::setObjectName(device, (uint64_t)(VkShaderModule)shaderModules[moduleIndex + 2], VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, "Color-only vertex shader");
		DebugMarker::setObjectName(device, (uint64_t)(VkShaderModule)shaderModules[moduleIndex + 3], VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, "Color-only fragment shader");
		DebugMarker::setObjectName(device, (uint64_t)(VkShaderModule)shaderModules[moduleIndex + 4], VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, "Postprocess vertex shader");
		DebugMarker::setObjectName(device, (uint64_t)(VkShaderModule)shaderModules[moduleIndex + 5], VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, "Postprocess fragment shader");

		// Name pipelines for debugging
		DebugMarker::setObjectName(device, (uint64_t)(VkPipeline)pipelines.toonshading, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "Toon shading pipeline");
		DebugMarker::setObjectName(device, (uint64_t)(VkPipeline)pipelines.color, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "Color only pipeline");
		DebugMarker::setObjectName(device, (uint64_t)(VkPipeline)pipelines.wireframe, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "Wireframe rendering pipeline");
		DebugMarker::setObjectName(device, (uint64_t)(VkPipeline)pipelines.postprocess, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, "Post processing pipeline");
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
			sizeof(uboVS),
			&uboVS,
			uniformData.vsScene.buffer,
			uniformData.vsScene.memory,
			uniformData.vsScene.descriptor);

		// Name uniform buffer for debugging
		DebugMarker::setObjectName(device, (uint64_t)(VkBuffer)uniformData.vsScene.buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "Scene uniform buffer block");
		// Add some random tag
		DebugMarker::setObjectTag(device, (uint64_t)(VkBuffer)uniformData.vsScene.buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, 0, sizeof(demoTag), &demoTag);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
		glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));

		uboVS.model = viewMatrix * glm::translate(glm::mat4(), cameraPos);
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		void *pData = device.mapMemory(uniformData.vsScene.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());
		memcpy(pData, &uboVS, sizeof(uboVS));
		device.unmapMemory(uniformData.vsScene.memory);
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		std::vector<vk::CommandBuffer> submitCmdBuffers;

		// Submit offscreen rendering command buffer 
		// todo : use event to ensure that offscreen result is finished bfore render command buffer is started
		if (glow)
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
		DebugMarker::setup(device);
		loadScene();
		prepareOffscreen();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		buildOffscreenCommandBuffer();
		updateTextOverlay();
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
		updateUniformBuffers();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case 0x57:
		case GAMEPAD_BUTTON_X:
			wireframe = !wireframe;
			reBuildCommandBuffers();
			break;
		case 0x47:
		case GAMEPAD_BUTTON_A:
			glow = !glow;
			reBuildCommandBuffers();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
		if (DebugMarker::active)
		{
			textOverlay->addText("VK_EXT_debug_marker active", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		}
		else
		{
			textOverlay->addText("VK_EXT_debug_marker not present", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
		}
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
