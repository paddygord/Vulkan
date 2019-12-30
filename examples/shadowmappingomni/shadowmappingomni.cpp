/*
* Vulkan Example - Omni directional shadows using a dynamic cube map
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

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define ENABLE_VALIDATION false

// Texture properties
#define TEX_DIM 1024
#define TEX_FILTER VK_FILTER_LINEAR

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM
#define FB_COLOR_FORMAT vk::Format::eR32sFloat

class VulkanExample : public VulkanExampleBase {
public:
    bool displayCubeMap = false;

    float zNear = 0.1f;
    float zFar = 1024.0f;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
    } };

    struct {
        vkx::model::Model skybox;
        vkx::model::Model scene;
    } models;

    struct {
        vks::Buffer scene;
        vks::Buffer offscreen;
    } uniformBuffers;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVSquad;

    glm::vec4 lightPos = glm::vec4(0.0f, -25.0f, 0.0f, 1.0);

    struct UBO {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model;
        glm::vec4 lightPos;
    };

    UBO uboVSscene, uboOffscreenVS;

    struct {
        vk::Pipeline scene;
        vk::Pipeline offscreen;
        vk::Pipeline cubemapDisplay;
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

    vks::Texture shadowCubeMap;

    // Framebuffer for offscreen rendering
    struct FrameBufferAttachment {
        vk::Image image;
        vk::DeviceMemory mem;
        vk::ImageView view;
    };
    struct OffscreenPass {
        int32_t width, height;
        vk::Framebuffer frameBuffer;
        FrameBufferAttachment color, depth;
        vk::RenderPass renderPass;
        vk::Sampler sampler;
        vk::DescriptorImageInfo descriptor;
    } offscreenPass;

    vk::Format fbDepthFormat;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Point light shadows (cubemap)";
        settings.overlay = true;
        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(45.0f, (float)width / (float)height, zNear, zFar);
        camera.setRotation(glm::vec3(-20.5f, -673.0f, 0.0f));
        camera.setPosition(glm::vec3(0.0f, 0.0f, -175.0f));
        zoomSpeed = 10.0f;
        timerSpeed *= 0.25f;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class

        // Cube map
        vkDestroyImageView(device, shadowCubeMap.view, nullptr);
        vkDestroyImage(device, shadowCubeMap.image, nullptr);
        vkDestroySampler(device, shadowCubeMap.sampler, nullptr);
        vkFreeMemory(device, shadowCubeMap.deviceMemory, nullptr);

        // Frame buffer

        // Color attachment
        vkDestroyImageView(device, offscreenPass.color.view, nullptr);
        vkDestroyImage(device, offscreenPass.color.image, nullptr);
        vkFreeMemory(device, offscreenPass.color.mem, nullptr);

        // Depth attachment
        vkDestroyImageView(device, offscreenPass.depth.view, nullptr);
        vkDestroyImage(device, offscreenPass.depth.image, nullptr);
        vkFreeMemory(device, offscreenPass.depth.mem, nullptr);

        vkDestroyFramebuffer(device, offscreenPass.frameBuffer, nullptr);

        vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);

        // Pipelibes
        vkDestroyPipeline(device, pipelines.scene, nullptr);
        vkDestroyPipeline(device, pipelines.offscreen, nullptr);
        vkDestroyPipeline(device, pipelines.cubemapDisplay, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayouts.scene, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayouts.offscreen, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        // Meshes
        models.scene.destroy();
        models.skybox.destroy();

        // Uniform buffers
        uniformBuffers.offscreen.destroy();
        uniformBuffers.scene.destroy();
    }

    void prepareCubeMap() {
        shadowCubeMap.width = TEX_DIM;
        shadowCubeMap.height = TEX_DIM;

        // 32 bit float format for higher precision
        vk::Format format = vk::Format::eR32sFloat;

        // Cube map image description
        vk::ImageCreateInfo imageCreateInfo;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = { shadowCubeMap.width, shadowCubeMap.height, 1 };
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 6;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        vk::MemoryAllocateInfo memAllocInfo;
        vk::MemoryRequirements memReqs;

        vk::CommandBuffer layoutCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        // Create cube map image
        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &shadowCubeMap.image));

        vkGetImageMemoryRequirements(device, shadowCubeMap.image, &memReqs);

        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &shadowCubeMap.deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, shadowCubeMap.image, shadowCubeMap.deviceMemory, 0));

        // Image barrier for optimal image (target)
        vk::ImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 6;
        vks::tools::setImageLayout(layoutCmd, shadowCubeMap.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);

        VulkanExampleBase::flushCommandBuffer(layoutCmd, queue, true);

        // Create sampler
        vk::SamplerCreateInfo sampler;
        sampler.magFilter = TEX_FILTER;
        sampler.minFilter = TEX_FILTER;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler.addressModeV = sampler.addressModeU;
        sampler.addressModeW = sampler.addressModeU;
        sampler.mipLodBias = 0.0f;
        sampler.maxAnisotropy = 1.0f;
        sampler.compareOp = VK_COMPARE_OP_NEVER;
        sampler.minLod = 0.0f;
        sampler.maxLod = 1.0f;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &shadowCubeMap.sampler));

        // Create image view
        vk::ImageViewCreateInfo view;
        view.image = VK_NULL_HANDLE;
        view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view.format = format;
        view.components = { VK_COMPONENT_SWIZZLE_R };
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        view.subresourceRange.layerCount = 6;
        view.image = shadowCubeMap.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &shadowCubeMap.view));
    }

    // Prepare a new framebuffer for offscreen rendering
    // The contents of this framebuffer are then
    // copied to the different cube map faces
    void prepareOffscreenFramebuffer() {
        offscreenPass.width = FB_DIM;
        offscreenPass.height = FB_DIM;

        vk::Format fbColorFormat = FB_COLOR_FORMAT;

        // Color attachment
        vk::ImageCreateInfo imageCreateInfo;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = fbColorFormat;
        imageCreateInfo.extent.width = offscreenPass.width;
        imageCreateInfo.extent.height = offscreenPass.height;
        imageCreateInfo.extent.depth = 1;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        // Image of the framebuffer is blit source
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vk::MemoryAllocateInfo memAlloc;

        vk::ImageViewCreateInfo colorImageView;
        colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorImageView.format = fbColorFormat;
        colorImageView.flags = 0;
        colorImageView.subresourceRange = {};
        colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorImageView.subresourceRange.baseMipLevel = 0;
        colorImageView.subresourceRange.levelCount = 1;
        colorImageView.subresourceRange.baseArrayLayer = 0;
        colorImageView.subresourceRange.layerCount = 1;

        vk::MemoryRequirements memReqs;

        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &offscreenPass.color.image));
        vkGetImageMemoryRequirements(device, offscreenPass.color.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &offscreenPass.color.mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, offscreenPass.color.image, offscreenPass.color.mem, 0));

        vk::CommandBuffer layoutCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        vks::tools::setImageLayout(layoutCmd, offscreenPass.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        colorImageView.image = offscreenPass.color.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &offscreenPass.color.view));

        // Depth stencil attachment
        imageCreateInfo.format = fbDepthFormat;
        imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        vk::ImageViewCreateInfo depthStencilView;
        depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthStencilView.format = fbDepthFormat;
        depthStencilView.flags = 0;
        depthStencilView.subresourceRange = {};
        depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        depthStencilView.subresourceRange.baseMipLevel = 0;
        depthStencilView.subresourceRange.levelCount = 1;
        depthStencilView.subresourceRange.baseArrayLayer = 0;
        depthStencilView.subresourceRange.layerCount = 1;

        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &offscreenPass.depth.image));
        vkGetImageMemoryRequirements(device, offscreenPass.depth.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &offscreenPass.depth.mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, offscreenPass.depth.image, offscreenPass.depth.mem, 0));

        vks::tools::setImageLayout(layoutCmd, offscreenPass.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        VulkanExampleBase::flushCommandBuffer(layoutCmd, queue, true);

        depthStencilView.image = offscreenPass.depth.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &offscreenPass.depth.view));

        vk::ImageView attachments[2];
        attachments[0] = offscreenPass.color.view;
        attachments[1] = offscreenPass.depth.view;

        vk::FramebufferCreateInfo fbufCreateInfo;
        fbufCreateInfo.renderPass = offscreenPass.renderPass;
        fbufCreateInfo.attachmentCount = 2;
        fbufCreateInfo.pAttachments = attachments;
        fbufCreateInfo.width = offscreenPass.width;
        fbufCreateInfo.height = offscreenPass.height;
        fbufCreateInfo.layers = 1;

        VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &offscreenPass.frameBuffer));
    }

    // Updates a single cube map face
    // Renders the scene with face's view and does a copy from framebuffer to cube face
    // Uses push constants for quick update of view matrix for the current cube map face
    void updateCubeFace(uint32_t faceIndex, vk::CommandBuffer commandBuffer) {
        vk::ClearValue clearValues[2];
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        // Reuse render pass from example pass
        renderPassBeginInfo.renderPass = offscreenPass.renderPass;
        renderPassBeginInfo.framebuffer = offscreenPass.frameBuffer;
        renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
        renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        // Update view matrix via push constant

        glm::mat4 viewMatrix = glm::mat4(1.0f);
        switch (faceIndex) {
            case 0:  // POSITIVE_X
                viewMatrix = glm::rotate(viewMatrix, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                break;
            case 1:  // NEGATIVE_X
                viewMatrix = glm::rotate(viewMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                break;
            case 2:  // POSITIVE_Y
                viewMatrix = glm::rotate(viewMatrix, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                break;
            case 3:  // NEGATIVE_Y
                viewMatrix = glm::rotate(viewMatrix, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                break;
            case 4:  // POSITIVE_Z
                viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                break;
            case 5:  // NEGATIVE_Z
                viewMatrix = glm::rotate(viewMatrix, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
                break;
        }

        // Render scene from cube face's point of view
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Update shader push constant block
        // Contains current face view matrix
        vkCmdPushConstants(commandBuffer, pipelineLayouts.offscreen, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &viewMatrix);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreen, 0, 1, &descriptorSets.offscreen, 0, NULL);

        vk::DeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &models.scene.vertices.buffer, offsets);
        vkCmdBindIndexBuffer(commandBuffer, models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, models.scene.indexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(commandBuffer);
        // Make sure color writes to the framebuffer are finished before using it as transfer source
        vks::tools::setImageLayout(commandBuffer, offscreenPass.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        vk::ImageSubresourceRange cubeFaceSubresourceRange = {};
        cubeFaceSubresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cubeFaceSubresourceRange.baseMipLevel = 0;
        cubeFaceSubresourceRange.levelCount = 1;
        cubeFaceSubresourceRange.baseArrayLayer = faceIndex;
        cubeFaceSubresourceRange.layerCount = 1;

        // Change image layout of one cubemap face to transfer destination
        vks::tools::setImageLayout(commandBuffer, shadowCubeMap.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   cubeFaceSubresourceRange);

        // Copy region for transfer from framebuffer to cube face
        vk::ImageCopy copyRegion = {};

        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.baseArrayLayer = 0;
        copyRegion.srcSubresource.mipLevel = 0;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.srcOffset = { 0, 0, 0 };

        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.baseArrayLayer = faceIndex;
        copyRegion.dstSubresource.mipLevel = 0;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.dstOffset = { 0, 0, 0 };

        copyRegion.extent.width = shadowCubeMap.width;
        copyRegion.extent.height = shadowCubeMap.height;
        copyRegion.extent.depth = 1;

        // Put image copy into command buffer
        vkCmdCopyImage(commandBuffer, offscreenPass.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shadowCubeMap.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Transform framebuffer color attachment back
        vks::tools::setImageLayout(commandBuffer, offscreenPass.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        // Change image layout of copied face to shader read
        vks::tools::setImageLayout(commandBuffer, shadowCubeMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   cubeFaceSubresourceRange);
    }

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            /*
				Generate shadow cube maps using one render pass per face
			*/
            {
                vk::Viewport viewport{ (float)offscreenPass.width, (float)offscreenPass.height, 0.0f, 1.0f };
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

                vk::Rect2D scissor{ offscreenPass.width, offscreenPass.height, 0, 0 };
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                for (uint32_t face = 0; face < 6; face++) {
                    updateCubeFace(face, drawCmdBuffers[i]);
                }
            }

            /*
				Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
			*/

            /*
				Scene rendering with applied shadow map
			*/
            {
                vk::ClearValue clearValues[2];
                clearValues[0].color = defaultClearColor;
                clearValues[1].depthStencil = { 1.0f, 0 };

                vk::RenderPassBeginInfo renderPassBeginInfo;
                renderPassBeginInfo.renderPass = renderPass;
                renderPassBeginInfo.framebuffer = frameBuffers[i];
                renderPassBeginInfo.renderArea.extent.width = width;
                renderPassBeginInfo.renderArea.extent.height = height;
                renderPassBeginInfo.clearValueCount = 2;
                renderPassBeginInfo.pClearValues = clearValues;

                vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

                vk::Rect2D scissor{ width, height, 0, 0 };
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                vk::DeviceSize offsets[1] = { 0 };

                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets.scene, 0, NULL);

                if (displayCubeMap) {
                    // Display all six sides of the shadow cube map
                    // Note: Visualization of the different faces is done in the fragment shader, see cubemapdisplay.frag
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.cubemapDisplay);
                    vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.skybox.vertices.buffer, offsets);
                    vkCmdBindIndexBuffer(drawCmdBuffers[i], models.skybox.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
                } else {
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scene);
                    vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.scene.vertices.buffer, offsets);
                    vkCmdBindIndexBuffer(drawCmdBuffers[i], models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(drawCmdBuffers[i], models.scene.indexCount, 1, 0, 0, 0);
                }

                drawUI(drawCmdBuffers[i]);

                vkCmdEndRenderPass(drawCmdBuffers[i]);
            }

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        models.skybox.loadFromFile(context, getAssetPath() + "models/cube.obj", vertexLayout, 2.0f);
        models.scene.loadFromFile(context, getAssetPath() + "models/shadowscene_fire.dae", vertexLayout, 2.0f);
    }

    void setupDescriptorPool() {
        // Example uses three ubos and two image samplers
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 3 }, { vk::DescriptorType::eCombinedImageSampler, 2 } };
        vk::DescriptorPoolCreateInfo descriptorPoolInfo{ poolSizes.size(), poolSizes.data(), 3 };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        // Shared pipeline layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = { // Binding 0 : Vertex shader uniform buffer
                                                                          { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 },
                                                                          // Binding 1 : Fragment shader image sampler (cube map)
                                                                          { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1 }
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout{ setLayoutBindings.data(), setLayoutBindings.size() };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        // 3D scene pipeline layout
        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo{ &descriptorSetLayout, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.scene));

        // Offscreen pipeline layout
        // Push constants for cube map face view matrices
        vk::PushConstantRange pushConstantRange{ vk::ShaderStageFlagBits::eVertex, sizeof(glm::mat4), 0 };
        // Push constant ranges are part of the pipeline layout
        pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.offscreen));
    }

    void setupDescriptorSets() {
        vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, &descriptorSetLayout, 1 };

        // 3D scene
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.scene));
        // Image descriptor for the cube map
        vk::DescriptorImageInfo texDescriptor =
            vk::descriptorImageInfo{shadowCubeMap.sampler, shadowCubeMap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::vector<vk::WriteDescriptorSet> sceneDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            { descriptorSets.scene, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.scene.descriptor },
            // Binding 1 : Fragment shader shadow sampler
            { descriptorSets.scene, vk::DescriptorType::eCombinedImageSampler, 1, &texDescriptor }
        };
        vkUpdateDescriptorSets(device, sceneDescriptorSets.size(), sceneDescriptorSets.data(), 0, NULL);

        // Offscreen
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.offscreen));
        std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            { descriptorSets.offscreen, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.offscreen.descriptor },
        };
        vkUpdateDescriptorSets(device, offScreenWriteDescriptorSets.size(), offScreenWriteDescriptorSets.data(), 0, NULL);
    }

    // Set up a separate render pass for the offscreen frame buffer
    // This is necessary as the offscreen frame buffer attachments
    // use formats different to the ones from the visible frame buffer
    // and at least the depth one may not be compatible
    void prepareOffscreenRenderpass() {
        vk::AttachmentDescription osAttachments[2] = {};

        // Find a suitable depth format
        vk::Bool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
        assert(validDepthFormat);

        osAttachments[0].format = FB_COLOR_FORMAT;
        osAttachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        osAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        osAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        osAttachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        osAttachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        osAttachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        osAttachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // Depth attachment
        osAttachments[1].format = fbDepthFormat;
        osAttachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        osAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        osAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        osAttachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        osAttachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        osAttachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        osAttachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        vk::AttachmentReference colorReference = {};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        vk::AttachmentReference depthReference = {};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        vk::SubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        vk::RenderPassCreateInfo renderPassCreateInfo;
        renderPassCreateInfo.attachmentCount = 2;
        renderPassCreateInfo.pAttachments = osAttachments;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;

        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &offscreenPass.renderPass));
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{ VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };
        vk::PipelineRasterizationStateCreateInfo rasterizationState{ VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0 };
        vk::PipelineColorBlendAttachmentState blendAttachmentState{ 0xf, VK_FALSE };
        vk::PipelineColorBlendStateCreateInfo colorBlendState{ 1, &blendAttachmentState };
        vk::PipelineDepthStencilStateCreateInfo depthStencilState{ VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };
        vk::PipelineViewportStateCreateInfo viewportState{ 1, 1, 0 };
        vk::PipelineMultisampleStateCreateInfo multisampleState{ VK_SAMPLE_COUNT_1_BIT, 0 };
        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState{ dynamicStateEnables.data(), dynamicStateEnables.size(), 0 };

        // 3D scene pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapomni/scene.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapomni/scene.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo{ pipelineLayouts.scene, renderPass, 0 };
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();

        // Vertex bindings and attributes
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Position
            { 0, 1, vk::Format::eR32G32sFloat, sizeof(float) * 3 },     // Texture coordinates
            { 0, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 5 },  // Color
            { 0, 3, vk::Format::eR32G32B32sFloat, sizeof(float) * 8 },  // Normal
        };
        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.scene));

        // Cube map display pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapomni/cubemapdisplay.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapomni/cubemapdisplay.frag.spv", vk::ShaderStageFlagBits::eFragment);
        vk::PipelineVertexInputStateCreateInfo emptyInputState;
        pipelineCreateInfo.pVertexInputState = &emptyInputState;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.cubemapDisplay));

        // Offscreen pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapomni/offscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapomni/offscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelineCreateInfo.layout = pipelineLayouts.offscreen;
        pipelineCreateInfo.renderPass = offscreenPass.renderPass;
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.offscreen));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Offscreen vertex shader uniform buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.offscreen,
                                                   sizeof(uboOffscreenVS)));

        // Scene vertex shader uniform buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.scene,
                                                   sizeof(uboVSscene)));

        // Map persistent
        VK_CHECK_RESULT(uniformBuffers.offscreen.map());
        VK_CHECK_RESULT(uniformBuffers.scene.map());

        updateUniformBufferOffscreen();
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboVSscene.projection = camera.matrices.perspective;
        uboVSscene.view = camera.matrices.view;
        uboVSscene.model = glm::mat4(1.0f);
        uboVSscene.lightPos = lightPos;
        memcpy(uniformBuffers.scene.mapped, &uboVSscene, sizeof(uboVSscene));
    }

    void updateUniformBufferOffscreen() {
        lightPos.x = sin(glm::radians(timer * 360.0f)) * 1.0f;
        lightPos.z = cos(glm::radians(timer * 360.0f)) * 1.0f;
        uboOffscreenVS.projection = glm::perspective((float)(M_PI / 2.0), 1.0f, zNear, zFar);
        uboOffscreenVS.view = glm::mat4(1.0f);
        uboOffscreenVS.model = glm::translate(glm::mat4(1.0f), glm::vec3(-lightPos.x, -lightPos.y, -lightPos.z));
        uboOffscreenVS.lightPos = lightPos;
        memcpy(uniformBuffers.offscreen.mapped, &uboOffscreenVS, sizeof(uboOffscreenVS));
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

        prepareUniformBuffers();
        prepareCubeMap();
        setupDescriptorSetLayout();
        prepareOffscreenRenderpass();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSets();
        prepareOffscreenFramebuffer();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        if (!paused || camera.updated) {
            updateUniformBufferOffscreen();
            updateUniformBuffers();
        }
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Settings")) {
            if (overlay->checkBox("Display shadow cube render target", &displayCubeMap)) {
                buildCommandBuffers();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
