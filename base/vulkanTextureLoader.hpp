/*
* Texture loader for Vulkan
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <vulkan/vulkan.h>
#pragma warning(disable: 4996 4244 4267)
#include <gli/gli.hpp>
#include "vulkantools.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

namespace vkTools 
{

	struct VulkanTexture
	{
		vk::Sampler sampler;
		vk::Image image;
		vk::ImageLayout imageLayout;
		vk::DeviceMemory deviceMemory;
		vk::ImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
		uint32_t layerCount;
	};

	class VulkanTextureLoader
	{
	private:
		vk::PhysicalDevice physicalDevice;
		vk::Device device;
		vk::Queue queue;
		vk::CommandBuffer cmdBuffer;
		vk::CommandPool cmdPool;
		vk::PhysicalDeviceMemoryProperties deviceMemoryProperties;

		// Get appropriate memory type index for a memory allocation
		uint32_t getMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties)
		{
			for (uint32_t i = 0; i < 32; i++)
			{
				if ((typeBits & 1) == 1)
				{
					if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
					{
						return i;
					}
				}
				typeBits >>= 1;
			}

			// todo : throw error
			return 0;
		}

	public:
#if defined(__ANDROID__)
		AAssetManager* assetManager = nullptr;
#endif
		// Load a 2D texture
		void loadTexture(std::string filename, vk::Format format, VulkanTexture *texture)
		{
			loadTexture(filename, format, texture, false);
		}

		// Load a 2D texture
		void loadTexture(std::string filename, vk::Format format, VulkanTexture *texture, bool forceLinear)
		{
			loadTexture(filename, format, texture, forceLinear, vk::ImageUsageFlagBits::eSampled);
		}

		// Load a 2D texture
		void loadTexture(std::string filename, vk::Format format, VulkanTexture *texture, bool forceLinear, vk::ImageUsageFlags imageUsageFlags)
		{
#if defined(__ANDROID__)
			assert(assetManager != nullptr);

			// Textures are stored inside the apk on Android (compressed)
			// So they need to be loaded via the asset manager
			AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);

			void *textureData = malloc(size);
			AAsset_read(asset, textureData, size);
			AAsset_close(asset);

			gli::texture2D tex2D(gli::load((const char*)textureData, size));

			free(textureData);
#else
			gli::texture2D tex2D(gli::load(filename.c_str()));
#endif		
			assert(!tex2D.empty());

			texture->width = (uint32_t)tex2D[0].dimensions().x;
			texture->height = (uint32_t)tex2D[0].dimensions().y;
			texture->mipLevels = tex2D.levels();

			// Get device properites for the requested texture format
			vk::FormatProperties formatProperties;
			formatProperties = physicalDevice.getFormatProperties(format);

			// Only use linear tiling if requested (and supported by the device)
			// Support for linear tiling is mostly limited, so prefer to use
			// optimal tiling instead
			// On most implementations linear tiling will only support a very
			// limited amount of formats and features (mip maps, cubemaps, arrays, etc.)
			vk::Bool32 useStaging = !forceLinear;

			vk::MemoryAllocateInfo memAllocInfo;
			vk::MemoryRequirements memReqs;

			// Use a separate command buffer for texture loading
			vk::CommandBufferBeginInfo cmdBufInfo;
			cmdBuffer.begin(cmdBufInfo);

			if (useStaging)
			{
				// Create a host-visible staging buffer that contains the raw image data
				vk::Buffer stagingBuffer;
				vk::DeviceMemory stagingMemory;

				vk::BufferCreateInfo bufferCreateInfo;
				bufferCreateInfo.size = tex2D.size();
				// This buffer is used as a transfer source for the buffer copy
				bufferCreateInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
				bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

				stagingBuffer = device.createBuffer(bufferCreateInfo);

				// Get memory requirements for the staging buffer (alignment, memory type bits)
				memReqs = device.getBufferMemoryRequirements(stagingBuffer);

				memAllocInfo.allocationSize = memReqs.size;
				// Get memory type index for a host visible buffer
				memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);

				stagingMemory = device.allocateMemory(memAllocInfo);
				device.bindBufferMemory(stagingBuffer, stagingMemory, 0);

				// Copy texture data into staging buffer
				void *data = device.mapMemory(stagingMemory, 0, memReqs.size, vk::MemoryMapFlags());
				memcpy(data, tex2D.data(), tex2D.size());
				device.unmapMemory(stagingMemory);

				// Setup buffer copy regions for each mip level
				std::vector<vk::BufferImageCopy> bufferCopyRegions;
				uint32_t offset = 0;

				for (uint32_t i = 0; i < texture->mipLevels; i++)
				{
					vk::BufferImageCopy bufferCopyRegion;
					bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
					bufferCopyRegion.imageSubresource.mipLevel = i;
					bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = tex2D[i].dimensions().x;
					bufferCopyRegion.imageExtent.height = tex2D[i].dimensions().y;
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);

					offset += tex2D[i].size();
				}

				// Create optimal tiled target image
				vk::ImageCreateInfo imageCreateInfo;
				imageCreateInfo.imageType = vk::ImageType::e2D;
				imageCreateInfo.format = format;
				imageCreateInfo.mipLevels = texture->mipLevels;
				imageCreateInfo.arrayLayers = 1;
				imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
				imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
				imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled;
				imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
				imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
				imageCreateInfo.extent = vk::Extent3D { texture->width, texture->height, 1 };
				imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;

				texture->image = device.createImage(imageCreateInfo);

				memReqs = device.getImageMemoryRequirements(texture->image);

				memAllocInfo.allocationSize = memReqs.size;

				memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
				texture->deviceMemory = device.allocateMemory(memAllocInfo);
				device.bindImageMemory(texture->image, texture->deviceMemory, 0);

				vk::ImageSubresourceRange subresourceRange;
				subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
				subresourceRange.baseMipLevel = 0;
				subresourceRange.levelCount = texture->mipLevels;
				subresourceRange.layerCount = 1;

				// Image barrier for optimal image (target)
				// Optimal image will be used as destination for the copy
				setImageLayout(
					cmdBuffer,
					texture->image,
					vk::ImageAspectFlagBits::eColor,
					vk::ImageLayout::ePreinitialized,
					vk::ImageLayout::eTransferDstOptimal,
					subresourceRange);

				// Copy mip levels from staging buffer
				cmdBuffer.copyBufferToImage(stagingBuffer, texture->image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegions.size(), bufferCopyRegions.data());

				// Change texture image layout to shader read after all mip levels have been copied
				texture->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
				setImageLayout(
					cmdBuffer,
					texture->image,
					vk::ImageAspectFlagBits::eColor,
					vk::ImageLayout::eTransferDstOptimal,
					texture->imageLayout,
					subresourceRange);

				// Submit command buffer containing copy and image layout commands
				cmdBuffer.end();

				// Create a fence to make sure that the copies have finished before continuing
				vk::Fence copyFence;
				vk::FenceCreateInfo fenceCreateInfo;
				copyFence = device.createFence(fenceCreateInfo);

				vk::SubmitInfo submitInfo;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &cmdBuffer;

				queue.submit(submitInfo, copyFence);

				device.waitForFences(copyFence, VK_TRUE, DEFAULT_FENCE_TIMEOUT);

				device.destroyFence(copyFence);

				// Clean up staging resources
				device.freeMemory(stagingMemory);
				device.destroyBuffer(stagingBuffer);
			}
			else
			{
				// Prefer using optimal tiling, as linear tiling 
				// may support only a small set of features 
				// depending on implementation (e.g. no mip maps, only one layer, etc.)

				// Check if this support is supported for linear tiling
				assert(formatProperties.linearTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage);

				vk::Image mappableImage;
				vk::DeviceMemory mappableMemory;

				vk::ImageCreateInfo imageCreateInfo;
				imageCreateInfo.imageType = vk::ImageType::e2D;
				imageCreateInfo.format = format;
                                imageCreateInfo.extent = vk::Extent3D { texture->width, texture->height, 1 };
				imageCreateInfo.mipLevels = 1;
				imageCreateInfo.arrayLayers = 1;
				imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
				imageCreateInfo.tiling = vk::ImageTiling::eLinear;
				imageCreateInfo.usage = (useStaging) ? vk::ImageUsageFlagBits::eTransferSrc : vk::ImageUsageFlagBits::eSampled;
				imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
				imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;

				// Load mip map level 0 to linear tiling image
				mappableImage = device.createImage(imageCreateInfo);

				// Get memory requirements for this image 
				// like size and alignment
				memReqs = device.getImageMemoryRequirements(mappableImage);
				// Set memory allocation size to required memory size
				memAllocInfo.allocationSize = memReqs.size;

				// Get memory type that can be mapped to host memory
				memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);

				// Allocate host memory
				mappableMemory = device.allocateMemory(memAllocInfo);

				// Bind allocated image for use
				device.bindImageMemory(mappableImage, mappableMemory, 0);

				// Get sub resource layout
				// Mip map count, array layer, etc.
				vk::ImageSubresource subRes;
				subRes.aspectMask = vk::ImageAspectFlagBits::eColor;
				subRes.mipLevel = 0;

				vk::SubresourceLayout subResLayout;
				void *data;

				// Get sub resources layout 
				// Includes row pitch, size offsets, etc.
				subResLayout = device.getImageSubresourceLayout(mappableImage, subRes);

				// Map image memory
				data = device.mapMemory(mappableMemory, 0, memReqs.size, vk::MemoryMapFlags());

				// Copy image data into memory
				memcpy(data, tex2D[subRes.mipLevel].data(), tex2D[subRes.mipLevel].size());

				device.unmapMemory(mappableMemory);

				// Linear tiled images don't need to be staged
				// and can be directly used as textures
				texture->image = mappableImage;
				texture->deviceMemory = mappableMemory;
				texture->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

				// Setup image memory barrier
				setImageLayout(
					cmdBuffer,
					texture->image, 
					vk::ImageAspectFlagBits::eColor, 
					vk::ImageLayout::ePreinitialized, 
					texture->imageLayout);

				// Submit command buffer containing copy and image layout commands
				cmdBuffer.end();

				vk::Fence nullFence = { VK_NULL_HANDLE };

				vk::SubmitInfo submitInfo;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &cmdBuffer;

				queue.submit(submitInfo, nullFence);
				queue.waitIdle();
			}

			// Create sampler
			vk::SamplerCreateInfo sampler;
			sampler.magFilter = vk::Filter::eLinear;
			sampler.minFilter = vk::Filter::eLinear;
			sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
			sampler.addressModeU = vk::SamplerAddressMode::eRepeat;
			sampler.addressModeV = vk::SamplerAddressMode::eRepeat;
			sampler.addressModeW = vk::SamplerAddressMode::eRepeat;
			sampler.mipLodBias = 0.0f;
			sampler.compareOp = vk::CompareOp::eNever;
			sampler.minLod = 0.0f;
			// Max level-of-detail should match mip level count
			sampler.maxLod = (useStaging) ? (float)texture->mipLevels : 0.0f;
			// Enable anisotropic filtering
			sampler.maxAnisotropy = 8;
			sampler.anisotropyEnable = VK_TRUE;
			sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
			texture->sampler = device.createSampler(sampler);
			
			// Create image view
			// Textures are not directly accessed by the shaders and
			// are abstracted by image views containing additional
			// information and sub resource ranges
			vk::ImageViewCreateInfo view;
			view.viewType = vk::ImageViewType::e2D;
			view.format = format;
			view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
			view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
			// Linear tiling usually won't support mip maps
			// Only set mip map count if optimal tiling is used
			view.subresourceRange.levelCount = (useStaging) ? texture->mipLevels : 1;
			view.image = texture->image;
			texture->view = device.createImageView(view);
		}

		// Clean up vulkan resources used by a texture object
		void destroyTexture(VulkanTexture texture)
		{
			device.destroyImageView(texture.view);
			device.destroyImage(texture.image);
			device.destroySampler(texture.sampler);
			device.freeMemory(texture.deviceMemory);
		}

		VulkanTextureLoader(vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue queue, vk::CommandPool cmdPool)
		{
			this->physicalDevice = physicalDevice;
			this->device = device;
			this->queue = queue;
			this->cmdPool = cmdPool;
			deviceMemoryProperties = physicalDevice.getMemoryProperties();

			// Create command buffer for submitting image barriers
			// and converting tilings
			vk::CommandBufferAllocateInfo cmdBufInfo;
			cmdBufInfo.commandPool = cmdPool;
			cmdBufInfo.level = vk::CommandBufferLevel::ePrimary;
			cmdBufInfo.commandBufferCount = 1;

			cmdBuffer = device.allocateCommandBuffers(cmdBufInfo)[0];
		}

		~VulkanTextureLoader()
		{
			device.freeCommandBuffers(cmdPool, cmdBuffer);
		}

		// Load a cubemap texture (single file)
		void loadCubemap(std::string filename, vk::Format format, VulkanTexture *texture)
		{
#if defined(__ANDROID__)
			assert(assetManager != nullptr);

			// Textures are stored inside the apk on Android (compressed)
			// So they need to be loaded via the asset manager
			AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);

			void *textureData = malloc(size);
			AAsset_read(asset, textureData, size);
			AAsset_close(asset);

			gli::textureCube texCube(gli::load((const char*)textureData, size));

			free(textureData);
#else
			gli::textureCube texCube(gli::load(filename));
#endif	
			assert(!texCube.empty());

			texture->width = (uint32_t)texCube[0].dimensions().x;
			texture->height = (uint32_t)texCube[0].dimensions().y;

			vk::MemoryAllocateInfo memAllocInfo;
			vk::MemoryRequirements memReqs;

			// Create a host-visible staging buffer that contains the raw image data
			vk::Buffer stagingBuffer;
			vk::DeviceMemory stagingMemory;

			vk::BufferCreateInfo bufferCreateInfo;
			bufferCreateInfo.size = texCube.size();
			// This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
			bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

			stagingBuffer = device.createBuffer(bufferCreateInfo);

			// Get memory requirements for the staging buffer (alignment, memory type bits)
			memReqs = device.getBufferMemoryRequirements(stagingBuffer);

			memAllocInfo.allocationSize = memReqs.size;
			// Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);

			stagingMemory = device.allocateMemory(memAllocInfo);
			device.bindBufferMemory(stagingBuffer, stagingMemory, 0);

			// Copy texture data into staging buffer
			void *data = device.mapMemory(stagingMemory, 0, memReqs.size, vk::MemoryMapFlags());
			memcpy(data, texCube.data(), texCube.size());
			device.unmapMemory(stagingMemory);

			// Setup buffer copy regions for the cube faces
			// As all faces of a cube map must have the same dimensions, we can do a single copy
			vk::BufferImageCopy bufferCopyRegion;
			bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 6;
			bufferCopyRegion.imageExtent.width = texture->width;
			bufferCopyRegion.imageExtent.height = texture->height;
			bufferCopyRegion.imageExtent.depth = 1;

			// Create optimal tiled target image
			vk::ImageCreateInfo imageCreateInfo;
			imageCreateInfo.imageType = vk::ImageType::e2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
			imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
			imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled;
			imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
			imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
                        imageCreateInfo.extent = vk::Extent3D { texture->width, texture->height, 1 };
			imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
			// Cube faces count as array layers in Vulkan
			imageCreateInfo.arrayLayers = 6;
			// This flag is required for cube map images
			imageCreateInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;

			texture->image = device.createImage(imageCreateInfo);

			memReqs = device.getImageMemoryRequirements(texture->image);

			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

			texture->deviceMemory = device.allocateMemory(memAllocInfo);
			device.bindImageMemory(texture->image, texture->deviceMemory, 0);

			vk::CommandBufferBeginInfo cmdBufInfo;
			cmdBuffer.begin(cmdBufInfo);

			// Image barrier for optimal image (target)
			// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 6;

			vkTools::setImageLayout(
				cmdBuffer,
				texture->image,
				vk::ImageAspectFlagBits::eColor,
				vk::ImageLayout::ePreinitialized,
				vk::ImageLayout::eTransferDstOptimal,
				subresourceRange);

			// Copy the cube map faces from the staging buffer to the optimal tiled image
			cmdBuffer.copyBufferToImage(stagingBuffer, texture->image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegion);

			// Change texture image layout to shader read after all faces have been copied
			texture->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			vkTools::setImageLayout(
				cmdBuffer,
				texture->image,
				vk::ImageAspectFlagBits::eColor,
				vk::ImageLayout::eTransferDstOptimal,
				texture->imageLayout,
				subresourceRange);

			cmdBuffer.end();

			// Create a fence to make sure that the copies have finished before continuing
			vk::Fence copyFence;
			vk::FenceCreateInfo fenceCreateInfo;
			copyFence = device.createFence(fenceCreateInfo);

			vk::SubmitInfo submitInfo;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmdBuffer;

			queue.submit(submitInfo, copyFence);

			device.waitForFences(copyFence, VK_TRUE, DEFAULT_FENCE_TIMEOUT);

			device.destroyFence(copyFence);

			// Create sampler
			vk::SamplerCreateInfo sampler;
			sampler.magFilter = vk::Filter::eLinear;
			sampler.minFilter = vk::Filter::eLinear;
			sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
			sampler.addressModeU = vk::SamplerAddressMode::eClampToEdge;
			sampler.addressModeV = sampler.addressModeU;
			sampler.addressModeW = sampler.addressModeU;
			sampler.mipLodBias = 0.0f;
			sampler.maxAnisotropy = 8;
			sampler.compareOp = vk::CompareOp::eNever;
			sampler.minLod = 0.0f;
			sampler.maxLod = 0.0f;
			sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
			texture->sampler = device.createSampler(sampler);

			// Create image view
			vk::ImageViewCreateInfo view;
			view.image;
			view.viewType = vk::ImageViewType::eCube;
			view.format = format;
			view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
			view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
			view.subresourceRange.layerCount = 6;
			view.image = texture->image;
			texture->view = device.createImageView(view);

			// Clean up staging resources
			device.freeMemory(stagingMemory);
			device.destroyBuffer(stagingBuffer);
		}

		// Load an array texture (single file)
		void loadTextureArray(std::string filename, vk::Format format, VulkanTexture *texture)
		{
#if defined(__ANDROID__)
			assert(assetManager != nullptr);

			// Textures are stored inside the apk on Android (compressed)
			// So they need to be loaded via the asset manager
			AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
			assert(asset);
			size_t size = AAsset_getLength(asset);
			assert(size > 0);

			void *textureData = malloc(size);
			AAsset_read(asset, textureData, size);
			AAsset_close(asset);

			gli::texture2DArray tex2DArray(gli::load((const char*)textureData, size));

			free(textureData);
#else
			gli::texture2DArray tex2DArray(gli::load(filename));
#endif	

			assert(!tex2DArray.empty());

			texture->width = tex2DArray.dimensions().x;
			texture->height = tex2DArray.dimensions().y;
			texture->layerCount = tex2DArray.layers();

			vk::MemoryAllocateInfo memAllocInfo;
			vk::MemoryRequirements memReqs;

			// Create a host-visible staging buffer that contains the raw image data
			vk::Buffer stagingBuffer;
			vk::DeviceMemory stagingMemory;

			vk::BufferCreateInfo bufferCreateInfo;
			bufferCreateInfo.size = tex2DArray.size();
			// This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
			bufferCreateInfo.sharingMode = vk::SharingMode::eExclusive;

			stagingBuffer = device.createBuffer(bufferCreateInfo);

			// Get memory requirements for the staging buffer (alignment, memory type bits)
			memReqs = device.getBufferMemoryRequirements(stagingBuffer);

			memAllocInfo.allocationSize = memReqs.size;
			// Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible);

			stagingMemory = device.allocateMemory(memAllocInfo);
			device.bindBufferMemory(stagingBuffer, stagingMemory, 0);

			// Copy texture data into staging buffer
			void *data = device.mapMemory(stagingMemory, 0, memReqs.size, vk::MemoryMapFlags());
			memcpy(data, tex2DArray.data(), tex2DArray.size());
			device.unmapMemory(stagingMemory);

			// Setup buffer copy regions for array layers
			std::vector<vk::BufferImageCopy> bufferCopyRegions;
			uint32_t offset = 0;

			// Check if all array layers have the same dimesions
			bool sameDims = true;
			for (uint32_t layer = 0; layer < texture->layerCount; layer++)
			{
				if (tex2DArray[layer].dimensions().x != texture->width || tex2DArray[layer].dimensions().y != texture->height)
				{
					sameDims = false;
					break;
				}
			}

			// If all layers of the texture array have the same dimensions, we only need to do one copy
			if (sameDims)
			{
				vk::BufferImageCopy bufferCopyRegion;
				bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				bufferCopyRegion.imageSubresource.mipLevel = 0;
				bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
				bufferCopyRegion.imageSubresource.layerCount = texture->layerCount;
				bufferCopyRegion.imageExtent.width = tex2DArray[0].dimensions().x;
				bufferCopyRegion.imageExtent.height = tex2DArray[0].dimensions().y;
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = offset;

				bufferCopyRegions.push_back(bufferCopyRegion);
			}
			else
			{
				// If dimensions differ, copy layer by layer and pass offsets
				for (uint32_t layer = 0; layer < texture->layerCount; layer++)
				{
					vk::BufferImageCopy bufferCopyRegion;
					bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
					bufferCopyRegion.imageSubresource.mipLevel = 0;
					bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = tex2DArray[layer].dimensions().x;
					bufferCopyRegion.imageExtent.height = tex2DArray[layer].dimensions().y;
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);

					offset += tex2DArray[layer].size();
				}
			}

			// Create optimal tiled target image
			vk::ImageCreateInfo imageCreateInfo;
			imageCreateInfo.imageType = vk::ImageType::e2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
			imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
			imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled;
			imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
			imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
                        imageCreateInfo.extent = vk::Extent3D { texture->width, texture->height, 1 };
			imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
			imageCreateInfo.arrayLayers = texture->layerCount;

			texture->image = device.createImage(imageCreateInfo);

			memReqs = device.getImageMemoryRequirements(texture->image);

			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

			texture->deviceMemory = device.allocateMemory(memAllocInfo);
			device.bindImageMemory(texture->image, texture->deviceMemory, 0);

			vk::CommandBufferBeginInfo cmdBufInfo;
			cmdBuffer.begin(cmdBufInfo);

			// Image barrier for optimal image (target)
			// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
			vk::ImageSubresourceRange subresourceRange;
			subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = texture->layerCount;

			vkTools::setImageLayout(
				cmdBuffer,
				texture->image,
				vk::ImageAspectFlagBits::eColor,
				vk::ImageLayout::ePreinitialized,
				vk::ImageLayout::eTransferDstOptimal,
				subresourceRange);

			// Copy the cube map faces from the staging buffer to the optimal tiled image
			cmdBuffer.copyBufferToImage(stagingBuffer, texture->image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegions.size(), bufferCopyRegions.data());

			// Change texture image layout to shader read after all faces have been copied
			texture->imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			vkTools::setImageLayout(
				cmdBuffer,
				texture->image,
				vk::ImageAspectFlagBits::eColor,
				vk::ImageLayout::eTransferDstOptimal,
				texture->imageLayout,
				subresourceRange);

			cmdBuffer.end();

			// Create a fence to make sure that the copies have finished before continuing
			vk::Fence copyFence;
			vk::FenceCreateInfo fenceCreateInfo;
			copyFence = device.createFence(fenceCreateInfo);

			vk::SubmitInfo submitInfo;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cmdBuffer;

			queue.submit(submitInfo, copyFence);

			device.waitForFences(copyFence, VK_TRUE, DEFAULT_FENCE_TIMEOUT);

			device.destroyFence(copyFence);

			// Create sampler
			vk::SamplerCreateInfo sampler;
			sampler.magFilter = vk::Filter::eLinear;
			sampler.minFilter = vk::Filter::eLinear;
			sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
			sampler.addressModeU = vk::SamplerAddressMode::eClampToEdge;
			sampler.addressModeV = sampler.addressModeU;
			sampler.addressModeW = sampler.addressModeU;
			sampler.mipLodBias = 0.0f;
			sampler.maxAnisotropy = 8;
			sampler.compareOp = vk::CompareOp::eNever;
			sampler.minLod = 0.0f;
			sampler.maxLod = 0.0f;
			sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
			texture->sampler = device.createSampler(sampler);

			// Create image view
			vk::ImageViewCreateInfo view;
			view.image;
			view.viewType = vk::ImageViewType::e2DArray;
			view.format = format;
			view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
			view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
			view.subresourceRange.layerCount = texture->layerCount;
			view.image = texture->image;
			texture->view = device.createImageView(view);

			// Clean up staging resources
			device.freeMemory(stagingMemory);
			device.destroyBuffer(stagingBuffer);
		}



	};

};
