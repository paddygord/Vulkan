/*
* Vulkan Example - Using input attachments
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Input attachments can be used to read attachment contents from a previous sub pass
* at the same pixel position within a single render pass
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
#include "VulkanModel.hpp"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
    } };

    vkx::model::Model scene;

    struct UBOMatrices {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 view;
    } uboMatrices;

    struct UBOParams {
        glm::vec2 brightnessContrast = glm::vec2(0.5f, 1.8f);
        glm::vec2 range = glm::vec2(0.6f, 1.0f);
        int32_t attachmentIndex = 1;
    } uboParams;

    struct {
        vks::Buffer matrices;
        vks::Buffer params;
    } uniformBuffers;

    struct {
        vk::Pipeline attachmentWrite;
        vk::Pipeline attachmentRead;
    } pipelines;

    struct {
        vk::PipelineLayout attachmentWrite;
        vk::PipelineLayout attachmentRead;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet attachmentWrite;
        std::vector<vk::DescriptorSet> attachmentRead;
    } descriptorSets;

    struct {
        vk::DescriptorSetLayout attachmentWrite;
        vk::DescriptorSetLayout attachmentRead;
    } descriptorSetLayouts;

    struct FrameBufferAttachment {
        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView view;
        vk::Format format;
    };
    struct Attachments {
        FrameBufferAttachment color, depth;
    };
    std::vector<Attachments> attachments;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Input attachments";
        camera.type = Camera::CameraType::firstperson;
        camera.movementSpeed = 2.5f;
        camera.setPosition(glm::vec3(1.65f, 1.75f, -6.15f));
        camera.setRotation(glm::vec3(-12.75f, 380.0f, 0.0f));
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
        settings.overlay = true;
        UIOverlay.subpass = 1;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class

        for (uint32_t i = 0; i < attachments.size(); i++) {
            vkDestroyImageView(device, attachments[i].color.view, nullptr);
            vkDestroyImage(device, attachments[i].color.image, nullptr);
            vkFreeMemory(device, attachments[i].color.memory, nullptr);
            vkDestroyImageView(device, attachments[i].depth.view, nullptr);
            vkDestroyImage(device, attachments[i].depth.image, nullptr);
            vkFreeMemory(device, attachments[i].depth.memory, nullptr);
        }

        vkDestroyPipeline(device, pipelines.attachmentRead, nullptr);
        vkDestroyPipeline(device, pipelines.attachmentWrite, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayouts.attachmentWrite, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayouts.attachmentRead, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.attachmentWrite, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.attachmentRead, nullptr);

        scene.destroy();
        uniformBuffers.matrices.destroy();
        uniformBuffers.params.destroy();
    }

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
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        vk::ImageCreateInfo imageCI;
        imageCI.imageType = VK_IMAGE_TYPE_2D;
        imageCI.format = format;
        imageCI.extent.width = width;
        imageCI.extent.height = height;
        imageCI.extent.depth = 1;
        imageCI.mipLevels = 1;
        imageCI.arrayLayers = 1;
        imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        // VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT flag is required for input attachments;
        imageCI.usage = usage | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &attachment->image));

        vk::MemoryAllocateInfo memAlloc;
        vk::MemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->memory));
        VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->memory, 0));

        vk::ImageViewCreateInfo imageViewCI;
        imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCI.format = format;
        imageViewCI.subresourceRange = {};
        imageViewCI.subresourceRange.aspectMask = aspectMask;
        imageViewCI.subresourceRange.baseMipLevel = 0;
        imageViewCI.subresourceRange.levelCount = 1;
        imageViewCI.subresourceRange.baseArrayLayer = 0;
        imageViewCI.subresourceRange.layerCount = 1;
        imageViewCI.image = attachment->image;
        VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &attachment->view));
    }

    // Override framebuffer setup from base class
    void setupFrameBuffer() {
        vk::ImageView views[3];

        vk::FramebufferCreateInfo frameBufferCI{};
        frameBufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCI.renderPass = renderPass;
        frameBufferCI.attachmentCount = 3;
        frameBufferCI.pAttachments = views;
        frameBufferCI.width = width;
        frameBufferCI.height = height;
        frameBufferCI.layers = 1;

        frameBuffers.resize(swapChain.imageCount);
        for (uint32_t i = 0; i < frameBuffers.size(); i++) {
            views[0] = swapChain.buffers[i].view;
            views[1] = attachments[i].color.view;
            views[2] = attachments[i].depth.view;
            VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCI, nullptr, &frameBuffers[i]));
        }
    }

    // Override render pass setup from base class
    void setupRenderPass() {
        const vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm;

        attachments.resize(swapChain.imageCount);
        for (auto i = 0; i < attachments.size(); i++) {
            createAttachment(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments[i].color);
            createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &attachments[i].depth);
        }

        std::array<vk::AttachmentDescription, 3> attachments{};

        // Swap chain image color attachment
        // Will be transitioned to present layout
        attachments[0].format = swapChain.colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // Input attachments
        // These will be written in the first subpass, transitioned to input attachments
        // and then read in the secod subpass

        // Color
        attachments[1].format = colorFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // Depth
        attachments[2].format = depthFormat;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::array<vk::SubpassDescription, 2> subpassDescriptions{};

        /*
			First subpass
			Fill the color and depth attachments
		*/
        vk::AttachmentReference colorReference = { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        vk::AttachmentReference depthReference = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        subpassDescriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescriptions[0].colorAttachmentCount = 1;
        subpassDescriptions[0].pColorAttachments = &colorReference;
        subpassDescriptions[0].pDepthStencilAttachment = &depthReference;

        /*
			Second subpass
			Input attachment read and swap chain color attachment write
		*/

        // Color reference (target) for this sub pass is the swap chain color attachment
        vk::AttachmentReference colorReferenceSwapchain = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        subpassDescriptions[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescriptions[1].colorAttachmentCount = 1;
        subpassDescriptions[1].pColorAttachments = &colorReferenceSwapchain;

        // Color and depth attachment written to in first sub pass will be used as input attachments to be read in the fragment shader
        vk::AttachmentReference inputReferences[2];
        inputReferences[0] = { 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        inputReferences[1] = { 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        // Use the attachments filled in the first pass as input attachments
        subpassDescriptions[1].inputAttachmentCount = 2;
        subpassDescriptions[1].pInputAttachments = inputReferences;

        /*
			Subpass dependencies for layout transitions
		*/
        std::array<vk::SubpassDependency, 3> dependencies;

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

        dependencies[2].srcSubpass = 0;
        dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        vk::RenderPassCreateInfo renderPassInfoCI{};
        renderPassInfoCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfoCI.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfoCI.pAttachments = attachments.data();
        renderPassInfoCI.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
        renderPassInfoCI.pSubpasses = subpassDescriptions.data();
        renderPassInfoCI.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfoCI.pDependencies = dependencies.data();
        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfoCI, nullptr, &renderPass));
    }

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[3];
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
        clearValues[1].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
        clearValues[2].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 3;
        renderPassBeginInfo.pClearValues = clearValues;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            vk::Rect2D scissor{ width, height, 0, 0 };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            vk::DeviceSize offsets[1] = { 0 };

            /*
				First sub pass
				Fills the attachments
			*/
            {
                vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 0: Writing attachments", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.attachmentWrite);
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.attachmentWrite, 0, 1,
                                        &descriptorSets.attachmentWrite, 0, NULL);
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &scene.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], scene.indexCount, 1, 0, 0, 0);

                vks::debugmarker::endRegion(drawCmdBuffers[i]);
            }

            /*
				Second sub pass
				Render a full screen quad, reading from the previously written attachments via input attachments
			*/
            {
                vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 1: Reading attachments", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

                vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.attachmentRead);
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.attachmentRead, 0, 1,
                                        &descriptorSets.attachmentRead[i], 0, NULL);
                vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

                vks::debugmarker::endRegion(drawCmdBuffers[i]);
            }

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        scene.loadFromFile(context, getAssetPath() + "models/treasure_smooth.dae", vertexLayout, 1.0f);
    }

    void setupDescriptors() {
        /*
			Pool
		*/
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, attachments.size() + 1 },
            { vk::DescriptorType::eCombinedImageSampler, attachments.size() + 1 },
            { vk::DescriptorType::eINPUT_ATTACHMENT, attachments.size() * 2 + 1 },
        };
        vk::DescriptorPoolCreateInfo descriptorPoolInfo{ static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 4 };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

        /*
			Attachment write
		*/
        {
            std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = { { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 } };
            vk::DescriptorSetLayoutCreateInfo descriptorLayout{ setLayoutBindings };
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.attachmentWrite));

            vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo{ &descriptorSetLayouts.attachmentWrite, 1 };
            VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayouts.attachmentWrite));

            vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, &descriptorSetLayouts.attachmentWrite, 1 };
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.attachmentWrite));

            vk::WriteDescriptorSet writeDescriptorSet{ descriptorSets.attachmentWrite, vk::DescriptorType::eUniformBuffer, 0,
                                                       &uniformBuffers.matrices.descriptor };
            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        }

        /*
			Attachment read
		*/
        {
            std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
                // Binding 0: Color input attachment
                { vk::DescriptorType::eINPUT_ATTACHMENT, vk::ShaderStageFlagBits::eFragment, 0 },
                // Binding 1: Depth input attachment
                { vk::DescriptorType::eINPUT_ATTACHMENT, vk::ShaderStageFlagBits::eFragment, 1 },
                // Binding 2: Display parameters uniform buffer
                { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 2 },
            };
            vk::DescriptorSetLayoutCreateInfo descriptorLayoutCI{ setLayoutBindings };
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.attachmentRead));

            vk::PipelineLayoutCreateInfo pipelineLayoutCI{ &descriptorSetLayouts.attachmentRead, 1 };
            VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.attachmentRead));

            descriptorSets.attachmentRead.resize(attachments.size());
            for (auto i = 0; i < descriptorSets.attachmentRead.size(); i++) {
                vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, &descriptorSetLayouts.attachmentRead, 1 };
                VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.attachmentRead[i]));

                // Image descriptors for the input attachments read by the shader
                std::vector<vk::DescriptorImageInfo> descriptors = { { VK_NULL_HANDLE, attachments[i].color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                                     { VK_NULL_HANDLE, attachments[i].depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
                std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
                    // Binding 0: Color input attachment
                    { descriptorSets.attachmentRead[i], vk::DescriptorType::eINPUT_ATTACHMENT, 0, &descriptors[0] },
                    // Binding 1: Depth input attachment
                    { descriptorSets.attachmentRead[i], vk::DescriptorType::eINPUT_ATTACHMENT, 1, &descriptors[1] },
                    // Binding 2: Display parameters uniform buffer
                    { descriptorSets.attachmentRead[i], vk::DescriptorType::eUniformBuffer, 2, &uniformBuffers.params.descriptor },
                };
                vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
            }
        }
    }

    void preparePipelines() {
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{ VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };
        vk::PipelineRasterizationStateCreateInfo rasterizationStateCI{ VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0 };
        vk::PipelineColorBlendAttachmentState blendAttachmentState{ 0xf, VK_FALSE };
        vk::PipelineColorBlendStateCreateInfo colorBlendStateCI{ 1, &blendAttachmentState };
        vk::PipelineDepthStencilStateCreateInfo depthStencilStateCI{ VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };
        vk::PipelineViewportStateCreateInfo viewportStateCI{ 1, 1, 0 };
        vk::PipelineMultisampleStateCreateInfo multisampleStateCI{ VK_SAMPLE_COUNT_1_BIT, 0 };
        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicStateCI{ dynamicStateEnables };
        vk::GraphicsPipelineCreateInfo pipelineCI;

        pipelineCI.renderPass = renderPass;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;
        pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCI.pStages = shaderStages.data();

        /*
			Attachment write
		*/

        // Pipeline will be used in first sub pass
        pipelineCI.subpass = 0;
        pipelineCI.layout = pipelineLayouts.attachmentWrite;

        // Binding description
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };

        // Attribute descriptions
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { 0, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 1: Color
            { 0, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 6 },  // Location 2: Normal
        };

        vk::PipelineVertexInputStateCreateInfo vertexInputStateCI;
        vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputStateCI.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

        pipelineCI.pVertexInputState = &vertexInputStateCI;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/inputattachments/attachmentwrite.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/inputattachments/attachmentwrite.frag.spv", vk::ShaderStageFlagBits::eFragment);

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.attachmentWrite));

        /*
			Attachment read
		*/

        // Pipeline will be used in second sub pass
        pipelineCI.subpass = 1;
        pipelineCI.layout = pipelineLayouts.attachmentRead;

        vk::PipelineVertexInputStateCreateInfo emptyInputStateCI{};
        emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        pipelineCI.pVertexInputState = &emptyInputStateCI;
        colorBlendStateCI.attachmentCount = 1;
        rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
        depthStencilStateCI.depthWriteEnable = VK_FALSE;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/inputattachments/attachmentread.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/inputattachments/attachmentread.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.attachmentRead));
    }

    void prepareUniformBuffers() {
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &uniformBuffers.matrices, sizeof(uboMatrices));
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &uniformBuffers.params, sizeof(uboParams));
        VK_CHECK_RESULT(uniformBuffers.matrices.map());
        VK_CHECK_RESULT(uniformBuffers.params.map());
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboMatrices.projection = camera.matrices.perspective;
        uboMatrices.view = camera.matrices.view;
        uboMatrices.model = glm::mat4(1.0f);
        memcpy(uniformBuffers.matrices.mapped, &uboMatrices, sizeof(uboMatrices));
        memcpy(uniformBuffers.params.mapped, &uboParams, sizeof(uboParams));
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
        setupDescriptors();
        preparePipelines();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        if (camera.updated) {
            updateUniformBuffers();
        }
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Settings")) {
            overlay->text("Input attachment");
            if (overlay->comboBox("##attachment", &uboParams.attachmentIndex, { "color", "depth" })) {
                updateUniformBuffers();
            }
            switch (uboParams.attachmentIndex) {
                case 0:
                    overlay->text("Brightness");
                    if (overlay->sliderFloat("##b", &uboParams.brightnessContrast[0], 0.0f, 2.0f)) {
                        updateUniformBuffers();
                    }
                    overlay->text("Contrast");
                    if (overlay->sliderFloat("##c", &uboParams.brightnessContrast[1], 0.0f, 4.0f)) {
                        updateUniformBuffers();
                    }
                    break;
                case 1:
                    overlay->text("Visible range");
                    if (overlay->sliderFloat("min", &uboParams.range[0], 0.0f, uboParams.range[1])) {
                        updateUniformBuffers();
                    }
                    if (overlay->sliderFloat("max", &uboParams.range[1], uboParams.range[0], 1.0f)) {
                        updateUniformBuffers();
                    }
                    break;
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
