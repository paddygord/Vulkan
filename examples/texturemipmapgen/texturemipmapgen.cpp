/*
* Vulkan Example - Runtime mip map generation
*
* Copyright (C) by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <algorithm>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanModel.hpp"
#include <ktx.h>
#include <ktxvulkan.h>

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    struct Texture {
        vk::Image image;
        vk::DeviceMemory deviceMemory;
        vk::ImageView view;
        uint32_t width, height;
        uint32_t mipLevels;
    } texture;

    // To demonstrate mip mapping and filtering this example uses separate samplers
    std::vector<std::string> samplerNames{ "No mip maps", "Mip maps (bilinear)", "Mip maps (anisotropic)" };
    std::vector<vk::Sampler> samplers;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
    } };

    struct {
        vkx::model::Model tunnel;
    } models;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    vks::Buffer uniformBufferVS;

    struct uboVS {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model;
        glm::vec4 viewPos;
        float lodBias = 0.0f;
        int32_t samplerIndex = 2;
    } uboVS;

    struct {
        vk::Pipeline solid;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Runtime mip map generation";
        camera.type = Camera::CameraType::firstperson;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 1024.0f);
        camera.setRotation(glm::vec3(0.0f, 90.0f, 0.0f));
        camera.setTranslation(glm::vec3(40.75f, 0.0f, 0.0f));
        camera.movementSpeed = 2.5f;
        camera.rotationSpeed = 0.5f;
        settings.overlay = true;
        timerSpeed *= 0.05f;
        paused = true;
    }

    ~VulkanExample() {
        destroyTextureImage(texture);
        vkDestroyPipeline(device, pipelines.solid, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        uniformBufferVS.destroy();
        for (auto sampler : samplers) {
            vkDestroySampler(device, sampler, nullptr);
        }
        models.tunnel.destroy();
    }

    virtual void getEnabledFeatures() {
        if (deviceFeatures.samplerAnisotropy) {
            enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
    }

    void loadTexture(std::string filename, vk::Format format, bool forceLinearTiling) {
        ktxResult result;
        ktxTexture* ktxTexture;

#if defined(__ANDROID__)
        // Textures are stored inside the apk on Android (compressed)
        // So they need to be loaded via the asset manager
        AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
        if (!asset) {
            vks::tools::exitFatal("Could not load texture from " + filename +
                                      "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download "
                                      "the latest version.",
                                  -1);
        }
        size_t size = AAsset_getLength(asset);
        assert(size > 0);

        ktx_uint8_t* textureData = new ktx_uint8_t[size];
        AAsset_read(asset, textureData, size);
        AAsset_close(asset);
        result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
        delete[] textureData;
#else
        if (!vks::tools::fileExists(filename)) {
            vks::tools::exitFatal("Could not load texture from " + filename +
                                      "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download "
                                      "the latest version.",
                                  -1);
        }
        result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif
        assert(result == KTX_SUCCESS);

        texture.width = ktxTexture->baseWidth;
        texture.height = ktxTexture->baseHeight;
        ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
        ktx_size_t ktxTextureSize = ktxTexture_GetImageSize(ktxTexture, 0);

        // calculate num of mip maps
        // numLevels = 1 + floor(log2(max(w, h, d)))
        // Calculated as log2(max(width, height, depth))c + 1 (see specs)
        texture.mipLevels = floor(log2(std::max(texture.width, texture.height))) + 1;

        // Get device properites for the requested texture format
        vk::FormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
        // Mip-chain generation requires support for blit source and destination
        assert(formatProperties.optimalTilingFeatures & vk::Format::eFEATURE_BLIT_SRC_BIT);
        assert(formatProperties.optimalTilingFeatures & vk::Format::eFEATURE_BLIT_DST_BIT);

        vk::MemoryAllocateInfo memAllocInfo;
        vk::MemoryRequirements memReqs = {};

        // Create a host-visible staging buffer that contains the raw image data
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        vk::BufferCreateInfo bufferCreateInfo;
        bufferCreateInfo.size = ktxTextureSize;
        // This buffer is used as a transfer source for the buffer copy
        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));
        vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex =
            vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingMemory));
        VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

        // Copy texture data into staging buffer
        uint8_t* data;
        VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, memReqs.size, 0, (void**)&data));
        memcpy(data, ktxTextureData, ktxTextureSize);
        vkUnmapMemory(device, stagingMemory);

        // Create optimal tiled target image
        vk::ImageCreateInfo imageCreateInfo;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.mipLevels = texture.mipLevels;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.extent = { texture.width, texture.height, 1 };
        imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &texture.image));
        vkGetImageMemoryRequirements(device, texture.image, &memReqs);
        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &texture.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, texture.image, texture.deviceMemory, 0));

        vk::CommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        vk::ImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 1;

        // Optimal image will be used as destination for the copy, so we must transfer from our initial undefined image layout to the transfer destination layout
        vks::tools::insertImageMemoryBarrier(copyCmd, texture.image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             subresourceRange);

        // Copy the first mip of the chain, remaining mips will be generated
        vk::BufferImageCopy bufferCopyRegion = {};
        bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bufferCopyRegion.imageSubresource.mipLevel = 0;
        bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
        bufferCopyRegion.imageSubresource.layerCount = 1;
        bufferCopyRegion.imageExtent.width = texture.width;
        bufferCopyRegion.imageExtent.height = texture.height;
        bufferCopyRegion.imageExtent.depth = 1;

        vkCmdCopyBufferToImage(copyCmd, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

        // Transition first mip level to transfer source for read during blit
        vks::tools::insertImageMemoryBarrier(copyCmd, texture.image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             VK_PIPELINE_STAGE_TRANSFER_BIT, subresourceRange);

        VulkanExampleBase::flushCommandBuffer(copyCmd, queue, true);

        // Clean up staging resources
        vkFreeMemory(device, stagingMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        ktxTexture_Destroy(ktxTexture);

        // Generate the mip chain
        // ---------------------------------------------------------------
        // We copy down the whole mip chain doing a blit from mip-1 to mip
        // An alternative way would be to always blit from the first mip level and sample that one down
        vk::CommandBuffer blitCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        // Copy down mips from n-1 to n
        for (int32_t i = 1; i < texture.mipLevels; i++) {
            vk::ImageBlit imageBlit{};

            // Source
            imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBlit.srcSubresource.layerCount = 1;
            imageBlit.srcSubresource.mipLevel = i - 1;
            imageBlit.srcOffsets[1].x = int32_t(texture.width >> (i - 1));
            imageBlit.srcOffsets[1].y = int32_t(texture.height >> (i - 1));
            imageBlit.srcOffsets[1].z = 1;

            // Destination
            imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageBlit.dstSubresource.layerCount = 1;
            imageBlit.dstSubresource.mipLevel = i;
            imageBlit.dstOffsets[1].x = int32_t(texture.width >> i);
            imageBlit.dstOffsets[1].y = int32_t(texture.height >> i);
            imageBlit.dstOffsets[1].z = 1;

            vk::ImageSubresourceRange mipSubRange = {};
            mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipSubRange.baseMipLevel = i;
            mipSubRange.levelCount = 1;
            mipSubRange.layerCount = 1;

            // Prepare current mip level as image blit destination
            vks::tools::insertImageMemoryBarrier(blitCmd, texture.image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                 mipSubRange);

            // Blit from previous level
            vkCmdBlitImage(blitCmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit,
                           VK_FILTER_LINEAR);

            // Prepare current mip level as image blit source for next level
            vks::tools::insertImageMemoryBarrier(blitCmd, texture.image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                 VK_PIPELINE_STAGE_TRANSFER_BIT, mipSubRange);
        }

        // After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
        subresourceRange.levelCount = texture.mipLevels;
        vks::tools::insertImageMemoryBarrier(blitCmd, texture.image, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, subresourceRange);

        VulkanExampleBase::flushCommandBuffer(blitCmd, queue, true);
        // ---------------------------------------------------------------

        // Create samplers
        samplers.resize(3);
        vk::SamplerCreateInfo sampler;
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        sampler.mipLodBias = 0.0f;
        sampler.compareOp = VK_COMPARE_OP_NEVER;
        sampler.minLod = 0.0f;
        sampler.maxLod = 0.0f;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler.maxAnisotropy = 1.0;
        sampler.anisotropyEnable = VK_FALSE;

        // Without mip mapping
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &samplers[0]));

        // With mip mapping
        sampler.maxLod = (float)texture.mipLevels;
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &samplers[1]));

        // With mip mapping and anisotropic filtering
        if (vulkanDevice->features.samplerAnisotropy) {
            sampler.maxAnisotropy = vulkanDevice->properties.limits.maxSamplerAnisotropy;
            sampler.anisotropyEnable = VK_TRUE;
        }
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &samplers[2]));

        // Create image view
        vk::ImageViewCreateInfo view;
        view.image = texture.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        view.subresourceRange.levelCount = texture.mipLevels;
        VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &texture.view));
    }

    // Free all Vulkan resources used a texture object
    void destroyTextureImage(Texture texture) {
        vkDestroyImageView(device, texture.view, nullptr);
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.deviceMemory, nullptr);
    }

    void buildCommandBuffers() {
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

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            vk::Rect2D scissor{ width, height, 0, 0 };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.solid);

            vk::DeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.tunnel.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.tunnel.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(drawCmdBuffers[i], models.tunnel.indexCount, 1, 0, 0, 0);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void draw() {
        VulkanExampleBase::prepareFrame();

        // Command buffer to be sumitted to the queue
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

        // Submit to queue
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

        VulkanExampleBase::submitFrame();
    }

    void loadAssets() {
        models.tunnel.loadFromFile(context, getAssetPath() + "models/tunnel_cylinder.dae", vertexLayout, 1.0f);
        loadTexture(getAssetPath() + "textures/metalplate_nomips_rgba.ktx", vk::Format::eR8G8B8A8Unorm, false);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vk::vertexInputBindingDescription{VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex};

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(3);
        // Location 0 : Position
        vertices.attributeDescriptions[0] = vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32sFloat, 0};
        // Location 1 : Texture coordinates
        vertices.attributeDescriptions[1] =
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32sFloat, 3 * sizeof(float)};
        // Location 1 : Vertex normal
        vertices.attributeDescriptions[2] =
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32sFloat, 5 * sizeof(float)};

        vertices.inputState;
        vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

        void setupDescriptorPool()
	{
		std::vector<vk::DescriptorPoolSize> poolSizes =
		{
			{ vk::DescriptorType::eUniformBuffer, 1 },	// Vertex shader UBO
			{ vk::DescriptorType::eSAMPLED_IMAGE, 1 },		// Sampled image
			{ vk::DescriptorType::eSAMPLER, 3),			// 3 samplers (array }
		};

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vk::descriptorPoolCreateInfo{static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 1};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
}

	void setupDescriptorSetLayout()
	{
    std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings;

    // Binding 0: Vertex shader uniform buffer
    setLayoutBindings.push_back(vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0)};

    // Binding 1: Sampled image
    setLayoutBindings.push_back(vk::descriptorSetLayoutBinding{vk::DescriptorType::eSAMPLED_IMAGE, vk::ShaderStageFlagBits::eFragment, 1)};

    // Binding 2: Sampler array (3 descriptors)
    setLayoutBindings.push_back(vk::descriptorSetLayoutBinding{vk::DescriptorType::eSAMPLER, vk::ShaderStageFlagBits::eFragment, 2, 3)};

    vk::DescriptorSetLayoutCreateInfo descriptorLayout =
        vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size())};

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

    vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&descriptorSetLayout, 1};

    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
}

void setupDescriptorSet() {
    vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, &descriptorSetLayout, 1 };
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

    vk::DescriptorImageInfo textureDescriptor{ VK_NULL_HANDLE, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {

        // Binding 0: Vertex shader uniform buffer
        { descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformBufferVS.descriptor },
        // Binding 1: Sampled image
        { descriptorSet, vk::DescriptorType::eSAMPLED_IMAGE, 1, &textureDescriptor }
    };

    // Binding 2: Sampler array
    std::vector<vk::DescriptorImageInfo> samplerDescriptors;
    for (auto i = 0; i < samplers.size(); i++) {
                        samplerDescriptors.push_back({ samplers[i], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) };
    }
    vk::WriteDescriptorSet samplerDescriptorWrite{};
    samplerDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    samplerDescriptorWrite.dstSet = descriptorSet;
    samplerDescriptorWrite.descriptorType = vk::DescriptorType::eSAMPLER;
    samplerDescriptorWrite.descriptorCount = static_cast<uint32_t>(samplerDescriptors.size());
    samplerDescriptorWrite.pImageInfo = samplerDescriptors.data();
    samplerDescriptorWrite.dstBinding = 2;
    samplerDescriptorWrite.dstArrayElement = 0;
    writeDescriptorSets.push_back(samplerDescriptorWrite);
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
}

void preparePipelines() {
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
        vk::pipelineInputAssemblyStateCreateInfo{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE};

    vk::PipelineRasterizationStateCreateInfo rasterizationState =
        vk::pipelineRasterizationStateCreateInfo{VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0};

    vk::PipelineColorBlendAttachmentState blendAttachmentState = vk::pipelineColorBlendAttachmentState{0xf, VK_FALSE};

    vk::PipelineColorBlendStateCreateInfo colorBlendState = vk::pipelineColorBlendStateCreateInfo{1, &blendAttachmentState};

    vk::PipelineDepthStencilStateCreateInfo depthStencilState =
        vk::pipelineDepthStencilStateCreateInfo{VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL};

    vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

    vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

    std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    vk::PipelineDynamicStateCreateInfo dynamicState =
        vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0};

    // Load shaders
    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

    shaderStages[0] = loadShader(getAssetPath() + "shaders/texturemipmapgen/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
    shaderStages[1] = loadShader(getAssetPath() + "shaders/texturemipmapgen/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);

    vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::pipelineCreateInfo{pipelineLayout, renderPass, 0};

    pipelineCreateInfo.pVertexInputState = &vertices.inputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pRasterizationState = &rasterizationState;
    pipelineCreateInfo.pColorBlendState = &colorBlendState;
    pipelineCreateInfo.pMultisampleState = &multisampleState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid));
}

// Prepare and initialize uniform buffer containing shader uniforms
void prepareUniformBuffers() {
    // Vertex shader uniform buffer block
    VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                               &uniformBufferVS, sizeof(uboVS), &uboVS));

    updateUniformBuffers();
}

void updateUniformBuffers() {
    uboVS.projection = camera.matrices.perspective;
    uboVS.view = camera.matrices.view;
    uboVS.model = glm::rotate(glm::mat4(1.0f), glm::radians(timer * 360.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    uboVS.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f);
    VK_CHECK_RESULT(uniformBufferVS.map());
    memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
    uniformBufferVS.unmap();
}

void prepare() {
    VulkanExampleBase::prepare();

    setupVertexDescriptions();
    prepareUniformBuffers();
    setupDescriptorSetLayout();
    preparePipelines();
    setupDescriptorPool();
    setupDescriptorSet();
    buildCommandBuffers();
    prepared = true;
}

virtual void render() {
    if (!prepared)
        return;
    draw();
    if (!paused) {
        updateUniformBuffers();
    }
}

virtual void viewChanged() {
    updateUniformBuffers();
}

virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
    if (overlay->header("Settings")) {
        if (overlay->sliderFloat("LOD bias", &uboVS.lodBias, 0.0f, (float)texture.mipLevels)) {
            updateUniformBuffers();
        }
        if (overlay->comboBox("Sampler type", &uboVS.samplerIndex, samplerNames)) {
            updateUniformBuffers();
        }
    }
}
}
;

VULKAN_EXAMPLE_MAIN()
