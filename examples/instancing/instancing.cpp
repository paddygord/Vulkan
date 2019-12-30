/*
* Vulkan Example - Instanced mesh rendering, uses a separate vertex buffer for instanced data
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <vector>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1
#define ENABLE_VALIDATION false
#if defined(__ANDROID__)
#define INSTANCE_COUNT 4096
#else
#define INSTANCE_COUNT 8192
#endif

class VulkanExample : public VulkanExampleBase {
public:
    struct {
        vkx::texture::Texture2DArray rocks;
        vkx::texture::Texture2D planet;
    } textures;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
    } };

    struct {
        vkx::model::Model rock;
        vkx::model::Model planet;
    } models;

    // Per-instance data block
    struct InstanceData {
        glm::vec3 pos;
        glm::vec3 rot;
        float scale;
        uint32_t texIndex;
    };
    // Contains the instanced data
    struct InstanceBuffer {
        vk::Buffer buffer = VK_NULL_HANDLE;
        vk::DeviceMemory memory = VK_NULL_HANDLE;
        size_t size = 0;
        vk::DescriptorBufferInfo descriptor;
    } instanceBuffer;

    struct UBOVS {
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec4 lightPos = glm::vec4(0.0f, -5.0f, 0.0f, 1.0f);
        float locSpeed = 0.0f;
        float globSpeed = 0.0f;
    } uboVS;

    struct {
        vks::Buffer scene;
    } uniformBuffers;

    vk::PipelineLayout pipelineLayout;
    struct {
        vk::Pipeline instancedRocks;
        vk::Pipeline planet;
        vk::Pipeline starfield;
    } pipelines;

    vk::DescriptorSetLayout descriptorSetLayout;
    struct {
        vk::DescriptorSet instancedRocks;
        vk::DescriptorSet planet;
    } descriptorSets;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Instanced mesh rendering";
        zoom = -18.5f;
        rotation = { -17.2f, -4.7f, 0.0f };
        cameraPos = { 5.5f, -1.85f, 0.0f };
        rotationSpeed = 0.25f;
        settings.overlay = true;
    }

    ~VulkanExample() {
        vkDestroyPipeline(device, pipelines.instancedRocks, nullptr);
        vkDestroyPipeline(device, pipelines.planet, nullptr);
        vkDestroyPipeline(device, pipelines.starfield, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        vkDestroyBuffer(device, instanceBuffer.buffer, nullptr);
        vkFreeMemory(device, instanceBuffer.memory, nullptr);
        models.rock.destroy();
        models.planet.destroy();
        textures.rocks.destroy();
        textures.planet.destroy();
        uniformBuffers.scene.destroy();
    }

    // Enable physical device features required for this example
    virtual void getEnabledFeatures() {
        // Enable anisotropic filtering if supported
        if (deviceFeatures.samplerAnisotropy) {
            enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
        // Enable texture compression
        if (deviceFeatures.textureCompressionBC) {
            enabledFeatures.textureCompressionBC = VK_TRUE;
        } else if (deviceFeatures.textureCompressionASTC_LDR) {
            enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
        } else if (deviceFeatures.textureCompressionETC2) {
            enabledFeatures.textureCompressionETC2 = VK_TRUE;
        }
    };

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
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

            vk::DeviceSize offsets[1] = { 0 };

            // Star field
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.planet, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starfield);
            vkCmdDraw(drawCmdBuffers[i], 4, 1, 0, 0);

            // Planet
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.planet, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.planet);
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.planet.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.planet.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(drawCmdBuffers[i], models.planet.indexCount, 1, 0, 0, 0);

            // Instanced rocks
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.instancedRocks, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.instancedRocks);
            // Binding point 0 : Mesh vertex buffer
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.rock.vertices.buffer, offsets);
            // Binding point 1 : Instance data buffer
            vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &instanceBuffer.buffer, offsets);

            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.rock.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            // Render instances
            vkCmdDrawIndexed(drawCmdBuffers[i], models.rock.indexCount, INSTANCE_COUNT, 0, 0, 0);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        models.rock.loadFromFile(context, getAssetPath() + "models/rock01.dae", vertexLayout, 0.1f);
        models.planet.loadFromFile(context, getAssetPath() + "models/sphere.obj", vertexLayout, 0.2f);

        // Textures
        std::string texFormatSuffix;
        vk::Format texFormat;
        // Get supported compressed texture format
        if (vulkanDevice->features.textureCompressionBC) {
            texFormatSuffix = "_bc3_unorm";
            texFormat = vk::Format::eBC3_UNORM_BLOCK;
        } else if (vulkanDevice->features.textureCompressionASTC_LDR) {
            texFormatSuffix = "_astc_8x8_unorm";
            texFormat = vk::Format::eASTC_8x8_UNORM_BLOCK;
        } else if (vulkanDevice->features.textureCompressionETC2) {
            texFormatSuffix = "_etc2_unorm";
            texFormat = vk::Format::eETC2_R8G8B8_UNORM_BLOCK;
        } else {
            vks::tools::exitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
        }

        textures.rocks.loadFromFile(context, getAssetPath() + "textures/texturearray_rocks" + texFormatSuffix + ".ktx", texFormat);
        textures.planet.loadFromFile(context, getAssetPath() + "textures/lavaplanet" + texFormatSuffix + ".ktx", texFormat);
    }

    void setupDescriptorPool() {
        // Example uses one ubo
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 2 },
            { vk::DescriptorType::eCombinedImageSampler, 2 },
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = vk::descriptorPoolCreateInfo{poolSizes.size(), poolSizes.data(), 2};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Vertex shader uniform buffer
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0},
            // Binding 1 : Fragment shader combined sampler
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1},
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), setLayoutBindings.size()};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo descripotrSetAllocInfo;
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets;

        descripotrSetAllocInfo{ descriptorPool, &descriptorSetLayout, 1 };

        // Instanced rocks
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descripotrSetAllocInfo, &descriptorSets.instancedRocks));
        writeDescriptorSets = {
            { descriptorSets.instancedRocks, vk::DescriptorType::eUniformBuffer, 0,
              &uniformBuffers.scene.descriptor },  // Binding 0 : Vertex shader uniform buffer
            { descriptorSets.instancedRocks, vk::DescriptorType::eCombinedImageSampler, 1, &textures.rocks.descriptor }  // Binding 1 : Color map
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Planet
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descripotrSetAllocInfo, &descriptorSets.planet));
        writeDescriptorSets = {
            { descriptorSets.planet, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.scene.descriptor },   // Binding 0 : Vertex shader uniform buffer
            { descriptorSets.planet, vk::DescriptorType::eCombinedImageSampler, 1, &textures.planet.descriptor }  // Binding 1 : Color map
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

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), dynamicStateEnables.size(), 0};

        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::pipelineCreateInfo{pipelineLayout, renderPass, 0};

        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();

        // This example uses two different input states, one for the instanced part and one for non-instanced rendering
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

        // Vertex input bindings
        // The instancing pipeline uses a vertex input state with two bindings
        bindingDescriptions = { // Binding point 0: Mesh vertex layout description at per-vertex rate
                                { VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex },
                                // Binding point 1: Instanced data at per-instance rate
                                { INSTANCE_BUFFER_BIND_ID, sizeof(InstanceData), vk::VertexInputRate::eINSTANCE }
        };

        // Vertex attribute bindings
        // Note that the shader declaration for per-vertex and per-instance attributes is the same, the different input rates are only stored in the bindings:
        // instanced.vert:
        //	layout (location = 0) in vec3 inPos;		Per-Vertex
        //	...
        //	layout (location = 4) in vec3 instancePos;	Per-Instance
        attributeDescriptions = {
            // Per-vertex attributees
            // These are advanced for each vertex fetched by the vertex shader
            { VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 1: Normal
            { VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32sFloat, sizeof(float) * 6 },     // Location 2: Texture coordinates
            { VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32B32sFloat, sizeof(float) * 8 },  // Location 3: Color
            // Per-Instance attributes
            // These are fetched for each instance rendered
            { INSTANCE_BUFFER_BIND_ID, 4, vk::Format::eR32G32B32sFloat, 0 },                  // Location 4: Position
            { INSTANCE_BUFFER_BIND_ID, 5, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 5: Rotation
            { INSTANCE_BUFFER_BIND_ID, 6, vk::Format::eR32sFloat, sizeof(float) * 6 },        // Location 6: Scale
            { INSTANCE_BUFFER_BIND_ID, 7, vk::Format::eR32_SINT, sizeof(float) * 7 },         // Location 7: Texture array layer index
        };
        inputState.pVertexBindingDescriptions = bindingDescriptions.data();
        inputState.pVertexAttributeDescriptions = attributeDescriptions.data();

        pipelineCreateInfo.pVertexInputState = &inputState;

        // Instancing pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing/instancing.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing/instancing.frag.spv", vk::ShaderStageFlagBits::eFragment);
        // Use all input bindings and attribute descriptions
        inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.instancedRocks));

        // Planet rendering pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing/planet.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing/planet.frag.spv", vk::ShaderStageFlagBits::eFragment);
        // Only use the non-instanced input bindings and attribute descriptions
        inputState.vertexBindingDescriptionCount = 1;
        inputState.vertexAttributeDescriptionCount = 4;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.planet));

        // Star field pipeline
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        depthStencilState.depthWriteEnable = VK_FALSE;
        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing/starfield.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing/starfield.frag.spv", vk::ShaderStageFlagBits::eFragment);
        // Vertices are generated in the vertex shader
        inputState.vertexBindingDescriptionCount = 0;
        inputState.vertexAttributeDescriptionCount = 0;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.starfield));
    }

    void prepareInstanceData() {
        std::vector<InstanceData> instanceData;
        instanceData.resize(INSTANCE_COUNT);

        std::default_random_engine rndGenerator(benchmark.active ? 0 : (unsigned)time(nullptr));
        std::uniform_real_distribution<float> uniformDist(0.0, 1.0);
        std::uniform_int_distribution<uint32_t> rndTextureIndex(0, textures.rocks.layerCount);

        // Distribute rocks randomly on two different rings
        for (auto i = 0; i < INSTANCE_COUNT / 2; i++) {
            glm::vec2 ring0{ 7.0f, 11.0f };
            glm::vec2 ring1{ 14.0f, 18.0f };

            float rho, theta;

            // Inner ring
            rho = sqrt((pow(ring0[1], 2.0f) - pow(ring0[0], 2.0f)) * uniformDist(rndGenerator) + pow(ring0[0], 2.0f));
            theta = 2.0 * M_PI * uniformDist(rndGenerator);
            instanceData[i].pos = glm::vec3(rho * cos(theta), uniformDist(rndGenerator) * 0.5f - 0.25f, rho * sin(theta));
            instanceData[i].rot = glm::vec3(M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator));
            instanceData[i].scale = 1.5f + uniformDist(rndGenerator) - uniformDist(rndGenerator);
            instanceData[i].texIndex = rndTextureIndex(rndGenerator);
            instanceData[i].scale *= 0.75f;

            // Outer ring
            rho = sqrt((pow(ring1[1], 2.0f) - pow(ring1[0], 2.0f)) * uniformDist(rndGenerator) + pow(ring1[0], 2.0f));
            theta = 2.0 * M_PI * uniformDist(rndGenerator);
            instanceData[i + INSTANCE_COUNT / 2].pos = glm::vec3(rho * cos(theta), uniformDist(rndGenerator) * 0.5f - 0.25f, rho * sin(theta));
            instanceData[i + INSTANCE_COUNT / 2].rot =
                glm::vec3(M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator));
            instanceData[i + INSTANCE_COUNT / 2].scale = 1.5f + uniformDist(rndGenerator) - uniformDist(rndGenerator);
            instanceData[i + INSTANCE_COUNT / 2].texIndex = rndTextureIndex(rndGenerator);
            instanceData[i + INSTANCE_COUNT / 2].scale *= 0.75f;
        }

        instanceBuffer.size = instanceData.size() * sizeof(InstanceData);

        // Staging
        // Instanced data is static, copy to device local memory
        // This results in better performance

        struct {
            vk::DeviceMemory memory;
            vk::Buffer buffer;
        } stagingBuffer;

        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                   instanceBuffer.size, &stagingBuffer.buffer, &stagingBuffer.memory, instanceData.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                   instanceBuffer.size, &instanceBuffer.buffer, &instanceBuffer.memory));

        // Copy to staging buffer
        vk::CommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        vk::BufferCopy copyRegion = {};
        copyRegion.size = instanceBuffer.size;
        vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, instanceBuffer.buffer, 1, &copyRegion);

        VulkanExampleBase::flushCommandBuffer(copyCmd, queue, true);

        instanceBuffer.descriptor.range = instanceBuffer.size;
        instanceBuffer.descriptor.buffer = instanceBuffer.buffer;
        instanceBuffer.descriptor.offset = 0;

        // Destroy staging resources
        vkDestroyBuffer(device, stagingBuffer.buffer, nullptr);
        vkFreeMemory(device, stagingBuffer.memory, nullptr);
    }

    void prepareUniformBuffers() {
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.scene,
                                                   sizeof(uboVS)));

        // Map persistent
        VK_CHECK_RESULT(uniformBuffers.scene.map());

        updateUniformBuffer(true);
    }

    void updateUniformBuffer(bool viewChanged) {
        if (viewChanged) {
            uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
            uboVS.view = glm::translate(glm::mat4(1.0f), cameraPos + glm::vec3(0.0f, 0.0f, zoom));
            uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        }

        if (!paused) {
            uboVS.locSpeed += frameTimer * 0.35f;
            uboVS.globSpeed += frameTimer * 0.01f;
        }

        memcpy(uniformBuffers.scene.mapped, &uboVS, sizeof(uboVS));
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

        prepareInstanceData();
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
        if (!paused) {
            updateUniformBuffer(false);
        }
    }

    virtual void viewChanged() {
        updateUniformBuffer(true);
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Statistics")) {
            overlay->text("Instances: %d", INSTANCE_COUNT);
        }
    }
};

VULKAN_EXAMPLE_MAIN()