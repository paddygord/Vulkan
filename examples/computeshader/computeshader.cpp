/*
* Vulkan Example - Compute shader image processing
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
#include "VulkanTexture.hpp"
#include "VulkanBuffer.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
};

class VulkanExample : public VulkanExampleBase {
private:
    vkx::texture::Texture2D textureColorMap;
    vkx::texture::Texture2D textureComputeTarget;

public:
    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    // Resources for the graphics part of the example
    struct {
        vk::DescriptorSetLayout descriptorSetLayout;  // Image display shader binding layout
        vk::DescriptorSet descriptorSetPreCompute;    // Image display shader bindings before compute shader image manipulation
        vk::DescriptorSet descriptorSetPostCompute;   // Image display shader bindings after compute shader image manipulation
        vk::Pipeline pipeline;                        // Image display pipeline
        vk::PipelineLayout pipelineLayout;            // Layout of the graphics pipeline
    } graphics;

    // Resources for the compute part of the example
    struct Compute {
        vk::Queue queue;                              // Separate queue for compute commands (queue family may differ from the one used for graphics)
        vk::CommandPool commandPool;                  // Use a separate command pool (queue family may differ from the one used for graphics)
        vk::CommandBuffer commandBuffer;              // Command buffer storing the dispatch commands and barriers
        vk::Fence fence;                              // Synchronization fence to avoid rewriting compute CB if still in use
        vk::DescriptorSetLayout descriptorSetLayout;  // Compute shader binding layout
        vk::DescriptorSet descriptorSet;              // Compute shader bindings
        vk::PipelineLayout pipelineLayout;            // Layout of the compute pipeline
        std::vector<vk::Pipeline> pipelines;          // Compute pipelines for image filters
        int32_t pipelineIndex = 0;                    // Current image filtering compute pipeline index
        uint32_t queueFamilyIndex;                    // Family index of the graphics queue, used for barriers
    } compute;

    vks::Buffer vertexBuffer;
    vks::Buffer indexBuffer;
    uint32_t indexCount;

    vks::Buffer uniformBufferVS;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVS;

    int vertexBufferSize;

    std::vector<std::string> shaderNames;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        zoom = -2.0f;
        title = "Compute shader image load/store";
        settings.overlay = true;
    }

    ~VulkanExample() {
        // Graphics
        vkDestroyPipeline(device, graphics.pipeline, nullptr);
        vkDestroyPipelineLayout(device, graphics.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, graphics.descriptorSetLayout, nullptr);

        // Compute
        for (auto& pipeline : compute.pipelines) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        vkDestroyPipelineLayout(device, compute.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, compute.descriptorSetLayout, nullptr);
        vkDestroyFence(device, compute.fence, nullptr);
        vkDestroyCommandPool(device, compute.commandPool, nullptr);

        vertexBuffer.destroy();
        indexBuffer.destroy();
        uniformBufferVS.destroy();

        textureColorMap.destroy();
        textureComputeTarget.destroy();
    }

    // Prepare a texture target that is used to store compute shader calculations
    void prepareTextureTarget(vks::Texture* tex, uint32_t width, uint32_t height, vk::Format format) {
        vk::FormatProperties formatProperties;

        // Get device properties for the requested texture format
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
        // Check if requested image format supports image storage operations
        assert(formatProperties.optimalTilingFeatures & vk::Format::eFEATURE_STORAGE_IMAGE_BIT);

        // Prepare blit target texture
        tex->width = width;
        tex->height = height;

        vk::ImageCreateInfo imageCreateInfo;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = { width, height, 1 };
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        // Image will be sampled in the fragment shader and used as storage target in the compute shader
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        imageCreateInfo.flags = 0;
        // Sharing mode exclusive means that ownership of the image does not need to be explicitly transferred between the compute and graphics queue
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vk::MemoryAllocateInfo memAllocInfo;
        vk::MemoryRequirements memReqs;

        VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &tex->image));

        vkGetImageMemoryRequirements(device, tex->image, &memReqs);
        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &tex->deviceMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device, tex->image, tex->deviceMemory, 0));

        vk::CommandBuffer layoutCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        tex->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        vks::tools::setImageLayout(layoutCmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, tex->imageLayout);

        VulkanExampleBase::flushCommandBuffer(layoutCmd, queue, true);

        // Create sampler
        vk::SamplerCreateInfo sampler;
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        sampler.addressModeV = sampler.addressModeU;
        sampler.addressModeW = sampler.addressModeU;
        sampler.mipLodBias = 0.0f;
        sampler.maxAnisotropy = 1.0f;
        sampler.compareOp = VK_COMPARE_OP_NEVER;
        sampler.minLod = 0.0f;
        sampler.maxLod = tex->mipLevels;
        sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &tex->sampler));

        // Create image view
        vk::ImageViewCreateInfo view;
        view.image = VK_NULL_HANDLE;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        view.image = tex->image;
        VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &tex->view));

        // Initialize a descriptor for later use
        tex->descriptor.imageLayout = tex->imageLayout;
        tex->descriptor.imageView = tex->view;
        tex->descriptor.sampler = tex->sampler;
        tex->device = vulkanDevice;
    }

    void loadAssets() {
        textureColorMap.loadFromFile(getAssetPath() + "textures/vulkan_11_rgba.ktx", vk::Format::eR8G8B8A8Unorm, vulkanDevice, queue,
                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_LAYOUT_GENERAL);
    }

    void buildCommandBuffers() {
        // Destroy command buffers if already present
        if (!checkCommandBuffers()) {
            destroyCommandBuffers();
            createCommandBuffers();
        }

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

            // Image memory barrier to make sure that compute shader writes are finished before sampling from the texture
            vk::ImageMemoryBarrier imageMemoryBarrier = {};
            imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            // We won't be changing the layout of the image
            imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageMemoryBarrier.image = textureComputeTarget.image;
            imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_FLAGS_NONE, 0, nullptr, 0,
                                 nullptr, 1, &imageMemoryBarrier);
            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vk::Viewport viewport{ (float)width * 0.5f, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            vk::Rect2D scissor{ width, height, 0, 0 };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            vk::DeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &vertexBuffer.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            // Left (pre compute)
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipelineLayout, 0, 1, &graphics.descriptorSetPreCompute, 0,
                                    NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipeline);

            vkCmdDrawIndexed(drawCmdBuffers[i], indexCount, 1, 0, 0, 0);

            // Right (post compute)
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipelineLayout, 0, 1, &graphics.descriptorSetPostCompute, 0,
                                    NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipeline);

            viewport.x = (float)width / 2.0f;
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
            vkCmdDrawIndexed(drawCmdBuffers[i], indexCount, 1, 0, 0, 0);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void buildComputeCommandBuffer() {
        // Flush the queue if we're rebuilding the command buffer after a pipeline change to ensure it's not currently in use
        vkQueueWaitIdle(compute.queue);

        vk::CommandBufferBeginInfo cmdBufInfo;

        VK_CHECK_RESULT(vkBeginCommandBuffer(compute.commandBuffer, &cmdBufInfo));

        vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelines[compute.pipelineIndex]);
        vkCmdBindDescriptorSets(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0, 0);

        vkCmdDispatch(compute.commandBuffer, textureComputeTarget.width / 16, textureComputeTarget.height / 16, 1);

        vkEndCommandBuffer(compute.commandBuffer);
    }

    // Setup vertices for a single uv-mapped quad
    void generateQuad() {
        // Setup vertices for a single uv-mapped quad made from two triangles
        std::vector<Vertex> vertices = { { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
                                         { { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
                                         { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
                                         { { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } } };

        // Setup indices
        std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };
        indexCount = static_cast<uint32_t>(indices.size());

        // Create buffers
        // For the sake of simplicity we won't stage the vertex data to the gpu memory
        // Vertex buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexBuffer,
                                                   vertices.size() * sizeof(Vertex), vertices.data()));
        // Index buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                   &indexBuffer, indices.size() * sizeof(uint32_t), indices.data()));
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions = { { VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex } };

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions = {
            // Location 0: Position
            { VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32sFloat, offsetof(Vertex, pos) },
            // Location 1: Texture coordinates
            { VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32sFloat, offsetof(Vertex, uv) },
        };

        // Assign to vertex buffer
        vertices.inputState;
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            // Graphics pipelines uniform buffers
            { vk::DescriptorType::eUniformBuffer, 2 },
            // Graphics pipelines image samplers for displaying compute output image
            { vk::DescriptorType::eCombinedImageSampler, 2 },
            // Compute pipelines uses a storage image for image reads and writes
            { vk::DescriptorType::eSTORAGE_IMAGE, 2 },
        };
        vk::DescriptorPoolCreateInfo descriptorPoolInfo{ poolSizes, 3 };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = { // Binding 0: Vertex shader uniform buffer
                                                                          { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 },
                                                                          // Binding 1: Fragment shader input image
                                                                          { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1 }
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout{ setLayoutBindings };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &graphics.descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo{ &graphics.descriptorSetLayout, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &graphics.pipelineLayout));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo = { descriptorPool, &graphics.descriptorSetLayout, 1 };

        // Input image (before compute post processing)
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &graphics.descriptorSetPreCompute));
        std::vector<vk::WriteDescriptorSet> baseImageWriteDescriptorSets = {
            { graphics.descriptorSetPreCompute, vk::DescriptorType::eUniformBuffer, 0, &uniformBufferVS.descriptor },
            { graphics.descriptorSetPreCompute, vk::DescriptorType::eCombinedImageSampler, 1, &textureColorMap.descriptor }
        };
        vkUpdateDescriptorSets(device, baseImageWriteDescriptorSets.size(), baseImageWriteDescriptorSets.data(), 0, nullptr);

        // Final image (after compute shader processing)
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &graphics.descriptorSetPostCompute));
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            { graphics.descriptorSetPostCompute, vk::DescriptorType::eUniformBuffer, 0, &uniformBufferVS.descriptor },
            { graphics.descriptorSetPostCompute, vk::DescriptorType::eCombinedImageSampler, 1, &textureComputeTarget.descriptor }
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vk::pipelineInputAssemblyStateCreateInfo{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE};

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vk::pipelineRasterizationStateCreateInfo{VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0};

        vk::PipelineColorBlendAttachmentState blendAttachmentState = vk::pipelineColorBlendAttachmentState{0xf, VK_FALSE};

        vk::PipelineColorBlendStateCreateInfo colorBlendState = vk::pipelineColorBlendStateCreateInfo{1, &blendAttachmentState};

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vk::pipelineDepthStencilStateCreateInfo{VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL};

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), dynamicStateEnables.size(), 0};

        // Rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/computeshader/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/computeshader/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::pipelineCreateInfo{graphics.pipelineLayout, renderPass, 0};

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
        pipelineCreateInfo.renderPass = renderPass;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &graphics.pipeline));
    }

    // Find and create a compute capable device queue
    void getComputeQueue() {
        uint32_t queueFamilyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);
        assert(queueFamilyCount >= 1);

        std::vector<vk::QueueFamilyProperties> queueFamilyProperties;
        queueFamilyProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

        // Some devices have dedicated compute queues, so we first try to find a queue that supports compute and not graphics
        bool computeQueueFound = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
            if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)) {
                compute.queueFamilyIndex = i;
                computeQueueFound = true;
                break;
            }
        }
        // If there is no dedicated compute queue, just find the first queue family that supports compute
        if (!computeQueueFound) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++) {
                if (queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    compute.queueFamilyIndex = i;
                    computeQueueFound = true;
                    break;
                }
            }
        }

        // Compute is mandatory in Vulkan, so there must be at least one queue family that supports compute
        assert(computeQueueFound);
        // Get a compute queue from the device
        vkGetDeviceQueue(device, compute.queueFamilyIndex, 0, &compute.queue);
    }

    void prepareCompute() {
        getComputeQueue();

        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines even if they use the same queue

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0: Input image (read-only)
            { vk::DescriptorType::eSTORAGE_IMAGE, vk::ShaderStageFlagBits::eCOMPUTE, 0 },
            // Binding 1: Output image (write)
            { vk::DescriptorType::eSTORAGE_IMAGE, vk::ShaderStageFlagBits::eCOMPUTE, 1 },
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout{ setLayoutBindings };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &compute.descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = { &compute.descriptorSetLayout, 1 };

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &compute.pipelineLayout));

        vk::DescriptorSetAllocateInfo allocInfo = { descriptorPool, &compute.descriptorSetLayout, 1 };

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &compute.descriptorSet));
        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets = {
            { compute.descriptorSet, vk::DescriptorType::eSTORAGE_IMAGE, 0, &textureColorMap.descriptor },
            { compute.descriptorSet, vk::DescriptorType::eSTORAGE_IMAGE, 1, &textureComputeTarget.descriptor }
        };
        vkUpdateDescriptorSets(device, computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, NULL);

        // Create compute shader pipelines
        vk::ComputePipelineCreateInfo computePipelineCreateInfo = { compute.pipelineLayout, 0 };

        // One pipeline for each effect
        shaderNames = { "emboss", "edgedetect", "sharpen" };
        for (auto& shaderName : shaderNames) {
            std::string fileName = getAssetPath() + "shaders/computeshader/" + shaderName + ".comp.spv";
            computePipelineCreateInfo.stage = loadShader(fileName, vk::ShaderStageFlagBits::eCOMPUTE);
            vk::Pipeline pipeline;
            VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &pipeline));
            compute.pipelines.push_back(pipeline);
        }

        // Separate command pool as queue family for compute may be different than graphics
        vk::CommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = compute.queueFamilyIndex;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &compute.commandPool));

        // Create a command buffer for compute operations
        vk::CommandBufferAllocateInfo cmdBufAllocateInfo =
            vk::commandBufferAllocateInfo{compute.commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};

        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &compute.commandBuffer));

        // Fence for compute CB sync
        vk::FenceCreateInfo fenceCreateInfo{ VK_FENCE_CREATE_SIGNALED_BIT };
        VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &compute.fence));

        // Build a single command buffer containing the compute dispatch commands
        buildComputeCommandBuffer();
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBufferVS,
                                                   sizeof(uboVS)));

        // Map persistent
        VK_CHECK_RESULT(uniformBufferVS.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader uniform buffer block
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width * 0.5f / (float)height, 0.1f, 256.0f);
        glm::mat4 viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

        uboVS.model = viewMatrix * glm::translate(glm::mat4(1.0f), cameraPos);
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
    }

    void draw() {
        VulkanExampleBase::prepareFrame();

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

        VulkanExampleBase::submitFrame();

        // Submit compute commands
        // Use a fence to ensure that compute command buffer has finished executin before using it again
        vkWaitForFences(device, 1, &compute.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &compute.fence);

        vk::SubmitInfo computeSubmitInfo;
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;

        VK_CHECK_RESULT(vkQueueSubmit(compute.queue, 1, &computeSubmitInfo, compute.fence));
    }

    void prepare() {
        VulkanExampleBase::prepare();

        generateQuad();
        setupVertexDescriptions();
        prepareUniformBuffers();
        prepareTextureTarget(&textureComputeTarget, textureColorMap.width, textureColorMap.height, vk::Format::eR8G8B8A8Unorm);
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        prepareCompute();
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
            if (overlay->comboBox("Shader", &compute.pipelineIndex, shaderNames)) {
                buildComputeCommandBuffer();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
