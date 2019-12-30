/*
* Vulkan Example - Conservative rasterization
*
* Note: Requires a device that supports the VK_EXT_conservative_rasterization extension
*
* Uses an offscreen buffer with lower resolution to demonstrate the effect of conservative rasterization
*
* Copyright by Sascha Willems - www.saschawillems.de
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

#define ENABLE_VALIDATION false

#define FB_COLOR_FORMAT vk::Format::eR8G8B8A8Unorm
#define ZOOM_FACTOR 16

class VulkanExample : public VulkanExampleBase {
public:
    // Fetch and store conservative rasterization state props for display purposes
    vk::PhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterProps{};

    bool conservativeRasterEnabled = true;

    struct Vertex {
        float position[3];
        float color[3];
    };

    struct Triangle {
        vks::Buffer vertices;
        vks::Buffer indices;
        uint32_t indexCount;
    } triangle;

    vks::Buffer uniformBuffer;

    struct UniformBuffers {
        vks::Buffer scene;
    } uniformBuffers;

    struct UboScene {
        glm::mat4 projection;
        glm::mat4 model;
    } uboScene;

    struct PipelineLayouts {
        vk::PipelineLayout scene;
        vk::PipelineLayout fullscreen;
    } pipelineLayouts;

    struct Pipelines {
        vk::Pipeline triangle;
        vk::Pipeline triangleConservativeRaster;
        vk::Pipeline triangleOverlay;
        vk::Pipeline fullscreen;
    } pipelines;

    struct DescriptorSetLayouts {
        vk::DescriptorSetLayout scene;
        vk::DescriptorSetLayout fullscreen;
    } descriptorSetLayouts;

    struct DescriptorSets {
        vk::DescriptorSet scene;
        vk::DescriptorSet fullscreen;
    } descriptorSets;

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

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Conservative rasterization";
        settings.overlay = true;

        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
        camera.setRotation(glm::vec3(0.0f));
        camera.setTranslation(glm::vec3(0.0f, 0.0f, -2.0f));

        // Enable extension required for conservative rasterization
        enabledDeviceExtensions.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);

        // Reading device properties of conservative rasterization requires VK_KHR_get_physical_device_properties2 to be enabled
        enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    ~VulkanExample() {
        // Frame buffer

        vkDestroyImageView(device, offscreenPass.color.view, nullptr);
        vkDestroyImage(device, offscreenPass.color.image, nullptr);
        vkFreeMemory(device, offscreenPass.color.mem, nullptr);
        vkDestroyImageView(device, offscreenPass.depth.view, nullptr);
        vkDestroyImage(device, offscreenPass.depth.image, nullptr);
        vkFreeMemory(device, offscreenPass.depth.mem, nullptr);

        vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);
        vkDestroySampler(device, offscreenPass.sampler, nullptr);
        vkDestroyFramebuffer(device, offscreenPass.frameBuffer, nullptr);

        vkDestroyPipeline(device, pipelines.triangle, nullptr);
        vkDestroyPipeline(device, pipelines.triangleOverlay, nullptr);
        vkDestroyPipeline(device, pipelines.triangleConservativeRaster, nullptr);
        vkDestroyPipeline(device, pipelines.fullscreen, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayouts.fullscreen, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayouts.scene, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.fullscreen, nullptr);

        uniformBuffers.scene.destroy();
        triangle.vertices.destroy();
        triangle.indices.destroy();
    }

    void getEnabledFeatures() {
        enabledFeatures.fillModeNonSolid = deviceFeatures.fillModeNonSolid;
        enabledFeatures.wideLines = deviceFeatures.wideLines;
    }

    /* 
		Setup offscreen framebuffer, attachments and render passes for lower resolution rendering of the scene
	*/
    void prepareOffscreen() {
        offscreenPass.width = width / ZOOM_FACTOR;
        offscreenPass.height = height / ZOOM_FACTOR;

        // Find a suitable depth format
        vk::Format fbDepthFormat;
        vk::Bool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
        assert(validDepthFormat);

        // Color attachment
        vk::ImageCreateInfo image;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = FB_COLOR_FORMAT;
        image.extent.width = offscreenPass.width;
        image.extent.height = offscreenPass.height;
        image.extent.depth = 1;
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        // We will sample directly from the color attachment
        image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        vk::MemoryAllocateInfo memAlloc;
        vk::MemoryRequirements memReqs;

        VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &offscreenPass.color.image));
        vkGetImageMemoryRequirements(device, offscreenPass.color.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &offscreenPass.color.mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, offscreenPass.color.image, offscreenPass.color.mem, 0));

        vk::ImageViewCreateInfo colorImageView;
        colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        colorImageView.format = FB_COLOR_FORMAT;
        colorImageView.subresourceRange = {};
        colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorImageView.subresourceRange.baseMipLevel = 0;
        colorImageView.subresourceRange.levelCount = 1;
        colorImageView.subresourceRange.baseArrayLayer = 0;
        colorImageView.subresourceRange.layerCount = 1;
        colorImageView.image = offscreenPass.color.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &offscreenPass.color.view));

        // Create sampler to sample from the attachment in the fragment shader
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = samplerInfo.addressModeU;
        samplerInfo.addressModeW = samplerInfo.addressModeU;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &offscreenPass.sampler));

        // Depth stencil attachment
        image.format = fbDepthFormat;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &offscreenPass.depth.image));
        vkGetImageMemoryRequirements(device, offscreenPass.depth.image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &offscreenPass.depth.mem));
        VK_CHECK_RESULT(vkBindImageMemory(device, offscreenPass.depth.image, offscreenPass.depth.mem, 0));

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
        depthStencilView.image = offscreenPass.depth.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &offscreenPass.depth.view));

        // Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering

        std::array<vk::AttachmentDescription, 2> attchmentDescriptions = {};
        // Color attachment
        attchmentDescriptions[0].format = FB_COLOR_FORMAT;
        attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // Depth attachment
        attchmentDescriptions[1].format = fbDepthFormat;
        attchmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attchmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attchmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attchmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attchmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attchmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attchmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        vk::AttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        vk::AttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        vk::SubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorReference;
        subpassDescription.pDepthStencilAttachment = &depthReference;

        // Use subpass dependencies for layout transitions
        std::array<vk::SubpassDependency, 2> dependencies;

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        // Create the actual renderpass
        vk::RenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
        renderPassInfo.pAttachments = attchmentDescriptions.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &offscreenPass.renderPass));

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

        // Fill a descriptor for later use in a descriptor set
        offscreenPass.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        offscreenPass.descriptor.imageView = offscreenPass.color.view;
        offscreenPass.descriptor.sampler = offscreenPass.sampler;
    }

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            /*
				First render pass: Render a low res triangle to an offscreen framebuffer to use for visualization in second pass
			*/
            {
                vk::ClearValue clearValues[2];
                clearValues[0].color = { { 0.25f, 0.25f, 0.25f, 0.0f } };
                clearValues[1].depthStencil = { 1.0f, 0 };

                vk::RenderPassBeginInfo renderPassBeginInfo;
                renderPassBeginInfo.renderPass = offscreenPass.renderPass;
                renderPassBeginInfo.framebuffer = offscreenPass.frameBuffer;
                renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
                renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
                renderPassBeginInfo.clearValueCount = 2;
                renderPassBeginInfo.pClearValues = clearValues;

                vk::Viewport viewport{ (float)offscreenPass.width, (float)offscreenPass.height, 0.0f, 1.0f };
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

                vk::Rect2D scissor{ offscreenPass.width, offscreenPass.height, 0, 0 };
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets.scene, 0, nullptr);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  conservativeRasterEnabled ? pipelines.triangleConservativeRaster : pipelines.triangle);

                vk::DeviceSize offsets[1] = { 0 };
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &triangle.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], triangle.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
                vkCmdDrawIndexed(drawCmdBuffers[i], triangle.indexCount, 1, 0, 0, 0);

                vkCmdEndRenderPass(drawCmdBuffers[i]);
            }

            /*
				Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
			*/

            /*
				Second render pass: Render scene with conservative rasterization
			*/
            {
                vk::ClearValue clearValues[2];
                clearValues[0].color = { { 0.25f, 0.25f, 0.25f, 0.25f } };
                clearValues[1].depthStencil = { 1.0f, 0 };

                vk::RenderPassBeginInfo renderPassBeginInfo;
                renderPassBeginInfo.framebuffer = frameBuffers[i];
                renderPassBeginInfo.renderPass = renderPass;
                renderPassBeginInfo.renderArea.offset.x = 0;
                renderPassBeginInfo.renderArea.offset.y = 0;
                renderPassBeginInfo.renderArea.extent.width = width;
                renderPassBeginInfo.renderArea.extent.height = height;
                renderPassBeginInfo.clearValueCount = 2;
                renderPassBeginInfo.pClearValues = clearValues;

                vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
                vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
                vk::Rect2D scissor{ width, height, 0, 0 };
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                // Low-res triangle from offscreen framebuffer
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fullscreen);
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.fullscreen, 0, 1, &descriptorSets.fullscreen, 0,
                                        nullptr);
                vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

                // Overlay actual triangle
                vk::DeviceSize offsets[1] = { 0 };
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &triangle.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], triangle.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangleOverlay);
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets.scene, 0, nullptr);
                vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

                drawUI(drawCmdBuffers[i]);

                vkCmdEndRenderPass(drawCmdBuffers[i]);
            }

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        // Create a single triangle
        struct Vertex {
            float position[3];
            float color[3];
        };

        std::vector<Vertex> vertexBuffer = { { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
                                             { { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                                             { { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
        uint32_t vertexBufferSize = static_cast<uint32_t>(vertexBuffer.size()) * sizeof(Vertex);
        std::vector<uint32_t> indexBuffer = { 0, 1, 2 };
        triangle.indexCount = static_cast<uint32_t>(indexBuffer.size());
        uint32_t indexBufferSize = triangle.indexCount * sizeof(uint32_t);

        struct StagingBuffers {
            vks::Buffer vertices;
            vks::Buffer indices;
        } stagingBuffers;

        // Host visible source buffers (staging)
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                   &stagingBuffers.vertices, vertexBufferSize, vertexBuffer.data()));
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                   &stagingBuffers.indices, indexBufferSize, indexBuffer.data()));

        // Device local destination buffers
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                   &triangle.vertices, vertexBufferSize));
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                   &triangle.indices, indexBufferSize));

        // Copy from host do device
        vulkanDevice->copyBuffer(&stagingBuffers.vertices, &triangle.vertices, queue);
        vulkanDevice->copyBuffer(&stagingBuffers.indices, &triangle.indices, queue);

        // Clean up
        stagingBuffers.vertices.destroy();
        stagingBuffers.indices.destroy();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 3 }, { vk::DescriptorType::eCombinedImageSampler, 2 } };
        vk::DescriptorPoolCreateInfo descriptorPoolInfo = { poolSizes, 2 };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings;
        vk::DescriptorSetLayoutCreateInfo descriptorLayout;
        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;

        // Scene rendering
        setLayoutBindings = {
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 },           // Binding 0: Vertex shader uniform buffer
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1 },  // Binding 1: Fragment shader image sampler
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 2 }          // Binding 2: Fragment shader uniform buffer
        };
        descriptorLayout{ setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()) };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.scene));
        pPipelineLayoutCreateInfo{ &descriptorSetLayouts.scene, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.scene));

        // Fullscreen pass
        setLayoutBindings = {
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 },          // Binding 0: Vertex shader uniform buffer
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1 }  // Binding 1: Fragment shader image sampler
        };
        descriptorLayout{ setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()) };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.fullscreen));
        pPipelineLayoutCreateInfo{ &descriptorSetLayouts.fullscreen, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.fullscreen));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo descriptorSetAllocInfo;

        // Scene rendering
        descriptorSetAllocInfo{ descriptorPool, &descriptorSetLayouts.scene, 1 };
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.scene));
        std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets = {
            { descriptorSets.scene, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.scene.descriptor },
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(offScreenWriteDescriptorSets.size()), offScreenWriteDescriptorSets.data(), 0, nullptr);

        // Fullscreen pass
        descriptorSetAllocInfo{ descriptorPool, &descriptorSetLayouts.fullscreen, 1 };
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.fullscreen));
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            { descriptorSets.fullscreen, vk::DescriptorType::eCombinedImageSampler, 1, &offscreenPass.descriptor },
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };

        vk::PipelineColorBlendAttachmentState blendAttachmentState = { 0xf, VK_FALSE };

        vk::PipelineColorBlendStateCreateInfo colorBlendStateCI = { 1, &blendAttachmentState };

        vk::PipelineDepthStencilStateCreateInfo depthStencilStateCI = { VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL };

        vk::PipelineViewportStateCreateInfo viewportStateCI = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleStateCI = { VK_SAMPLE_COUNT_1_BIT, 0 };

        std::vector<vk::DynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        vk::PipelineDynamicStateCreateInfo dynamicStateCI = { dynamicStateEnables };

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = { pipelineLayouts.fullscreen, renderPass, 0 };

        vk::PipelineRasterizationStateCreateInfo rasterizationStateCI = { VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0 };

        /*
			Conservative rasterization setup
		*/

        /*
			Get device properties for conservative rasterization
			Requires VK_KHR_get_physical_device_properties2 and manual function pointer creation
		*/
        PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR =
            reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
        assert(vkGetPhysicalDeviceProperties2KHR);
        vk::PhysicalDeviceProperties2KHR deviceProps2{};
        conservativeRasterProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT;
        deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
        deviceProps2.pNext = &conservativeRasterProps;
        vkGetPhysicalDeviceProperties2KHR(physicalDevice, &deviceProps2);

        // Vertex bindings and attributes
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, sizeof(Vertex), vk::VertexInputRate::eVertex },
        };
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { 0, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 1: Color
        };
        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCreateInfo.pRasterizationState = &rasterizationStateCI;
        pipelineCreateInfo.pColorBlendState = &colorBlendStateCI;
        pipelineCreateInfo.pMultisampleState = &multisampleStateCI;
        pipelineCreateInfo.pViewportState = &viewportStateCI;
        pipelineCreateInfo.pDepthStencilState = &depthStencilStateCI;
        pipelineCreateInfo.pDynamicState = &dynamicStateCI;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        // Full screen pass
        shaderStages[0] = loadShader(getAssetPath() + "shaders/conservativeraster/fullscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/conservativeraster/fullscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);
        // Empty vertex input state (full screen triangle generated in vertex shader)
        vk::PipelineVertexInputStateCreateInfo emptyInputState;
        pipelineCreateInfo.pVertexInputState = &emptyInputState;
        pipelineCreateInfo.layout = pipelineLayouts.fullscreen;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.fullscreen));

        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.layout = pipelineLayouts.scene;

        // Original triangle outline
        // TODO: Check support for lines
        rasterizationStateCI.lineWidth = 2.0f;
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_LINE;
        shaderStages[0] = loadShader(getAssetPath() + "shaders/conservativeraster/triangle.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/conservativeraster/triangleoverlay.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.triangleOverlay));

        pipelineCreateInfo.renderPass = offscreenPass.renderPass;

        /*
			Triangle rendering
		*/
        rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
        shaderStages[0] = loadShader(getAssetPath() + "shaders/conservativeraster/triangle.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/conservativeraster/triangle.frag.spv", vk::ShaderStageFlagBits::eFragment);

        /*
			Basic pipeline
		*/
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.triangle));

        /*
			Pipeline with conservative rasterization enabled
		*/
        vk::PipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterStateCI{};
        conservativeRasterStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
        conservativeRasterStateCI.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
        conservativeRasterStateCI.extraPrimitiveOverestimationSize = conservativeRasterProps.maxExtraPrimitiveOverestimationSize;

        // Conservative rasterization state has to be chained into the pipeline rasterization state create info structure
        rasterizationStateCI.pNext = &conservativeRasterStateCI;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.triangleConservativeRaster));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.scene,
                                                   sizeof(uboScene)));
        VK_CHECK_RESULT(uniformBuffers.scene.map());
        updateUniformBuffersScene();
    }

    void updateUniformBuffersScene() {
        uboScene.projection = camera.matrices.perspective;
        uboScene.model = camera.matrices.view;
        memcpy(uniformBuffers.scene.mapped, &uboScene, sizeof(uboScene));
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

        prepareOffscreen();
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
        if (camera.updated)
            updateUniformBuffersScene();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Settings")) {
            if (overlay->checkBox("Conservative rasterization", &conservativeRasterEnabled)) {
                buildCommandBuffers();
            }
        }
        if (overlay->header("Device properties")) {
            overlay->text("maxExtraPrimitiveOverestimationSize: %f", conservativeRasterProps.maxExtraPrimitiveOverestimationSize);
            overlay->text("extraPrimitiveOverestimationSizeGranularity: %f", conservativeRasterProps.extraPrimitiveOverestimationSizeGranularity);
            overlay->text("primitiveUnderestimation:  %s", conservativeRasterProps.primitiveUnderestimation ? "yes" : "no");
            overlay->text("conservativePointAndLineRasterization:  %s", conservativeRasterProps.conservativePointAndLineRasterization ? "yes" : "no");
            overlay->text("degenerateTrianglesRasterized: %s", conservativeRasterProps.degenerateTrianglesRasterized ? "yes" : "no");
            overlay->text("degenerateLinesRasterized: %s", conservativeRasterProps.degenerateLinesRasterized ? "yes" : "no");
            overlay->text("fullyCoveredFragmentShaderInputVariable: %s", conservativeRasterProps.fullyCoveredFragmentShaderInputVariable ? "yes" : "no");
            overlay->text("conservativeRasterizationPostDepthCoverage: %s", conservativeRasterProps.conservativeRasterizationPostDepthCoverage ? "yes" : "no");
        }
    }
};

VULKAN_EXAMPLE_MAIN()
