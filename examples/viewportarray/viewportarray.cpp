/*
* Vulkan Example - Viewport array with single pass rendering using geometry shaders
*
* Copyright (C) 2017 by Sascha Willems - www.saschawillems.de
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

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    // Vertex layout for the models
    vks::VertexLayout vertexLayout = vks::VertexLayout({
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
    });

    vkx::model::Model scene;

    struct UBOGS {
        glm::mat4 projection[2];
        glm::mat4 modelview[2];
        glm::vec4 lightPos = glm::vec4(-2.5f, -3.5f, 0.0f, 1.0f);
    } uboGS;

    vks::Buffer uniformBufferGS;

    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    // Camera and view properties
    float eyeSeparation = 0.08f;
    const float focalLength = 0.5f;
    const float fov = 90.0f;
    const float zNear = 0.1f;
    const float zFar = 256.0f;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Viewport arrays";
        camera.type = Camera::CameraType::firstperson;
        camera.setRotation(glm::vec3(0.0f, 90.0f, 0.0f));
        camera.setTranslation(glm::vec3(7.0f, 3.2f, 0.0f));
        camera.movementSpeed = 5.0f;
        settings.overlay = true;
    }

    ~VulkanExample() {
        vkDestroyPipeline(device, pipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        scene.destroy();

        uniformBufferGS.destroy();
    }

    // Enable physical device features required for this example
    virtual void getEnabledFeatures() {
        // Geometry shader support is required for this example
        if (deviceFeatures.geometryShader) {
            enabledFeatures.geometryShader = VK_TRUE;
        } else {
            vks::tools::exitFatal("Selected GPU does not support geometry shaders!", VK_ERROR_FEATURE_NOT_PRESENT);
        }
        // Multiple viewports must be supported
        if (deviceFeatures.multiViewport) {
            enabledFeatures.multiViewport = VK_TRUE;
        } else {
            vks::tools::exitFatal("Selected GPU does not support multi viewports!", VK_ERROR_FEATURE_NOT_PRESENT);
        }
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

            vk::Viewport viewports[2];
            // Left
            viewports[0] = { 0, 0, (float)width / 2.0f, (float)height, 0.0, 1.0f };
            // Right
            viewports[1] = { (float)width / 2.0f, 0, (float)width / 2.0f, (float)height, 0.0, 1.0f };

            vkCmdSetViewport(drawCmdBuffers[i], 0, 2, viewports);

            vk::Rect2D scissorRects[2] = {
                { width / 2, height, 0, 0 },
                { width / 2, height, width / 2, 0 },
            };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 2, scissorRects);

            vkCmdSetLineWidth(drawCmdBuffers[i], 1.0f);

            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            vk::DeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &scene.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdDrawIndexed(drawCmdBuffers[i], scene.indexCount, 1, 0, 0, 0);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        scene.loadFromFile(context, getAssetPath() + "models/sampleroom.dae", vertexLayout, 0.25f);
    }

    void setupDescriptorPool() {
        // Example uses two ubos
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 1 },
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = { static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 1 };

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eGEOMETRY, 0 }  // Binding 1: Geometry shader ubo
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout = { setLayoutBindings };

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = { &descriptorSetLayout, 1 };

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            { descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformBufferGS.descriptor },  // Binding 0 :Geometry shader ubo
        };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };

        vk::PipelineRasterizationStateCreateInfo rasterizationState = { VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE };

        vk::PipelineColorBlendAttachmentState blendAttachmentState = { 0xf, VK_FALSE };

        vk::PipelineColorBlendStateCreateInfo colorBlendState = { 1, &blendAttachmentState };

        vk::PipelineDepthStencilStateCreateInfo depthStencilState = { VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };

        // We use two viewports
        vk::PipelineViewportStateCreateInfo viewportState = { 2, 2, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = { VK_SAMPLE_COUNT_1_BIT };

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };
        vk::PipelineDynamicStateCreateInfo dynamicState = { dynamicStateEnables };

        // Tessellation pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 3> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = { pipelineLayout, renderPass };

        // Vertex bindings an attributes
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { 0, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 1: Normals
            { 0, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 6 },  // Location 2: Color

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
        pipelineCreateInfo.renderPass = renderPass;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/viewportarray/scene.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/viewportarray/scene.frag.spv", vk::ShaderStageFlagBits::eFragment);
        // A geometry shader is used to output geometry to multiple viewports in one single pass
        // See the "invoctations" decorator of the layout input in the shader
        shaderStages[2] = loadShader(getAssetPath() + "shaders/viewportarray/multiview.geom.spv", vk::ShaderStageFlagBits::eGEOMETRY);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Geometry shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBufferGS,
                                                   sizeof(uboGS)));

        // Map persistent
        VK_CHECK_RESULT(uniformBufferGS.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Geometry shader matrices for the two viewports
        // See http://paulbourke.net/stereographics/stereorender/

        // Calculate some variables
        float aspectRatio = (float)(width * 0.5f) / (float)height;
        float wd2 = zNear * tan(glm::radians(fov / 2.0f));
        float ndfl = zNear / focalLength;
        float left, right;
        float top = wd2;
        float bottom = -wd2;

        glm::vec3 camFront;
        camFront.x = -cos(glm::radians(rotation.x)) * sin(glm::radians(rotation.y));
        camFront.y = sin(glm::radians(rotation.x));
        camFront.z = cos(glm::radians(rotation.x)) * cos(glm::radians(rotation.y));
        camFront = glm::normalize(camFront);
        glm::vec3 camRight = glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f)));

        glm::mat4 rotM = glm::mat4(1.0f);
        glm::mat4 transM;

        rotM = glm::rotate(rotM, glm::radians(camera.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(camera.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(camera.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        // Left eye
        left = -aspectRatio * wd2 + 0.5f * eyeSeparation * ndfl;
        right = aspectRatio * wd2 + 0.5f * eyeSeparation * ndfl;

        transM = glm::translate(glm::mat4(1.0f), camera.position - camRight * (eyeSeparation / 2.0f));

        uboGS.projection[0] = glm::frustum(left, right, bottom, top, zNear, zFar);
        uboGS.modelview[0] = rotM * transM;

        // Right eye
        left = -aspectRatio * wd2 - 0.5f * eyeSeparation * ndfl;
        right = aspectRatio * wd2 - 0.5f * eyeSeparation * ndfl;

        transM = glm::translate(glm::mat4(1.0f), camera.position + camRight * (eyeSeparation / 2.0f));

        uboGS.projection[1] = glm::frustum(left, right, bottom, top, zNear, zFar);
        uboGS.modelview[1] = rotM * transM;

        memcpy(uniformBufferGS.mapped, &uboGS, sizeof(uboGS));
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
            if (overlay->sliderFloat("Eye separation", &eyeSeparation, -1.0f, 1.0f)) {
                updateUniformBuffers();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()