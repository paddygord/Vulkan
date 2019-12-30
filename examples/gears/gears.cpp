/*
* Vulkan Example - Animated gears using multiple uniform buffers
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
#include "vulkangear.h"
#include "vulkanexamplebase.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vk::Pipeline solid;
    } pipelines;

    std::vector<VulkanGear*> gears;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        zoom = -16.0f;
        rotation = glm::vec3(-23.75f, 41.25f, 21.0f);
        timerSpeed *= 0.25f;
        title = "Rotating gears";
        settings.overlay = true;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        vkDestroyPipeline(device, pipelines.solid, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        for (auto& gear : gears) {
            delete (gear);
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
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            vk::Rect2D scissor{ width, height, 0, 0 };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.solid);

            for (auto& gear : gears) {
                gear->draw(drawCmdBuffers[i], pipelineLayout);
            }

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void prepareVertices() {
        // Gear definitions
        std::vector<float> innerRadiuses = { 1.0f, 0.5f, 1.3f };
        std::vector<float> outerRadiuses = { 4.0f, 2.0f, 2.0f };
        std::vector<float> widths = { 1.0f, 2.0f, 0.5f };
        std::vector<int32_t> toothCount = { 20, 10, 10 };
        std::vector<float> toothDepth = { 0.7f, 0.7f, 0.7f };
        std::vector<glm::vec3> colors = { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.2f), glm::vec3(0.0f, 0.0f, 1.0f) };
        std::vector<glm::vec3> positions = { glm::vec3(-3.0, 0.0, 0.0), glm::vec3(3.1, 0.0, 0.0), glm::vec3(-3.1, -6.2, 0.0) };
        std::vector<float> rotationSpeeds = { 1.0f, -2.0f, -2.0f };
        std::vector<float> rotationOffsets = { 0.0f, -9.0f, -30.0f };

        gears.resize(positions.size());
        for (int32_t i = 0; i < gears.size(); ++i) {
            GearInfo gearInfo = {};
            gearInfo.innerRadius = innerRadiuses[i];
            gearInfo.outerRadius = outerRadiuses[i];
            gearInfo.width = widths[i];
            gearInfo.numTeeth = toothCount[i];
            gearInfo.toothDepth = toothDepth[i];
            gearInfo.color = colors[i];
            gearInfo.pos = positions[i];
            gearInfo.rotSpeed = rotationSpeeds[i];
            gearInfo.rotOffset = rotationOffsets[i];

            gears[i] = new VulkanGear(vulkanDevice);
            gears[i]->generate(&gearInfo, queue);
        }

        // Binding and attribute descriptions are shared across all gears
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] = vk::vertexInputBindingDescription{VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex};

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(3);
        // Location 0 : Position
        vertices.attributeDescriptions[0] = vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32sFloat, 0};
        // Location 1 : Normal
        vertices.attributeDescriptions[1] =
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3};
        // Location 2 : Color
        vertices.attributeDescriptions[2] =
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 6};

        vertices.inputState;
        vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // One UBO for each gear
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 3 },
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = vk::descriptorPoolCreateInfo{static_cast<uint32_t>(poolSizes.size()), poolSizes.data(},
                                                                                                      // Three descriptor sets (for each gear)
                                                                                                      3);

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Vertex shader uniform buffer
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0}
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size())};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSets() {
        for (auto& gear : gears) {
            gear->setupDescriptorSet(descriptorPool, descriptorSetLayout);
        }
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

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0};

        // Solid rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/gears/gears.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/gears/gears.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid));
    }

    void updateUniformBuffers() {
        glm::mat4 perspective = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
        for (auto& gear : gears) {
            gear->updateUniformBuffer(perspective, rotation, zoom, timer * 360.0f);
        }
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
        prepareVertices();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSets();
        updateUniformBuffers();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        vkDeviceWaitIdle(device);
        draw();
        vkDeviceWaitIdle(device);
        if (!paused) {
            updateUniformBuffers();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }
};

VULKAN_EXAMPLE_MAIN()