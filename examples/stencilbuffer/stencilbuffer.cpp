/*
* Vulkan Example - Rendering outlines using the stencil buffer
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
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

class VulkanExample : public VulkanExampleBase {
public:
    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
    } };

    vkx::model::Model model;

    struct UBO {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec4 lightPos = glm::vec4(0.0f, -2.0f, 1.0f, 0.0f);
        // Vertex shader extrudes model by this value along normals for outlining
        float outlineWidth = 0.05f;
    } uboVS;

    vks::Buffer uniformBufferVS;

    struct {
        vk::Pipeline stencil;
        vk::Pipeline outline;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Stencil buffer outlines";
        timerSpeed *= 0.25f;
        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
        camera.setRotation(glm::vec3(2.5f, -35.0f, 0.0f));
        camera.setTranslation(glm::vec3(0.08f, 3.6f, -8.4f));
        settings.overlay = true;
    }

    ~VulkanExample() {
        vkDestroyPipeline(device, pipelines.stencil, nullptr);
        vkDestroyPipeline(device, pipelines.outline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        model.destroy();
        uniformBufferVS.destroy();
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
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            vk::Rect2D scissor{ width, height, 0, 0 };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            vk::DeviceSize offsets[1] = { 0 };

            vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

            // First pass renders object (toon shaded) and fills stencil buffer
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.stencil);
            vkCmdDrawIndexed(drawCmdBuffers[i], model.indexCount, 1, 0, 0, 0);

            // Second pass renders scaled object only where stencil was not set by first pass
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.outline);
            vkCmdDrawIndexed(drawCmdBuffers[i], model.indexCount, 1, 0, 0, 0);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        model.loadFromFile(context, getAssetPath() + "models/venus.fbx", vertexLayout, 0.3f);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 1 },
        };
        vk::DescriptorPoolCreateInfo descriptorPoolInfo = { static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 1 };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = { { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 } };

        vk::DescriptorSetLayoutCreateInfo descriptorLayoutInfo = { setLayoutBindings.data(), 1 };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo = { &descriptorSetLayout, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo = { descriptorPool, &descriptorSetLayout, 1 };
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
        std::vector<vk::WriteDescriptorSet> modelWriteDescriptorSets = { { descriptorSet, vk::DescriptorType::eUniformBuffer, 0,
                                                                           &uniformBufferVS.descriptor } };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(modelWriteDescriptorSets.size()), modelWriteDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };

        vk::PipelineRasterizationStateCreateInfo rasterizationState = { VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_CLOCKWISE, 0 };

        vk::PipelineColorBlendAttachmentState blendAttachmentState = { 0xf, VK_FALSE };

        vk::PipelineColorBlendStateCreateInfo colorBlendState = { 1, &blendAttachmentState };

        vk::PipelineDepthStencilStateCreateInfo depthStencilState = { VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = { VK_SAMPLE_COUNT_1_BIT, 0 };

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState = { dynamicStateEnables };

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = { pipelineLayout, renderPass, 0 };

        // Vertex bindings an attributes
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { 0, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 1: Color
            { 0, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 6 }   // Location 2: Normal
        };

        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        // Toon render and stencil fill pass
        shaderStages[0] = loadShader(getAssetPath() + "shaders/stencilbuffer/toon.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/stencilbuffer/toon.frag.spv", vk::ShaderStageFlagBits::eFragment);

        rasterizationState.cullMode = VK_CULL_MODE_NONE;

        depthStencilState.stencilTestEnable = VK_TRUE;

        depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilState.back.failOp = VK_STENCIL_OP_REPLACE;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_REPLACE;
        depthStencilState.back.passOp = VK_STENCIL_OP_REPLACE;
        depthStencilState.back.compareMask = 0xff;
        depthStencilState.back.writeMask = 0xff;
        depthStencilState.back.reference = 1;
        depthStencilState.front = depthStencilState.back;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.stencil));

        // Outline pass
        depthStencilState.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_REPLACE;
        depthStencilState.front = depthStencilState.back;
        depthStencilState.depthTestEnable = VK_FALSE;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/stencilbuffer/outline.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/stencilbuffer/outline.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.outline));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Mesh vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBufferVS,
                                                   sizeof(uboVS)));

        // Map persistent
        VK_CHECK_RESULT(uniformBufferVS.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboVS.projection = camera.matrices.perspective;
        uboVS.model = camera.matrices.view;
        memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
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
        if (overlay->header("Settings")) {
            if (overlay->inputFloat("Outline width", &uboVS.outlineWidth, 0.05f, 2)) {
                updateUniformBuffers();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
