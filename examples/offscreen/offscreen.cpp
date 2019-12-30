/*
* Vulkan Example - Offscreen rendering using a separate framebuffer
*
* Copyright (C) Sascha Willems - www.saschawillems.de
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
#include "VulkanModel.hpp"
#include "VulkanBuffer.hpp"

#define ENABLE_VALIDATION false

// Offscreen frame buffer properties
#define FB_DIM 512
#define FB_COLOR_FORMAT vk::Format::eR8G8B8A8Unorm

class VulkanExample : public VulkanExampleBase {
public:
    bool debugDisplay = false;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
    } };

    struct {
        vkx::model::Model example;
        vkx::model::Model quad;
        vkx::model::Model plane;
    } models;

    struct {
        vks::Buffer vsShared;
        vks::Buffer vsMirror;
        vks::Buffer vsOffScreen;
        vks::Buffer vsDebugQuad;
    } uniformBuffers;

    struct UBO {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    } uboShared;

    struct {
        vk::Pipeline debug;
        vk::Pipeline shaded;
        vk::Pipeline shadedOffscreen;
        vk::Pipeline mirror;
    } pipelines;

    struct {
        vk::PipelineLayout textured;
        vk::PipelineLayout shaded;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet offscreen;
        vk::DescriptorSet mirror;
        vk::DescriptorSet model;
        vk::DescriptorSet debugQuad;
    } descriptorSets;

    struct {
        vk::DescriptorSetLayout textured;
        vk::DescriptorSetLayout shaded;
    } descriptorSetLayouts;

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

    glm::vec3 meshPos = glm::vec3(0.0f, -1.5f, 0.0f);
    glm::vec3 meshRot = glm::vec3(0.0f);

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        zoom = -6.0f;
        rotation = { -2.5f, 0.0f, 0.0f };
        cameraPos = { 0.0f, 1.0f, 0.0f };
        timerSpeed *= 0.25f;
        title = "Offscreen rendering";
        settings.overlay = true;
        // The scene shader uses a clipping plane, so this feature has to be enabled
        enabledFeatures.shaderClipDistance = VK_TRUE;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class

        // Frame buffer

        // Color attachment
        vkDestroyImageView(device, offscreenPass.color.view, nullptr);
        vkDestroyImage(device, offscreenPass.color.image, nullptr);
        vkFreeMemory(device, offscreenPass.color.mem, nullptr);

        // Depth attachment
        vkDestroyImageView(device, offscreenPass.depth.view, nullptr);
        vkDestroyImage(device, offscreenPass.depth.image, nullptr);
        vkFreeMemory(device, offscreenPass.depth.mem, nullptr);

        vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);
        vkDestroySampler(device, offscreenPass.sampler, nullptr);
        vkDestroyFramebuffer(device, offscreenPass.frameBuffer, nullptr);

        vkDestroyPipeline(device, pipelines.debug, nullptr);
        vkDestroyPipeline(device, pipelines.shaded, nullptr);
        vkDestroyPipeline(device, pipelines.shadedOffscreen, nullptr);
        vkDestroyPipeline(device, pipelines.mirror, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayouts.textured, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayouts.shaded, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.shaded, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.textured, nullptr);

        // Models
        models.example.destroy();
        models.quad.destroy();
        models.plane.destroy();

        // Uniform buffers
        uniformBuffers.vsShared.destroy();
        uniformBuffers.vsMirror.destroy();
        uniformBuffers.vsOffScreen.destroy();
        uniformBuffers.vsDebugQuad.destroy();
    }

    // Setup the offscreen framebuffer for rendering the mirrored scene
    // The color attachment of this framebuffer will then be used to sample from in the fragment shader of the final pass
    void prepareOffscreen() {
        offscreenPass.width = FB_DIM;
        offscreenPass.height = FB_DIM;

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
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
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

        vk::ClearValue clearValues[2];
        vk::Viewport viewport;
        vk::Rect2D scissor;
        vk::DeviceSize offsets[1] = { 0 };

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            /*
				First render pass: Offscreen rendering
			*/
            {
                vk::ClearValue clearValues[2];
                clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
                clearValues[1].depthStencil = { 1.0f, 0 };

                vk::RenderPassBeginInfo renderPassBeginInfo;
                renderPassBeginInfo.renderPass = offscreenPass.renderPass;
                renderPassBeginInfo.framebuffer = offscreenPass.frameBuffer;
                renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
                renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
                renderPassBeginInfo.clearValueCount = 2;
                renderPassBeginInfo.pClearValues = clearValues;

                vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                vk::Viewport viewport{ (float)offscreenPass.width, (float)offscreenPass.height, 0.0f, 1.0f };
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

                vk::Rect2D scissor{ offscreenPass.width, offscreenPass.height, 0, 0 };
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                vk::DeviceSize offsets[1] = { 0 };

                // Mirrored scene
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.shaded, 0, 1, &descriptorSets.offscreen, 0, NULL);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.shadedOffscreen);
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.example.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.example.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.example.indexCount, 1, 0, 0, 0);

                vkCmdEndRenderPass(drawCmdBuffers[i]);
            }

            /*
				Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
			*/

            /*
				Second render pass: Scene rendering with applied radial blur
			*/
            {
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

                if (debugDisplay) {
                    vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.textured, 0, 1, &descriptorSets.debugQuad, 0,
                                            NULL);
                    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.debug);
                    vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.quad.vertices.buffer, offsets);
                    vkCmdBindIndexBuffer(drawCmdBuffers[i], models.quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(drawCmdBuffers[i], models.quad.indexCount, 1, 0, 0, 0);
                }

                // Scene

                // Reflection plane
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.textured, 0, 1, &descriptorSets.mirror, 0, NULL);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.mirror);

                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.plane.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.plane.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.plane.indexCount, 1, 0, 0, 0);

                // Model
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.shaded, 0, 1, &descriptorSets.model, 0, NULL);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.shaded);

                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.example.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.example.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.example.indexCount, 1, 0, 0, 0);

                drawUI(drawCmdBuffers[i]);

                vkCmdEndRenderPass(drawCmdBuffers[i]);
            }

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        models.plane.loadFromFile(context, getAssetPath() + "models/plane.obj", vertexLayout, 0.5f);
        models.example.loadFromFile(context, getAssetPath() + "models/chinesedragon.dae", vertexLayout, 0.3f);
    }

    void generateQuad() {
        // Setup vertices for a single uv-mapped quad
        struct Vertex {
            float pos[3];
            float uv[2];
            float col[3];
            float normal[3];
        };

#define QUAD_COLOR_NORMAL { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }
        std::vector<Vertex> vertexBuffer = { { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f }, QUAD_COLOR_NORMAL },
                                             { { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }, QUAD_COLOR_NORMAL },
                                             { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f }, QUAD_COLOR_NORMAL },
                                             { { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }, QUAD_COLOR_NORMAL } };
#undef QUAD_COLOR_NORMAL

        VK_CHECK_RESULT(
            vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       vertexBuffer.size() * sizeof(Vertex), &models.quad.vertices.buffer, &models.quad.vertices.memory, vertexBuffer.data()));

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0, 1, 2, 2, 3, 0 };
        models.quad.indexCount = indexBuffer.size();

        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                   indexBuffer.size() * sizeof(uint32_t), &models.quad.indices.buffer, &models.quad.indices.memory,
                                                   indexBuffer.data()));

        models.quad.device = device;
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 6 }, { vk::DescriptorType::eCombinedImageSampler, 8 } };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = vk::descriptorPoolCreateInfo{poolSizes.size(), poolSizes.data(), 5};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings;
        vk::DescriptorSetLayoutCreateInfo descriptorLayoutInfo;
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;

        // Binding 0 : Vertex shader uniform buffer
        setLayoutBindings.push_back(vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0)};
        // Binding 1 : Fragment shader image sampler
        setLayoutBindings.push_back(
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)};
        // Binding 2 : Fragment shader image sampler
        setLayoutBindings.push_back(
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2)};

        // Shaded layouts (only use first layout binding)
        descriptorLayoutInfo{ setLayoutBindings.data(), 1 };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorSetLayouts.shaded));

        pipelineLayoutInfo{ &descriptorSetLayouts.shaded, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayouts.shaded));

        // Textured layouts (use all layout bindings)
        descriptorLayoutInfo{ setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()) };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorSetLayouts.textured));

        pipelineLayoutInfo{ &descriptorSetLayouts.textured, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayouts.textured));
    }

    void setupDescriptorSet() {
        // Mirror plane descriptor set
        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &descriptorSetLayouts.textured, 1};

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.mirror));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::writeDescriptorSet{descriptorSets.mirror, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.vsMirror.descriptor},
            // Binding 1 : Fragment shader texture sampler
            vk::writeDescriptorSet{descriptorSets.mirror, vk::DescriptorType::eCombinedImageSampler, 1, &offscreenPass.descriptor},
        };

        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Debug quad
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.debugQuad));

        std::vector<vk::WriteDescriptorSet> debugQuadWriteDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::writeDescriptorSet{descriptorSets.debugQuad, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.vsDebugQuad.descriptor},
            // Binding 1 : Fragment shader texture sampler
            vk::writeDescriptorSet{descriptorSets.debugQuad, vk::DescriptorType::eCombinedImageSampler, 1, &offscreenPass.descriptor}
        };
        vkUpdateDescriptorSets(device, debugQuadWriteDescriptorSets.size(), debugQuadWriteDescriptorSets.data(), 0, NULL);

        // Shaded descriptor sets
        allocInfo.pSetLayouts = &descriptorSetLayouts.shaded;

        // Model
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.model));

        std::vector<vk::WriteDescriptorSet> modelWriteDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::writeDescriptorSet{descriptorSets.model, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.vsShared.descriptor}
        };
        vkUpdateDescriptorSets(device, modelWriteDescriptorSets.size(), modelWriteDescriptorSets.data(), 0, NULL);

        // Offscreen
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.offscreen));

        std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::writeDescriptorSet{descriptorSets.offscreen, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.vsOffScreen.descriptor}
        };
        vkUpdateDescriptorSets(device, offScreenWriteDescriptorSets.size(), offScreenWriteDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vk::pipelineInputAssemblyStateCreateInfo{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE};

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vk::pipelineRasterizationStateCreateInfo{VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_CLOCKWISE, 0};

        vk::PipelineColorBlendAttachmentState blendAttachmentState = vk::pipelineColorBlendAttachmentState{0xf, VK_FALSE};

        vk::PipelineColorBlendStateCreateInfo colorBlendState = vk::pipelineColorBlendStateCreateInfo{1, &blendAttachmentState};

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vk::pipelineDepthStencilStateCreateInfo{VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL};

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), dynamicStateEnables.size(), 0};

        // Solid rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/quad.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/quad.frag.spv", vk::ShaderStageFlagBits::eFragment);

        // Vertex bindings and attributes
        const std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        const std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { 0, 1, vk::Format::eR32G32sFloat, sizeof(float) * 3 },     // Location 1: UV
            { 0, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 5 },  // Location 2: Color
            { 0, 3, vk::Format::eR32G32B32sFloat, sizeof(float) * 8 },  // Location 3: Normal
        };
        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        vk::GraphicsPipelineCreateInfo pipelineCI{ pipelineLayouts.textured, renderPass, 0 };
        pipelineCI.pVertexInputState = &vertexInputState;
        pipelineCI.pInputAssemblyState = &inputAssemblyState;
        pipelineCI.pRasterizationState = &rasterizationState;
        pipelineCI.pColorBlendState = &colorBlendState;
        pipelineCI.pMultisampleState = &multisampleState;
        pipelineCI.pViewportState = &viewportState;
        pipelineCI.pDepthStencilState = &depthStencilState;
        pipelineCI.pDynamicState = &dynamicState;
        pipelineCI.stageCount = shaderStages.size();
        pipelineCI.pStages = shaderStages.data();

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.debug));

        // Mirror
        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/mirror.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/mirror.frag.spv", vk::ShaderStageFlagBits::eFragment);
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.mirror));

        // Flip culling
        rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;

        // Phong shading pipelines
        pipelineCI.layout = pipelineLayouts.shaded;
        // Scene
        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/phong.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/phong.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.shaded));
        // Offscreen
        // Flip culling
        rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
        pipelineCI.renderPass = offscreenPass.renderPass;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.shadedOffscreen));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Mesh vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.vsShared,
                                                   sizeof(uboShared)));

        // Mirror plane vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.vsMirror,
                                                   sizeof(uboShared)));

        // Offscreen vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.vsOffScreen,
                                                   sizeof(uboShared)));

        // Debug quad vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.vsDebugQuad,
                                                   sizeof(uboShared)));

        // Map persistent
        VK_CHECK_RESULT(uniformBuffers.vsShared.map());
        VK_CHECK_RESULT(uniformBuffers.vsMirror.map());
        VK_CHECK_RESULT(uniformBuffers.vsOffScreen.map());
        VK_CHECK_RESULT(uniformBuffers.vsDebugQuad.map());

        updateUniformBuffers();
        updateUniformBufferOffscreen();
    }

    void updateUniformBuffers() {
        // Mesh
        uboShared.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
        glm::mat4 viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

        uboShared.model = viewMatrix * glm::translate(glm::mat4(1.0f), cameraPos);
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.y + meshRot.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        uboShared.model = glm::translate(uboShared.model, meshPos);

        memcpy(uniformBuffers.vsShared.mapped, &uboShared, sizeof(uboShared));

        // Mirror
        uboShared.model = viewMatrix * glm::translate(glm::mat4(1.0f), cameraPos);
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        memcpy(uniformBuffers.vsMirror.mapped, &uboShared, sizeof(uboShared));

        // Debug quad
        uboShared.projection = glm::ortho(4.0f, 0.0f, 0.0f, 4.0f * (float)height / (float)width, -1.0f, 1.0f);
        uboShared.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f));

        memcpy(uniformBuffers.vsDebugQuad.mapped, &uboShared, sizeof(uboShared));
    }

    void updateUniformBufferOffscreen() {
        uboShared.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
        glm::mat4 viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

        uboShared.model = viewMatrix * glm::translate(glm::mat4(1.0f), cameraPos);
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.y + meshRot.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboShared.model = glm::rotate(uboShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        uboShared.model = glm::scale(uboShared.model, glm::vec3(1.0f, -1.0f, 1.0f));
        uboShared.model = glm::translate(uboShared.model, meshPos);

        memcpy(uniformBuffers.vsOffScreen.mapped, &uboShared, sizeof(uboShared));
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

        generateQuad();
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
        if (!paused) {
            meshRot.y += frameTimer * 10.0f;
            updateUniformBuffers();
            updateUniformBufferOffscreen();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffers();
        updateUniformBufferOffscreen();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Settings")) {
            if (overlay->checkBox("Display render target", &debugDisplay)) {
                buildCommandBuffers();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
