/*
* Vulkan Example - Using subpasses for G-Buffer compositing
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Implements a deferred rendering setup with a forward transparency pass using sub passes
*
* Sub passes allow reading from the previous framebuffer (in the same render pass) at 
* the same pixel position.
* 
* This is a feature that was especially designed for tile-based-renderers 
* (mostly mobile GPUs) and is a new optomization feature in Vulkan for those GPU types.
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

#define NUM_LIGHTS 64

class VulkanExample : public VulkanExampleBase {
public:
    struct {
        vkx::texture::Texture2D glass;
    } textures;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_UV,
    } };

    struct {
        vkx::model::Model scene;
        vkx::model::Model transparent;
    } models;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 view;
    } uboGBuffer;

    struct Light {
        glm::vec4 position;
        glm::vec3 color;
        float radius;
    };

    struct {
        glm::vec4 viewPos;
        Light lights[NUM_LIGHTS];
    } uboLights;

    struct {
        vks::Buffer GBuffer;
        vks::Buffer lights;
    } uniformBuffers;

    struct {
        vk::Pipeline offscreen;
        vk::Pipeline composition;
        vk::Pipeline transparent;
    } pipelines;

    struct {
        vk::PipelineLayout offscreen;
        vk::PipelineLayout composition;
        vk::PipelineLayout transparent;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet scene;
        vk::DescriptorSet composition;
        vk::DescriptorSet transparent;
    } descriptorSets;

    struct {
        vk::DescriptorSetLayout scene;
        vk::DescriptorSetLayout composition;
        vk::DescriptorSetLayout transparent;
    } descriptorSetLayouts;

    // G-Buffer framebuffer attachments
    struct FrameBufferAttachment {
        vk::Image image;
        vk::DeviceMemory mem;
        vk::ImageView view;
        vk::Format format;
    };
    struct Attachments {
        FrameBufferAttachment position, normal, albedo;
    } attachments;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Subpasses";
        camera.type = Camera::CameraType::firstperson;
        camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
        camera.rotationSpeed = 0.25f;
#endif
        camera.setPosition(glm::vec3(-3.2f, 1.0f, 5.9f));
        camera.setRotation(glm::vec3(0.5f, 210.05f, 0.0f));
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
        settings.overlay = true;
        UIOverlay.subpass = 2;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class

        vkDestroyImageView(device, attachments.position.view, nullptr);
        vkDestroyImage(device, attachments.position.image, nullptr);
        vkFreeMemory(device, attachments.position.mem, nullptr);

        vkDestroyImageView(device, attachments.normal.view, nullptr);
        vkDestroyImage(device, attachments.normal.image, nullptr);
        vkFreeMemory(device, attachments.normal.mem, nullptr);

        vkDestroyImageView(device, attachments.albedo.view, nullptr);
        vkDestroyImage(device, attachments.albedo.image, nullptr);
        vkFreeMemory(device, attachments.albedo.mem, nullptr);

        vkDestroyPipeline(device, pipelines.offscreen, nullptr);
        vkDestroyPipeline(device, pipelines.composition, nullptr);
        vkDestroyPipeline(device, pipelines.transparent, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayouts.offscreen, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayouts.composition, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayouts.transparent, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.composition, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.transparent, nullptr);

        textures.glass.destroy();
        models.scene.destroy();
        models.transparent.destroy();
        uniformBuffers.GBuffer.destroy();
        uniformBuffers.lights.destroy();
    }

    // Enable physical device features required for this example
    virtual void getEnabledFeatures() {
        // Enable anisotropic filtering if supported
        if (deviceFeatures.samplerAnisotropy) {
            enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
        // Enable texture compression
        if (deviceFeatures.textureCompressionBC) {
            enabledFeatures.textureCompressionBC = VK_TRUE;
        } else if (deviceFeatures.textureCompressionASTC_LDR) {
            enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
        } else if (deviceFeatures.textureCompressionETC2) {
            enabledFeatures.textureCompressionETC2 = VK_TRUE;
        }
    };

    // Create a frame buffer attachment
    void createAttachment(vk::Format format, vk::ImageUsageFlags usage, FrameBufferAttachment* attachment) {
        vk::ImageAspectFlags aspectMask = 0;
        vk::ImageLayout imageLayout;

        attachment->format = format;

        if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
            aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        assert(aspectMask > 0);

        vk::ImageCreateInfo image;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent.width = width;
        image.extent.height = height;
        image.extent.depth = 1;
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = usage | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;  // VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT flag is required for input attachments;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vk::MemoryAllocateInfo memAlloc;
        vk::MemoryRequirements memReqs;

        VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));
        vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->mem, 0));

        vk::ImageViewCreateInfo imageView;
        imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageView.format = format;
        imageView.subresourceRange = {};
        imageView.subresourceRange.aspectMask = aspectMask;
        imageView.subresourceRange.baseMipLevel = 0;
        imageView.subresourceRange.levelCount = 1;
        imageView.subresourceRange.baseArrayLayer = 0;
        imageView.subresourceRange.layerCount = 1;
        imageView.image = attachment->image;
        VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
    }

    // Create color attachments for the G-Buffer components
    void createGBufferAttachments() {
        createAttachment(vk::Format::eR16G16B16A16sFloat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments.position);  // (World space) Positions
        createAttachment(vk::Format::eR16G16B16A16sFloat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments.normal);    // (World space) Normals
        createAttachment(vk::Format::eR8G8B8A8Unorm, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments.albedo);         // Albedo (color)
    }

    // Override framebuffer setup from base class
    // Deferred components will be used as frame buffer attachments
    void setupFrameBuffer() {
        vk::ImageView attachments[5];

        vk::FramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.pNext = NULL;
        frameBufferCreateInfo.renderPass = renderPass;
        frameBufferCreateInfo.attachmentCount = 5;
        frameBufferCreateInfo.pAttachments = attachments;
        frameBufferCreateInfo.width = width;
        frameBufferCreateInfo.height = height;
        frameBufferCreateInfo.layers = 1;

        // Create frame buffers for every swap chain image
        frameBuffers.resize(swapChain.imageCount);
        for (uint32_t i = 0; i < frameBuffers.size(); i++) {
            attachments[0] = swapChain.buffers[i].view;
            attachments[1] = this->attachments.position.view;
            attachments[2] = this->attachments.normal.view;
            attachments[3] = this->attachments.albedo.view;
            attachments[4] = depthStencil.view;
            VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
        }
    }

    // Override render pass setup from base class
    void setupRenderPass() {
        createGBufferAttachments();

        std::array<vk::AttachmentDescription, 5> attachments{};
        // Color attachment
        attachments[0].format = swapChain.colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // Deferred attachments
        // Position
        attachments[1].format = this->attachments.position.format;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Normals
        attachments[2].format = this->attachments.normal.format;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Albedo
        attachments[3].format = this->attachments.albedo.format;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Depth attachment
        attachments[4].format = depthFormat;
        attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // Three subpasses
        std::array<vk::SubpassDescription, 3> subpassDescriptions{};

        // First subpass: Fill G-Buffer components
        // ----------------------------------------------------------------------------------------

        vk::AttachmentReference colorReferences[4];
        colorReferences[0] = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        colorReferences[1] = { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        colorReferences[2] = { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        colorReferences[3] = { 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        vk::AttachmentReference depthReference = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        subpassDescriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescriptions[0].colorAttachmentCount = 4;
        subpassDescriptions[0].pColorAttachments = colorReferences;
        subpassDescriptions[0].pDepthStencilAttachment = &depthReference;

        // Second subpass: Final composition (using G-Buffer components)
        // ----------------------------------------------------------------------------------------

        vk::AttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        vk::AttachmentReference inputReferences[3];
        inputReferences[0] = { 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        inputReferences[1] = { 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        inputReferences[2] = { 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        uint32_t preserveAttachmentIndex = 1;

        subpassDescriptions[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescriptions[1].colorAttachmentCount = 1;
        subpassDescriptions[1].pColorAttachments = &colorReference;
        subpassDescriptions[1].pDepthStencilAttachment = &depthReference;
        // Use the color attachments filled in the first pass as input attachments
        subpassDescriptions[1].inputAttachmentCount = 3;
        subpassDescriptions[1].pInputAttachments = inputReferences;

        // Third subpass: Forward transparency
        // ----------------------------------------------------------------------------------------
        colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        inputReferences[0] = { 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        subpassDescriptions[2].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescriptions[2].colorAttachmentCount = 1;
        subpassDescriptions[2].pColorAttachments = &colorReference;
        subpassDescriptions[2].pDepthStencilAttachment = &depthReference;
        // Use the color/depth attachments filled in the first pass as input attachments
        subpassDescriptions[2].inputAttachmentCount = 1;
        subpassDescriptions[2].pInputAttachments = inputReferences;

        // Subpass dependencies for layout transitions
        std::array<vk::SubpassDependency, 4> dependencies;

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // This dependency transitions the input attachment from color attachment to shader read
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = 1;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[2].srcSubpass = 1;
        dependencies[2].dstSubpass = 2;
        dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[3].srcSubpass = 0;
        dependencies[3].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[3].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[3].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[3].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        vk::RenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
        renderPassInfo.pSubpasses = subpassDescriptions.data();
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
    }

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[5];
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[3].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[4].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 5;
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

            // First sub pass
            // Renders the components of the scene to the G-Buffer atttachments
            {
                vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 0: Deferred G-Buffer creation", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreen, 0, 1, &descriptorSets.scene, 0, NULL);
                vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.scene.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.scene.indexCount, 1, 0, 0, 0);

                vks::debugmarker::endRegion(drawCmdBuffers[i]);
            }

            // Second sub pass
            // This subpass will use the G-Buffer components that have been filled in the first subpass as input attachment for the final compositing
            {
                vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 1: Deferred composition", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

                vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition);
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.composition, 0, 1, &descriptorSets.composition, 0,
                                        NULL);
                vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

                vks::debugmarker::endRegion(drawCmdBuffers[i]);
            }

            // Third subpass
            // Render transparent geometry using a forward pass that compares against depth generted during G-Buffer fill
            {
                vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 2: Forward transparency", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

                vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.transparent);
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.transparent, 0, 1, &descriptorSets.transparent, 0,
                                        NULL);
                vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.transparent.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.transparent.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.transparent.indexCount, 1, 0, 0, 0);

                vks::debugmarker::endRegion(drawCmdBuffers[i]);
            }

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        models.scene.loadFromFile(context, getAssetPath() + "models/samplebuilding.dae", vertexLayout, 1.0f);
        models.transparent.loadFromFile(context, getAssetPath() + "models/samplebuilding_glass.dae", vertexLayout, 1.0f);
        // Textures
        if (vulkanDevice->features.textureCompressionBC) {
            textures.glass.loadFromFile(context, getAssetPath() + "textures/colored_glass_bc3_unorm.ktx", vk::Format::eBC3_UNORM_BLOCK);
        } else if (vulkanDevice->features.textureCompressionASTC_LDR) {
            textures.glass.loadFromFile(context, getAssetPath() + "textures/colored_glass_astc_8x8_unorm.ktx", vk::Format::eASTC_8x8_UNORM_BLOCK);
        } else if (vulkanDevice->features.textureCompressionETC2) {
            textures.glass.loadFromFile(context, getAssetPath() + "textures/colored_glass_etc2_unorm.ktx", vk::Format::eETC2_R8G8B8A8_UNORM_BLOCK);
        } else {
            vks::tools::exitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
        }
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions = {
            vk::vertexInputBindingDescription{VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex},
        };

        // Attribute descriptions
        vertices.attributeDescriptions = {
            // Location 0: Position
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32sFloat, 0},
            // Location 1: Color
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3},
            // Location 2: Normal
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 6},
            // Location 3: UV
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32sFloat, sizeof(float) * 9},
        };

        vertices.inputState;
        vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 4 },
            { vk::DescriptorType::eCombinedImageSampler, 4 },
            { vk::DescriptorType::eINPUT_ATTACHMENT, 4 },
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vk::descriptorPoolCreateInfo{static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 4};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        // Deferred shading layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Vertex shader uniform buffer
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0}
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size())};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.scene));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&descriptorSetLayouts.scene, 1};

        // Offscreen (scene) rendering pipeline layout
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.offscreen));
    }

    void setupDescriptorSet() {
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets;

        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &descriptorSetLayouts.scene, 1};

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.scene));
        writeDescriptorSets = {
            // Binding 0: Vertex shader uniform buffer
            vk::writeDescriptorSet{descriptorSets.scene, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.GBuffer.descriptor}
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vk::pipelineInputAssemblyStateCreateInfo{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE};

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vk::pipelineRasterizationStateCreateInfo{VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0};

        vk::PipelineColorBlendAttachmentState blendAttachmentState = vk::pipelineColorBlendAttachmentState{0xf, VK_FALSE};

        vk::PipelineColorBlendStateCreateInfo colorBlendState = vk::pipelineColorBlendStateCreateInfo{1, &blendAttachmentState};

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vk::pipelineDepthStencilStateCreateInfo{VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL};

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0};

        // Final fullscreen pass pipeline
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::pipelineCreateInfo{pipelineLayouts.offscreen, renderPass, 0};

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
        pipelineCreateInfo.subpass = 0;

        std::array<vk::PipelineColorBlendAttachmentState, 4> blendAttachmentStates = {
            { 0xf, VK_FALSE }, { 0xf, VK_FALSE }, { 0xf, VK_FALSE }, { 0xf, VK_FALSE }
        };

        colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
        colorBlendState.pAttachments = blendAttachmentStates.data();

        // Offscreen scene rendering pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/subpasses/gbuffer.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/subpasses/gbuffer.frag.spv", vk::ShaderStageFlagBits::eFragment);

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.offscreen));
    }

    // Create the Vulkan objects used in the composition pass (descriptor sets, pipelines, etc.)
    void prepareCompositionPass() {
        // Descriptor set layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0: Position input attachment
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eINPUT_ATTACHMENT, vk::ShaderStageFlagBits::eFragment, 0},
            // Binding 1: Normal input attachment
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eINPUT_ATTACHMENT, vk::ShaderStageFlagBits::eFragment, 1},
            // Binding 2: Albedo input attachment
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eINPUT_ATTACHMENT, vk::ShaderStageFlagBits::eFragment, 2},
            // Binding 3: Light positions
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 3},
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size())};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.composition));

        // Pipeline layout
        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = { &descriptorSetLayouts.composition, 1 };

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.composition));

        // Descriptor sets
        vk::DescriptorSetAllocateInfo allocInfo = { descriptorPool, &descriptorSetLayouts.composition, 1 };

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.composition));

        // Image descriptors for the offscreen color attachments
        vk::DescriptorImageInfo texDescriptorPosition =
            vk::descriptorImageInfo{VK_NULL_HANDLE, attachments.position.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        vk::DescriptorImageInfo texDescriptorNormal =
            vk::descriptorImageInfo{VK_NULL_HANDLE, attachments.normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        vk::DescriptorImageInfo texDescriptorAlbedo =
            vk::descriptorImageInfo{VK_NULL_HANDLE, attachments.albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            // Binding 0: Position texture target
            vk::writeDescriptorSet{descriptorSets.composition, vk::DescriptorType::eINPUT_ATTACHMENT, 0, &texDescriptorPosition},
            // Binding 1: Normals texture target
            vk::writeDescriptorSet{descriptorSets.composition, vk::DescriptorType::eINPUT_ATTACHMENT, 1, &texDescriptorNormal},
            // Binding 2: Albedo texture target
            vk::writeDescriptorSet{descriptorSets.composition, vk::DescriptorType::eINPUT_ATTACHMENT, 2, &texDescriptorAlbedo},
            // Binding 4: Fragment shader lights
            vk::writeDescriptorSet{descriptorSets.composition, vk::DescriptorType::eUniformBuffer, 3, &uniformBuffers.lights.descriptor},
        };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

        // Pipeline
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };

        vk::PipelineRasterizationStateCreateInfo rasterizationState = { VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, 0 };

        vk::PipelineColorBlendAttachmentState blendAttachmentState = { 0xf, VK_FALSE };

        vk::PipelineColorBlendStateCreateInfo colorBlendState = { 1, &blendAttachmentState };

        vk::PipelineDepthStencilStateCreateInfo depthStencilState = { VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = { VK_SAMPLE_COUNT_1_BIT, 0 };

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState = { dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0 };

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/subpasses/composition.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/subpasses/composition.frag.spv", vk::ShaderStageFlagBits::eFragment);

        // Use specialization constants to pass number of lights to the shader
        vk::SpecializationMapEntry specializationEntry{};
        specializationEntry.constantID = 0;
        specializationEntry.offset = 0;
        specializationEntry.size = sizeof(uint32_t);

        uint32_t specializationData = NUM_LIGHTS;

        vk::SpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 1;
        specializationInfo.pMapEntries = &specializationEntry;
        specializationInfo.dataSize = sizeof(specializationData);
        specializationInfo.pData = &specializationData;

        shaderStages[1].pSpecializationInfo = &specializationInfo;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = { pipelineLayouts.composition, renderPass, 0 };

        vk::PipelineVertexInputStateCreateInfo emptyInputState{};
        emptyInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        pipelineCreateInfo.pVertexInputState = &emptyInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();
        // Index of the subpass that this pipeline will be used in
        pipelineCreateInfo.subpass = 1;

        depthStencilState.depthWriteEnable = VK_FALSE;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.composition));

        // Transparent (forward) pipeline

        // Descriptor set layout
        setLayoutBindings = {
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 },
            { vk::DescriptorType::eINPUT_ATTACHMENT, vk::ShaderStageFlagBits::eFragment, 1 },
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2 },
        };

        descriptorLayout{ setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()) };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.transparent));

        // Pipeline layout
        pPipelineLayoutCreateInfo{ &descriptorSetLayouts.transparent, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.transparent));

        // Descriptor sets
        allocInfo{ descriptorPool, &descriptorSetLayouts.transparent, 1 };
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.transparent));

        writeDescriptorSets = {
            { descriptorSets.transparent, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.GBuffer.descriptor },
            { descriptorSets.transparent, vk::DescriptorType::eINPUT_ATTACHMENT, 1, &texDescriptorPosition },
            { descriptorSets.transparent, vk::DescriptorType::eCombinedImageSampler, 2, &textures.glass.descriptor },
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

        // Enable blending
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        pipelineCreateInfo.pVertexInputState = &vertices.inputState;
        pipelineCreateInfo.layout = pipelineLayouts.transparent;
        pipelineCreateInfo.subpass = 2;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/subpasses/transparent.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/subpasses/transparent.frag.spv", vk::ShaderStageFlagBits::eFragment);

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.transparent));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Deferred vertex shader
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &uniformBuffers.GBuffer, sizeof(uboGBuffer));

        // Deferred fragment shader
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &uniformBuffers.lights, sizeof(uboLights));

        // Update
        updateUniformBufferDeferredMatrices();
        updateUniformBufferDeferredLights();
    }

    void updateUniformBufferDeferredMatrices() {
        uboGBuffer.projection = camera.matrices.perspective;
        uboGBuffer.view = camera.matrices.view;
        uboGBuffer.model = glm::mat4(1.0f);

        VK_CHECK_RESULT(uniformBuffers.GBuffer.map());
        memcpy(uniformBuffers.GBuffer.mapped, &uboGBuffer, sizeof(uboGBuffer));
        uniformBuffers.GBuffer.unmap();
    }

    void initLights() {
        std::vector<glm::vec3> colors = {
            glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 0.0f),
        };

        std::default_random_engine rndGen(benchmark.active ? 0 : (unsigned)time(nullptr));
        std::uniform_real_distribution<float> rndDist(-1.0f, 1.0f);
        std::uniform_int_distribution<uint32_t> rndCol(0, static_cast<uint32_t>(colors.size() - 1));

        for (auto& light : uboLights.lights) {
            light.position = glm::vec4(rndDist(rndGen) * 6.0f, 0.25f + std::abs(rndDist(rndGen)) * 4.0f, rndDist(rndGen) * 6.0f, 1.0f);
            light.color = colors[rndCol(rndGen)];
            light.radius = 1.0f + std::abs(rndDist(rndGen));
        }
    }

    // Update fragment shader light position uniform block
    void updateUniformBufferDeferredLights() {
        // Current view position
        uboLights.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

        VK_CHECK_RESULT(uniformBuffers.lights.map());
        memcpy(uniformBuffers.lights.mapped, &uboLights, sizeof(uboLights));
        uniformBuffers.lights.unmap();
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

    void prepare() {
        VulkanExampleBase::prepare();

        setupVertexDescriptions();
        initLights();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        prepareCompositionPass();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
    }

    virtual void viewChanged() {
        updateUniformBufferDeferredMatrices();
        updateUniformBufferDeferredLights();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Subpasses")) {
            overlay->text("0: Deferred G-Buffer creation");
            overlay->text("1: Deferred composition");
            overlay->text("2: Forward transparency");
        }
        if (overlay->header("Settings")) {
            if (overlay->button("Randomize lights")) {
                initLights();
                updateUniformBufferDeferredLights();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
