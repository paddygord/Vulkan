/*
* Vulkan Example - Geometry shader (vertex normal debugging)
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

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    bool displayNormals = true;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
    } };

    struct {
        vkx::model::Model object;
    } models;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVS;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec2 viewportDim;
    } uboGS;

    struct {
        vks::Buffer VS;
        vks::Buffer GS;
    } uniformBuffers;

    struct {
        vk::Pipeline solid;
        vk::Pipeline normals;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        zoom = -8.0f;
        rotation = glm::vec3(0.0f, -25.0f, 0.0f);
        title = "Geometry shader normal debugging";
        settings.overlay = true;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        vkDestroyPipeline(device, pipelines.solid, nullptr);
        vkDestroyPipeline(device, pipelines.normals, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        models.object.destroy();

        uniformBuffers.GS.destroy();
        uniformBuffers.VS.destroy();
    }

    // Enable physical device features required for this example
    virtual void getEnabledFeatures() {
        // Geometry shader support is required for this example
        if (deviceFeatures.geometryShader) {
            enabledFeatures.geometryShader = VK_TRUE;
        } else {
            vks::tools::exitFatal("Selected GPU does not support geometry shaders!", VK_ERROR_FEATURE_NOT_PRESENT);
        }
    }

    void reBuildCommandBuffers() {
        if (!checkCommandBuffers()) {
            destroyCommandBuffers();
            createCommandBuffers();
        }
        buildCommandBuffers();
    }

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
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

                        vk::Viewport viewport { (float)width, (float }height, 0.0f, 1.0f
			);
                        vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

                        vk::Rect2D scissor{ width, height, 0, 0 };
                        vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                        vkCmdSetLineWidth(drawCmdBuffers[i], 1.0f);

                        vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

                        vk::DeviceSize offsets[1] = { 0 };
                        vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.object.vertices.buffer, offsets);
                        vkCmdBindIndexBuffer(drawCmdBuffers[i], models.object.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

                        // Solid shading
                        vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.solid);
                        vkCmdDrawIndexed(drawCmdBuffers[i], models.object.indexCount, 1, 0, 0, 0);

                        // Normal debugging
                        if (displayNormals) {
                            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.normals);
                            vkCmdDrawIndexed(drawCmdBuffers[i], models.object.indexCount, 1, 0, 0, 0);
                        }

                        drawUI(drawCmdBuffers[i]);

                        vkCmdEndRenderPass(drawCmdBuffers[i]);

                        VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        models.object.loadFromFile(context, getAssetPath() + "models/suzanne.obj", vertexLayout, 0.25f);
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

        // Location 1 : Normals
        vertices.attributeDescriptions[1] =
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3};

        // Location 2 : Color
        vertices.attributeDescriptions[2] =
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 6};

        // Assign to vertex buffer
        vertices.inputState;
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // Example uses two ubos
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 2 },
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = vk::descriptorPoolCreateInfo{poolSizes.size(), poolSizes.data(), 2};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Vertex shader ubo
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0},
            // Binding 1 : Geometry shader ubo
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eGEOMETRY, 1}
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
            // Binding 0 : Vertex shader shader ubo
            vk::writeDescriptorSet{descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.VS.descriptor},
            // Binding 1 : Geometry shader ubo
            vk::writeDescriptorSet{descriptorSet, vk::DescriptorType::eUniformBuffer, 1, &uniformBuffers.GS.descriptor}
        };

        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vk::pipelineInputAssemblyStateCreateInfo{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE};

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vk::pipelineRasterizationStateCreateInfo{VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, 0};

        vk::PipelineColorBlendAttachmentState blendAttachmentState = vk::pipelineColorBlendAttachmentState{0xf, VK_FALSE};

        vk::PipelineColorBlendStateCreateInfo colorBlendState = vk::pipelineColorBlendStateCreateInfo{1, &blendAttachmentState};

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vk::pipelineDepthStencilStateCreateInfo{VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL};

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), dynamicStateEnables.size(), 0};

        // Tessellation pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 3> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/geometryshader/base.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/geometryshader/base.frag.spv", vk::ShaderStageFlagBits::eFragment);
        shaderStages[2] = loadShader(getAssetPath() + "shaders/geometryshader/normaldebug.geom.spv", vk::ShaderStageFlagBits::eGEOMETRY);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::pipelineCreateInfo{pipelineLayout, renderPass, 0};

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

        // Normal debugging pipeline
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.normals));

        // Solid rendering pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/geometryshader/mesh.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/geometryshader/mesh.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelineCreateInfo.stageCount = 2;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.VS,
                                                   sizeof(uboVS)));

        // Geometry shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.GS,
                                                   sizeof(uboGS)));

        // Map persistent
        VK_CHECK_RESULT(uniformBuffers.VS.map());
        VK_CHECK_RESULT(uniformBuffers.GS.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
        glm::mat4 viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));
        uboVS.model = viewMatrix * glm::translate(glm::mat4(1.0f), cameraPos);
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        memcpy(uniformBuffers.VS.mapped, &uboVS, sizeof(uboVS));

        // Geometry shader
        uboGS.model = uboVS.model;
        uboGS.projection = uboVS.projection;
        uboGS.viewportDim = glm::vec2(width, height);
        memcpy(uniformBuffers.GS.mapped, &uboGS, sizeof(uboGS));
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
            if (overlay->checkBox("Display normals", &displayNormals)) {
                buildCommandBuffers();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()