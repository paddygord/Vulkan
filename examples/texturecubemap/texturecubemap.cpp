/*
* Vulkan Example - Cube map texture loading and displaying
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
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"
#include <ktx.h>
#include <ktxvulkan.h>

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    bool displaySkybox = true;

    vks::Texture cubeMap;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_UV,
    } };

    struct Meshes {
        vkx::model::Model skybox;
        std::vector<vkx::model::Model> objects;
        int32_t objectIndex = 0;
    } models;

    struct {
        vks::Buffer object;
        vks::Buffer skybox;
    } uniformBuffers;

    struct UBOVS {
        glm::mat4 projection;
        glm::mat4 model;
        float lodBias = 0.0f;
    } uboVS;

    struct {
        vk::Pipeline skybox;
        vk::Pipeline reflect;
    } pipelines;

    struct {
        vk::DescriptorSet object;
        vk::DescriptorSet skybox;
    } descriptorSets;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSetLayout descriptorSetLayout;

    std::vector<std::string> objectNames;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        zoom = -4.0f;
        rotationSpeed = 0.25f;
        rotation = { -7.25f, -120.0f, 0.0f };
        title = "Cube map textures";
        settings.overlay = true;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class

        // Clean up texture resources
        vkDestroyImageView(device, cubeMap.view, nullptr);
        vkDestroyImage(device, cubeMap.image, nullptr);
        vkDestroySampler(device, cubeMap.sampler, nullptr);
        vkFreeMemory(device, cubeMap.deviceMemory, nullptr);

        vkDestroyPipeline(device, pipelines.skybox, nullptr);
        vkDestroyPipeline(device, pipelines.reflect, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        for (auto& model : models.objects) {
            model.destroy();
        }
        models.skybox.destroy();

        uniformBuffers.object.destroy();
        uniformBuffers.skybox.destroy();
    }

    // Enable physical device features required for this example
    virtual void getEnabledFeatures() {
        if (deviceFeatures.samplerAnisotropy) {
            enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
        if (deviceFeatures.textureCompressionBC) {
            enabledFeatures.textureCompressionBC = VK_TRUE;
        } else if (deviceFeatures.textureCompressionASTC_LDR) {
            enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
        } else if (deviceFeatures.textureCompressionETC2) {
            enabledFeatures.textureCompressionETC2 = VK_TRUE;
        }
    };

    void loadCubemap(std::string filename, vk::Format format, bool forceLinearTiling) {
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

        // Get properties required for using and upload texture data from the ktx texture object
        cubeMap.width = ktxTexture->baseWidth;
        cubeMap.height = ktxTexture->baseHeight;
        cubeMap.mipLevels = ktxTexture->numLevels;
        ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
        ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

        vk::MemoryAllocateInfo memAllocInfo;
        vk::MemoryRequirements memReqs;

        // Create a host-visible staging buffer that contains the raw image data
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        vk::BufferCreateInfo bufferCreateInfo;
        bufferCreateInfo.size = ktxTextureSize;
        // This buffer is used as a transfer source for the buffer copy
        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));

        // Get memory requirements for the staging buffer (alignment, memory type bits)
        vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
        memAllocInfo.allocationSize = memReqs.size;
        // Get memory type index for a host visible buffer
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
        imageCreateInfo.mipLevels = cubeMap.mipLevels;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.extent = { cubeMap.width, cubeMap.height, 1 };
        imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        // Cube faces count as array layers in Vulkan
        imageCreateInfo.arrayLayers = 6;
        // This flag is required for cube map images
        imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &cubeMap.image));

        vkGetImageMemoryRequirements(device, cubeMap.image, &memReqs);

        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &cubeMap.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, cubeMap.image, cubeMap.deviceMemory, 0));

        vk::CommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        // Setup buffer copy regions for each face including all of it's miplevels
        std::vector<vk::BufferImageCopy> bufferCopyRegions;
        uint32_t offset = 0;

        for (uint32_t face = 0; face < 6; face++) {
            for (uint32_t level = 0; level < cubeMap.mipLevels; level++) {
                // Calculate offset into staging buffer for the current mip level and face
                ktx_size_t offset;
                assert(ktxTexture_GetImageOffset(ktxTexture, level, 0, face, &offset) == KTX_SUCCESS);
                vk::BufferImageCopy bufferCopyRegion = {};
                bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                bufferCopyRegion.imageSubresource.mipLevel = level;
                bufferCopyRegion.imageSubresource.baseArrayLayer = face;
                bufferCopyRegion.imageSubresource.layerCount = 1;
                bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> level;
                bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> level;
                bufferCopyRegion.imageExtent.depth = 1;
                bufferCopyRegion.bufferOffset = offset;
                bufferCopyRegions.push_back(bufferCopyRegion);
            }
        }

        // Image barrier for optimal image (target)
        // Set initial layout for all array layers (faces) of the optimal (target) tiled texture
        vk::ImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = cubeMap.mipLevels;
        subresourceRange.layerCount = 6;

        vks::tools::setImageLayout(copyCmd, cubeMap.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

        // Copy the cube map faces from the staging buffer to the optimal tiled image
        vkCmdCopyBufferToImage(copyCmd, stagingBuffer, cubeMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()),
                               bufferCopyRegions.data());

        // Change texture image layout to shader read after all faces have been copied
        cubeMap.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vks::tools::setImageLayout(copyCmd, cubeMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cubeMap.imageLayout, subresourceRange);

        VulkanExampleBase::flushCommandBuffer(copyCmd, queue, true);

        // Create sampler
        vk::SamplerCreateInfo sampler;
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = sampler.addressModeU;
        sampler.addressModeW = sampler.addressModeU;
        sampler.mipLodBias = 0.0f;
        sampler.compareOp = VK_COMPARE_OP_NEVER;
        sampler.minLod = 0.0f;
        sampler.maxLod = cubeMap.mipLevels;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler.maxAnisotropy = 1.0f;
        if (vulkanDevice->features.samplerAnisotropy) {
            sampler.maxAnisotropy = vulkanDevice->properties.limits.maxSamplerAnisotropy;
            sampler.anisotropyEnable = VK_TRUE;
        }
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &cubeMap.sampler));

        // Create image view
        vk::ImageViewCreateInfo view;
        // Cube map view type
        view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view.format = format;
        view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        // 6 array layers (faces)
        view.subresourceRange.layerCount = 6;
        // Set number of mip levels
        view.subresourceRange.levelCount = cubeMap.mipLevels;
        view.image = cubeMap.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &cubeMap.view));

        // Clean up staging resources
        vkFreeMemory(device, stagingMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        ktxTexture_Destroy(ktxTexture);
    }

    void loadTextures() {
        // Vulkan core supports three different compressed texture formats
        // As the support differs between implemementations we need to check device features and select a proper format and file
        std::string filename;
        vk::Format format;
        if (deviceFeatures.textureCompressionBC) {
            filename = "cubemap_yokohama_bc3_unorm.ktx";
            format = vk::Format::eBC2_UNORM_BLOCK;
        } else if (deviceFeatures.textureCompressionASTC_LDR) {
            filename = "cubemap_yokohama_astc_8x8_unorm.ktx";
            format = vk::Format::eASTC_8x8_UNORM_BLOCK;
        } else if (deviceFeatures.textureCompressionETC2) {
            filename = "cubemap_yokohama_etc2_unorm.ktx";
            format = vk::Format::eETC2_R8G8B8_UNORM_BLOCK;
        } else {
            vks::tools::exitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
        }

        loadCubemap(getAssetPath() + "textures/" + filename, format, false);
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

            vk::DeviceSize offsets[1] = { 0 };

            // Skybox
            if (displaySkybox) {
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.skybox, 0, NULL);
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.skybox.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.skybox.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.skybox.indexCount, 1, 0, 0, 0);
            }

            // 3D object
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.object, 0, NULL);
            vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.objects[models.objectIndex].vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.objects[models.objectIndex].indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.reflect);
            vkCmdDrawIndexed(drawCmdBuffers[i], models.objects[models.objectIndex].indexCount, 1, 0, 0, 0);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        // Skybox
        models.skybox.loadFromFile(context, getAssetPath() + "models/cube.obj", vertexLayout, 0.05f);
        // Objects
        std::vector<std::string> filenames = { "sphere.obj", "teapot.dae", "torusknot.obj", "venus.fbx" };
        objectNames = { "Sphere", "Teapot", "Torusknot", "Venus" };
        for (auto file : filenames) {
            vkx::model::Model model;
            model.loadFromFile(context, getAssetPath() + "models/" + file, vertexLayout, 0.05f * (file == "venus.fbx" ? 3.0f : 1.0f));
            models.objects.push_back(model);
        }
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 2 }, { vk::DescriptorType::eCombinedImageSampler, 2 } };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = vk::descriptorPoolCreateInfo{poolSizes.size(), poolSizes.data(), 2};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Vertex shader uniform buffer
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0},
            // Binding 1 : Fragment shader image sampler
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1}
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), setLayoutBindings.size()};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSets() {
        // Image descriptor for the cube map texture
        vk::DescriptorImageInfo textureDescriptor = vk::descriptorImageInfo{cubeMap.sampler, cubeMap.view, cubeMap.imageLayout};

        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &descriptorSetLayout, 1};

        // 3D object descriptor set
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.object));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::writeDescriptorSet{descriptorSets.object, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.object.descriptor},
            // Binding 1 : Fragment shader cubemap sampler
            vk::writeDescriptorSet{descriptorSets.object, vk::DescriptorType::eCombinedImageSampler, 1, &textureDescriptor}
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Sky box descriptor set
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.skybox));

        writeDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::writeDescriptorSet{descriptorSets.skybox, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.skybox.descriptor},
            // Binding 1 : Fragment shader cubemap sampler
            vk::writeDescriptorSet{descriptorSets.skybox, vk::DescriptorType::eCombinedImageSampler, 1, &textureDescriptor}
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vk::pipelineInputAssemblyStateCreateInfo{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE};

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vk::pipelineRasterizationStateCreateInfo{VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0};

        vk::PipelineColorBlendAttachmentState blendAttachmentState = vk::pipelineColorBlendAttachmentState{0xf, VK_FALSE};

        vk::PipelineColorBlendStateCreateInfo colorBlendState = vk::pipelineColorBlendStateCreateInfo{1, &blendAttachmentState};

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vk::pipelineDepthStencilStateCreateInfo{VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL};

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), dynamicStateEnables.size(), 0};

        // Vertex bindings and attributes
        vk::VertexInputBindingDescription vertexInputBinding = { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex };

        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { 0, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 1: Normal
        };

        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo{ pipelineLayout, renderPass, 0 };
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.pVertexInputState = &vertexInputState;

        // Skybox pipeline (background cube)
        shaderStages[0] = loadShader(getAssetPath() + "shaders/texturecubemap/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/texturecubemap/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.skybox));

        // Cube map reflect pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/texturecubemap/reflect.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/texturecubemap/reflect.frag.spv", vk::ShaderStageFlagBits::eFragment);
        // Enable depth test and write
        depthStencilState.depthWriteEnable = VK_TRUE;
        depthStencilState.depthTestEnable = VK_TRUE;
        // Flip cull mode
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.reflect));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Objact vertex shader uniform buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.object,
                                                   sizeof(uboVS)));

        // Skybox vertex shader uniform buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.skybox,
                                                   sizeof(uboVS)));

        // Map persistent
        VK_CHECK_RESULT(uniformBuffers.object.map());
        VK_CHECK_RESULT(uniformBuffers.skybox.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // 3D object
        glm::mat4 viewMatrix = glm::mat4(1.0f);
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
        viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, zoom));

        uboVS.model = glm::mat4(1.0f);
        uboVS.model = viewMatrix * glm::translate(uboVS.model, cameraPos);
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        memcpy(uniformBuffers.object.mapped, &uboVS, sizeof(uboVS));

        // Skybox
        viewMatrix = glm::mat4(1.0f);
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);

        uboVS.model = glm::mat4(1.0f);
        uboVS.model = viewMatrix * glm::translate(uboVS.model, glm::vec3(0, 0, 0));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        memcpy(uniformBuffers.skybox.mapped, &uboVS, sizeof(uboVS));
    }

    void draw() {
        VulkanExampleBase::prepareFrame();

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

        VulkanExampleBase::submitFrame();
    }

    void prepare() {
        VulkanExampleBase::prepare();
        loadTextures();

        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSets();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Settings")) {
            if (overlay->sliderFloat("LOD bias", &uboVS.lodBias, 0.0f, (float)cubeMap.mipLevels)) {
                updateUniformBuffers();
            }
            if (overlay->comboBox("Object type", &models.objectIndex, objectNames)) {
                buildCommandBuffers();
            }
            if (overlay->checkBox("Skybox", &displaySkybox)) {
                buildCommandBuffers();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()