/*
* Vulkan Example - Using different pipelines in one single renderpass
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
#include "VulkanModel.hpp"
#include "VulkanBuffer.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
    } };

    struct {
        vkx::model::Model cube;
    } models;

    vks::Buffer uniformBuffer;

    // Same uniform buffer layout as shader
    struct UBOVS {
        glm::mat4 projection;
        glm::mat4 modelView;
        glm::vec4 lightPos = glm::vec4(0.0f, 2.0f, 1.0f, 0.0f);
    } uboVS;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    struct {
        vk::Pipeline phong;
        vk::Pipeline wireframe;
        vk::Pipeline toon;
    } pipelines;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        zoom = -10.5f;
        rotation = glm::vec3(-25.0f, 15.0f, 0.0f);
        title = "Pipeline state objects";
        settings.overlay = true;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        vkDestroyPipeline(device, pipelines.phong, nullptr);
        if (deviceFeatures.fillModeNonSolid) {
            vkDestroyPipeline(device, pipelines.wireframe, nullptr);
        }
        vkDestroyPipeline(device, pipelines.toon, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        models.cube.destroy();
        uniformBuffer.destroy();
    }

    // Enable physical device features required for this example
    virtual void getEnabledFeatures() {
        // Fill mode non solid is required for wireframe display
        if (deviceFeatures.fillModeNonSolid) {
            enabledFeatures.fillModeNonSolid = VK_TRUE;
            // Wide lines must be present for line width > 1.0f
            if (deviceFeatures.wideLines) {
                enabledFeatures.wideLines = VK_TRUE;
            }
        };
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

            vk::DeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.cube.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.cube.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            // Left : Solid colored
            viewport.width = (float)width / 3.0;
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);

            vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);

            // Center : Toon
            viewport.x = (float)width / 3.0;
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.toon);
            // Line width > 1.0f only if wide lines feature is supported
            if (deviceFeatures.wideLines) {
                vkCmdSetLineWidth(drawCmdBuffers[i], 2.0f);
            }
            vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);

            if (deviceFeatures.fillModeNonSolid) {
                // Right : Wireframe
                viewport.x = (float)width / 3.0 + (float)width / 3.0;
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.wireframe);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);
            }

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        models.cube.loadFromFile(context, getAssetPath() + "models/treasure_smooth.dae", vertexLayout, 1.0f);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 1 } };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = vk::descriptorPoolCreateInfo{poolSizes.size(), poolSizes.data(), 2};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Vertex shader uniform buffer
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0}
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), setLayoutBindings.size()};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::writeDescriptorSet{descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffer.descriptor}
        };

        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
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

        vk::PipelineMultisampleStateCreateInfo multisampleState = { VK_SAMPLE_COUNT_1_BIT };

        std::vector<vk::DynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
        };
        vk::PipelineDynamicStateCreateInfo dynamicState = { dynamicStateEnables };

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = { pipelineLayout, renderPass };

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();

        // Shared vertex bindings and attributes used by all pipelines

        // Binding description
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };

        // Attribute descriptions
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 1: Color
            { VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32sFloat, sizeof(float) * 6 },     // Location 2 : Texture coordinates
            { VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32B32sFloat, sizeof(float) * 8 },  // Location 3 : Normal
        };

        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        pipelineCreateInfo.pVertexInputState = &vertexInputState;

        // Create the graphics pipeline state objects

        // We are using this pipeline as the base for the other pipelines (derivatives)
        // Pipeline derivatives can be used for pipelines that share most of their state
        // Depending on the implementation this may result in better performance for pipeline
        // switchting and faster creation time
        pipelineCreateInfo.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;

        // Textured pipeline
        // Phong shading pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/pipelines/phong.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/pipelines/phong.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.phong));

        // All pipelines created after the base pipeline will be derivatives
        pipelineCreateInfo.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
        // Base pipeline will be our first created pipeline
        pipelineCreateInfo.basePipelineHandle = pipelines.phong;
        // It's only allowed to either use a handle or index for the base pipeline
        // As we use the handle, we must set the index to -1 (see section 9.5 of the specification)
        pipelineCreateInfo.basePipelineIndex = -1;

        // Toon shading pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/pipelines/toon.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/pipelines/toon.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.toon));

        // Pipeline for wire frame rendering
        // Non solid rendering is not a mandatory Vulkan feature
        if (deviceFeatures.fillModeNonSolid) {
            rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
            shaderStages[0] = loadShader(getAssetPath() + "shaders/pipelines/wireframe.vert.spv", vk::ShaderStageFlagBits::eVertex);
            shaderStages[1] = loadShader(getAssetPath() + "shaders/pipelines/wireframe.frag.spv", vk::ShaderStageFlagBits::eFragment);
            VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.wireframe));
        }
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Create the vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(uboVS)));

        // Map persistent
        VK_CHECK_RESULT(uniformBuffer.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)(width / 3.0f) / (float)height, 0.1f, 256.0f);

        glm::mat4 viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

        uboVS.modelView = viewMatrix * glm::translate(glm::mat4(1.0f), cameraPos);
        uboVS.modelView = glm::rotate(uboVS.modelView, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.modelView = glm::rotate(uboVS.modelView, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.modelView = glm::rotate(uboVS.modelView, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        memcpy(uniformBuffer.mapped, &uboVS, sizeof(uboVS));
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
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (!deviceFeatures.fillModeNonSolid) {
            if (overlay->header("Info")) {
                overlay->text("Non solid fill modes not supported!");
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()