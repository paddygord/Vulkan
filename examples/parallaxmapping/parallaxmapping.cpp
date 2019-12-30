/*
* Vulkan Example - Parallax Mapping
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
#include <glm/gtc/matrix_inverse.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    struct {
        vkx::texture::Texture2D colorMap;
        // Normals and height are combined into one texture (height = alpha channel)
        vkx::texture::Texture2D normalHeightMap;
    } textures;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_TANGENT,
        vkx::vertex::VERTEX_COMPONENT_BITANGENT,
    } };

    struct {
        vkx::model::Model quad;
    } models;

    struct {
        vks::Buffer vertexShader;
        vks::Buffer fragmentShader;
    } uniformBuffers;

    struct {
        struct {
            glm::mat4 projection;
            glm::mat4 view;
            glm::mat4 model;
            glm::vec4 lightPos = glm::vec4(0.0f, -2.0f, 0.0f, 1.0f);
            glm::vec4 cameraPos;
        } vertexShader;

        struct {
            float heightScale = 0.1f;
            // Basic parallax mapping needs a bias to look any good (and is hard to tweak)
            float parallaxBias = -0.02f;
            // Number of layers for steep parallax and parallax occlusion (more layer = better result for less performance)
            float numLayers = 48.0f;
            // (Parallax) mapping mode to use
            int32_t mappingMode = 4;
        } fragmentShader;

    } ubos;

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;
    vk::DescriptorSetLayout descriptorSetLayout;
    vk::DescriptorSet descriptorSet;

    const std::vector<std::string> mappingModes = {
        "Color only", "Normal mapping", "Parallax mapping", "Steep parallax mapping", "Parallax occlusion mapping",
    };

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Parallax Mapping";
        timerSpeed *= 0.5f;
        camera.type = Camera::CameraType::firstperson;
        camera.setPosition(glm::vec3(0.0f, 1.25f, 1.5f));
        camera.setRotation(glm::vec3(-45.0f, 180.0f, 0.0f));
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
        settings.overlay = true;
    }

    ~VulkanExample() {
        vkDestroyPipeline(device, pipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        models.quad.destroy();

        uniformBuffers.vertexShader.destroy();
        uniformBuffers.fragmentShader.destroy();

        textures.colorMap.destroy();
        textures.normalHeightMap.destroy();
    }

    void loadAssets() {
        models.quad.loadFromFile(context, getAssetPath() + "models/plane_z.obj", vertexLayout, 0.1f);

        // Textures
        textures.normalHeightMap.loadFromFile(context, getAssetPath() + "textures/rocks_normal_height_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
        if (vulkanDevice->features.textureCompressionBC) {
            textures.colorMap.loadFromFile(context, getAssetPath() + "textures/rocks_color_bc3_unorm.ktx", vk::Format::eBC3_UNORM_BLOCK);
        } else if (vulkanDevice->features.textureCompressionASTC_LDR) {
            textures.colorMap.loadFromFile(context, getAssetPath() + "textures/rocks_color_astc_8x8_unorm.ktx", vk::Format::eASTC_8x8_UNORM_BLOCK);
        } else if (vulkanDevice->features.textureCompressionETC2) {
            textures.colorMap.loadFromFile(context, getAssetPath() + "textures/rocks_color_etc2_unorm.ktx", vk::Format::eETC2_R8G8B8_UNORM_BLOCK);
        } else {
            vks::tools::exitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
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

            vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            vk::Rect2D scissor{ width, height, 0, 0 };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

            vk::DeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.quad.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.quad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdDrawIndexed(drawCmdBuffers[i], models.quad.indexCount, 1, 0, 0, 1);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void setupDescriptorPool() {
        // Example uses two ubos and two image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 2 }, { vk::DescriptorType::eCombinedImageSampler, 2 } };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = { poolSizes, 2 };

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 },           // Binding 0: Vertex shader uniform buffer
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1 },  // Binding 1: Fragment shader color map image sampler
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2 },  // Binding 2: Fragment combined normal and heightmap
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 3 },         // Binding 3: Fragment shader uniform buffer
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout = { setLayoutBindings };

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            { descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.vertexShader.descriptor },      // Binding 0: Vertex shader uniform buffer
            { descriptorSet, vk::DescriptorType::eCombinedImageSampler, 1, &textures.colorMap.descriptor },         // Binding 1: Fragment shader image sampler
            { descriptorSet, vk::DescriptorType::eCombinedImageSampler, 2, &textures.normalHeightMap.descriptor },  // Binding 2: Combined normal and heightmap
            { descriptorSet, vk::DescriptorType::eUniformBuffer, 3, &uniformBuffers.fragmentShader.descriptor },    // Binding 3: Fragment shader uniform buffer
        };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };

        vk::PipelineRasterizationStateCreateInfo rasterizationState = { VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE };

        vk::PipelineColorBlendAttachmentState blendAttachmentState = { 0xf, VK_FALSE };

        vk::PipelineColorBlendStateCreateInfo colorBlendState = { 1, &blendAttachmentState };

        vk::PipelineDepthStencilStateCreateInfo depthStencilState = { VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = { VK_SAMPLE_COUNT_1_BIT };

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState = { dynamicStateEnables };

        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = { pipelineLayout, renderPass };

        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        // Vertex bindings an attributes
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32sFloat, 0 },                   // Location 0: Position
            { VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32sFloat, sizeof(float) * 3 },      // Location 1: Texture coordinates
            { VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 5 },   // Location 2: Normal
            { VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32B32sFloat, sizeof(float) * 8 },   // Location 3: Tangent
            { VERTEX_BUFFER_BIND_ID, 4, vk::Format::eR32G32B32sFloat, sizeof(float) * 11 },  // Location 4: Bitangent
        };
        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        pipelineCreateInfo.pVertexInputState = &vertexInputState;

        // Parallax mapping modes pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/parallaxmapping/parallax.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/parallaxmapping/parallax.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
    }

    void prepareUniformBuffers() {
        // Vertex shader uniform buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.vertexShader,
                                                   sizeof(ubos.vertexShader)));

        // Fragment shader uniform buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.fragmentShader,
                                                   sizeof(ubos.fragmentShader)));

        // Map persistent
        VK_CHECK_RESULT(uniformBuffers.vertexShader.map());
        VK_CHECK_RESULT(uniformBuffers.fragmentShader.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader
        ubos.vertexShader.projection = camera.matrices.perspective;
        ubos.vertexShader.view = camera.matrices.view;
        ubos.vertexShader.model = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        ubos.vertexShader.model = glm::rotate(ubos.vertexShader.model, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        if (!paused) {
            ubos.vertexShader.lightPos.x = sin(glm::radians(timer * 360.0f)) * 1.5f;
            ubos.vertexShader.lightPos.z = cos(glm::radians(timer * 360.0f)) * 1.5f;
        }

        ubos.vertexShader.cameraPos = glm::vec4(camera.position, -1.0f) * -1.0f;

        memcpy(uniformBuffers.vertexShader.mapped, &ubos.vertexShader, sizeof(ubos.vertexShader));

        // Fragment shader
        memcpy(uniformBuffers.fragmentShader.mapped, &ubos.fragmentShader, sizeof(ubos.fragmentShader));
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
        if (!paused) {
            updateUniformBuffers();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Settings")) {
            if (overlay->comboBox("Mode", &ubos.fragmentShader.mappingMode, mappingModes)) {
                updateUniformBuffers();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()