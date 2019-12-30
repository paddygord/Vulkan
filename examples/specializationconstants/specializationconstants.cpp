/*
* Vulkan Example - Shader specialization constants
*
* For details see https://www.khronos.org/registry/vulkan/specs/misc/GL_KHR_vulkan_glsl.txt
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
#include "VulkanModel.hpp"
#include "VulkanBuffer.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

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

    struct {
        vkx::texture::Texture2D colormap;
    } textures;

    vks::Buffer uniformBuffer;

    // Same uniform buffer layout as shader
    struct UBOVS {
        glm::mat4 projection;
        glm::mat4 modelView;
        glm::vec4 lightPos = glm::vec4(0.0f, -2.0f, 1.0f, 0.0f);
    } uboVS;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    struct {
        vk::Pipeline phong;
        vk::Pipeline toon;
        vk::Pipeline textured;
    } pipelines;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Specialization constants";
        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f);
        camera.setRotation(glm::vec3(-40.0f, -90.0f, 0.0f));
        camera.setTranslation(glm::vec3(0.0f, 0.0f, -2.0f));
        settings.overlay = true;
    }

    ~VulkanExample() {
        vkDestroyPipeline(device, pipelines.phong, nullptr);
        vkDestroyPipeline(device, pipelines.textured, nullptr);
        vkDestroyPipeline(device, pipelines.toon, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        models.cube.destroy();
        textures.colormap.destroy();
        uniformBuffer.destroy();
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

            // Left
            viewport.width = (float)width / 3.0f;
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);

            vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);

            // Center
            viewport.x = (float)width / 3.0f;
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.toon);
            vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);

            // Right
            viewport.x = (float)width / 3.0f + (float)width / 3.0f;
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.textured);
            vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        models.cube.loadFromFile(context, getAssetPath() + "models/color_teapot_spheres.dae", vertexLayout, 0.1f);
        textures.colormap.loadFromFile(context, getAssetPath() + "textures/metalplate_nomips_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions = {
            { VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };

        // Attribute descriptions
        vertices.attributeDescriptions = {
            // Location 0 : Position
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32sFloat, 0},
            // Location 1 : Color
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3},
            // Location 3 : Texture coordinates
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32sFloat, sizeof(float) * 6},
            // Location 2 : Normal
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32B32sFloat, sizeof(float) * 8},
        };

        vertices.inputState;
        vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 1 }, { vk::DescriptorType::eCombinedImageSampler, 1 } };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vk::descriptorPoolCreateInfo{static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 1};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 },
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1 },
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size())};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            { descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffer.descriptor },
            { descriptorSet, vk::DescriptorType::eCombinedImageSampler, 1, &textures.colormap.descriptor },
        };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
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

        std::vector<vk::DynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
        };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0};

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::pipelineCreateInfo{pipelineLayout, renderPass, 0};

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

        // Prepare specialization data

        // Host data to take specialization constants from
        struct SpecializationData {
            // Sets the lighting model used in the fragment "uber" shader
            uint32_t lightingModel;
            // Parameter for the toon shading part of the fragment shader
            float toonDesaturationFactor = 0.5f;
        } specializationData;

        // Each shader constant of a shader stage corresponds to one map entry
        std::array<vk::SpecializationMapEntry, 2> specializationMapEntries;
        // Shader bindings based on specialization constants are marked by the new "constant_id" layout qualifier:
        //	layout (constant_id = 0) const int LIGHTING_MODEL = 0;
        //	layout (constant_id = 1) const float PARAM_TOON_DESATURATION = 0.0f;

        // Map entry for the lighting model to be used by the fragment shader
        specializationMapEntries[0].constantID = 0;
        specializationMapEntries[0].size = sizeof(specializationData.lightingModel);
        specializationMapEntries[0].offset = 0;

        // Map entry for the toon shader parameter
        specializationMapEntries[1].constantID = 1;
        specializationMapEntries[1].size = sizeof(specializationData.toonDesaturationFactor);
        specializationMapEntries[1].offset = offsetof(SpecializationData, toonDesaturationFactor);

        // Prepare specialization info block for the shader stage
        vk::SpecializationInfo specializationInfo{};
        specializationInfo.dataSize = sizeof(specializationData);
        specializationInfo.mapEntryCount = static_cast<uint32_t>(specializationMapEntries.size());
        specializationInfo.pMapEntries = specializationMapEntries.data();
        specializationInfo.pData = &specializationData;

        // Create pipelines
        // All pipelines will use the same "uber" shader and specialization constants to change branching and parameters of that shader
        shaderStages[0] = loadShader(getAssetPath() + "shaders/specializationconstants/uber.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/specializationconstants/uber.frag.spv", vk::ShaderStageFlagBits::eFragment);
        // Specialization info is assigned is part of the shader stage (modul) and must be set after creating the module and before creating the pipeline
        shaderStages[1].pSpecializationInfo = &specializationInfo;

        // Solid phong shading
        specializationData.lightingModel = 0;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.phong));

        // Phong and textured
        specializationData.lightingModel = 1;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.toon));

        // Textured discard
        specializationData.lightingModel = 2;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.textured));
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
        camera.setPerspective(60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f);

        uboVS.projection = camera.matrices.perspective;
        uboVS.modelView = camera.matrices.view;

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
        if (!prepared) {
            return;
        }
        draw();
        if (camera.updated) {
            updateUniformBuffers();
        }
    }

    virtual void windowResized() {
        updateUniformBuffers();
    }
};

VULKAN_EXAMPLE_MAIN()