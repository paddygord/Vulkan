/*
* Vulkan Example - Using inline uniform blocks for passing data to shader stages at descriptor setup

* Note: Requires a device that supports the VK_EXT_inline_uniform_block extension
*
* Relevant code parts are marked with [POI]
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
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
#include "VulkanBuffer.hpp"
#include "VulkanModel.hpp"

#define ENABLE_VALIDATION false
#define OBJ_DIM 0.025f

float rnd() {
    return ((float)rand() / (RAND_MAX));
}

class VulkanExample : public VulkanExampleBase {
public:
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_UV,
    } };

    vkx::model::Model model;

    struct Object {
        struct Material {
            float roughness;
            float metallic;
            float r, g, b;
            float ambient;
        } material;
        vk::DescriptorSet descriptorSet;
        void setRandomMaterial() {
            material.r = rnd();
            material.g = rnd();
            material.b = rnd();
            material.ambient = 0.0025f;
            material.roughness = glm::clamp(rnd(), 0.005f, 1.0f);
            material.metallic = glm::clamp(rnd(), 0.005f, 1.0f);
        }
    };
    std::array<Object, 16> objects;

    struct {
        vks::Buffer scene;
    } uniformBuffers;

    struct UBOMatrices {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 view;
        glm::vec3 camPos;
    } uboMatrices;

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;
    vk::DescriptorSet descriptorSet;

    struct DescriptorSetLaysts {
        vk::DescriptorSetLayout scene;
        vk::DescriptorSetLayout object;
    } descriptorSetLayouts;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Inline uniform blocks";
        camera.type = Camera::CameraType::firstperson;
        camera.setPosition(glm::vec3(0.0f, 0.0f, -10.0f));
        camera.setRotation(glm::vec3(0.0, 0.0f, 0.0f));
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
        camera.movementSpeed = 4.0f;
        camera.rotationSpeed = 0.25f;
        settings.overlay = true;

        srand((unsigned int)time(0));

        /*
			[POI] Enable extensions required for inline uniform blocks
		*/
        enabledDeviceExtensions.push_back(VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME);
        enabledDeviceExtensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
        enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    ~VulkanExample() {
        vkDestroyPipeline(device, pipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.object, nullptr);

        model.destroy();

        uniformBuffers.scene.destroy();
    }

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = { { 0.15f, 0.15f, 0.15f, 1.0f } };
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

            // Render objects
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            uint32_t objcount = static_cast<uint32_t>(objects.size());
            for (uint32_t x = 0; x < objcount; x++) {
                /*
					[POI] Bind descriptor sets
					Set 0 = Scene matrices: 
					Set 1 = Object inline uniform block (In shader pbr.frag: layout (set = 1, binding = 0) uniform UniformInline ... )
				*/
                std::vector<vk::DescriptorSet> descriptorSets = { descriptorSet, objects[x].descriptorSet };
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, descriptorSets.data(), 0, nullptr);

                glm::vec3 pos = glm::vec3(sin(glm::radians(x * (360.0f / objcount))), cos(glm::radians(x * (360.0f / objcount))), 0.0f) * 3.5f;

                vkCmdPushConstants(drawCmdBuffers[i], pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::vec3), &pos);
                vkCmdDrawIndexed(drawCmdBuffers[i], model.indexCount, 1, 0, 0, 0);
            }
            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets() {
        model.loadFromFile(context, getAssetPath() + "models/geosphere.obj", vertexLayout, OBJ_DIM);

        // Setup random materials for every object in the scene
        for (uint32_t i = 0; i < objects.size(); i++) {
            objects[i].setRandomMaterial();
        }
    }

    void setupDescriptorSetLayout() {
        // Scene
        {
            std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
                { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0 },
            };
            vk::DescriptorSetLayoutCreateInfo descriptorLayoutCI{ setLayoutBindings };
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.scene));
        }

        // Objects
        {
            std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
                /*
					[POI] Setup inline uniform block for set 1 at binding 0 (see fragment shader)
					Descriptor count for an inline uniform block contains data sizes of the block (last parameter)
				*/
                { vk::DescriptorType::eINLINE_UNIFORM_BLOCK_EXT, vk::ShaderStageFlagBits::eFragment, 0, sizeof(Object::Material) },
            };
            vk::DescriptorSetLayoutCreateInfo descriptorLayoutCI{ setLayoutBindings };
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.object));
        }

        /*
			[POI] Pipeline layout
		*/
        std::vector<vk::DescriptorSetLayout> setLayouts = {
            descriptorSetLayouts.scene,  // Set 0 = Scene matrices
            descriptorSetLayouts.object  // Set 1 = Object inline uniform block
        };
        vk::PipelineLayoutCreateInfo pipelineLayoutCI{ setLayouts.data(), static_cast<uint32_t>(setLayouts.size()) };

        std::vector<vk::PushConstantRange> pushConstantRanges = {
            { vk::ShaderStageFlagBits::eVertex, sizeof(glm::vec3), 0 },
        };
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
    }

    void setupDescriptorSets() {
        // Pool
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 1 },
            /* [POI] Allocate inline uniform blocks */
            { vk::DescriptorType::eINLINE_UNIFORM_BLOCK_EXT, static_cast<uint32_t>(objects.size()) * sizeof(Object::Material) },
        };
        vk::DescriptorPoolCreateInfo descriptorPoolCI{ poolSizes, static_cast<uint32_t>(objects.size()) + 1 };

        /*
			[POI] New structure that has to be chained into the descriptor pool's createinfo if you want to allocate inline uniform blocks
		*/
        vk::DescriptorPoolInlineUniformBlockCreateInfoEXT descriptorPoolInlineUniformBlockCreateInfo{};
        descriptorPoolInlineUniformBlockCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO_EXT;
        descriptorPoolInlineUniformBlockCreateInfo.maxInlineUniformBlockBindings = static_cast<uint32_t>(objects.size());
        descriptorPoolCI.pNext = &descriptorPoolInlineUniformBlockCreateInfo;

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

        // Sets

        // Scene
        vk::DescriptorSetAllocateInfo descriptorAllocateInfo{ descriptorPool, &descriptorSetLayouts.scene, 1 };
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorAllocateInfo, &descriptorSet));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            { descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformBuffers.scene.descriptor },
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

        // Objects
        for (auto& object : objects) {
            vk::DescriptorSetAllocateInfo descriptorAllocateInfo{ descriptorPool, &descriptorSetLayouts.object, 1 };
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorAllocateInfo, &object.descriptorSet));

            /*
				[POI] New structure that defines size and data of the inline uniform block needs to be chained into the write descriptor set
				We will be using this inline uniform block to pass per-object material information to the fragment shader
			*/
            vk::WriteDescriptorSetInlineUniformBlockEXT writeDescriptorSetInlineUniformBlock{};
            writeDescriptorSetInlineUniformBlock.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK_EXT;
            writeDescriptorSetInlineUniformBlock.dataSize = sizeof(Object::Material);
            // Uniform data for the inline block
            writeDescriptorSetInlineUniformBlock.pData = &object.material;

            /*
				[POI] Setup the inline uniform block
			*/
            vk::WriteDescriptorSet writeDescriptorSet{};
            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.descriptorType = vk::DescriptorType::eINLINE_UNIFORM_BLOCK_EXT;
            writeDescriptorSet.dstSet = object.descriptorSet;
            writeDescriptorSet.dstBinding = 0;
            // Descriptor count for an inline uniform block contains data sizes of the block(last parameter)
            writeDescriptorSet.descriptorCount = sizeof(Object::Material);
            // Chain inline uniform block structure
            writeDescriptorSet.pNext = &writeDescriptorSetInlineUniformBlock;

            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        }
    }

    void preparePipelines() {
        // Vertex bindings an attributes
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },
            { 0, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },
        };
        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{ VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };
        vk::PipelineRasterizationStateCreateInfo rasterizationStateCI{ VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE };
        vk::PipelineColorBlendAttachmentState blendAttachmentState{ 0xf, VK_FALSE };
        vk::PipelineColorBlendStateCreateInfo colorBlendStateCI{ 1, &blendAttachmentState };
        vk::PipelineDepthStencilStateCreateInfo depthStencilStateCI{ VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };
        vk::PipelineViewportStateCreateInfo viewportStateCI{ 1, 1 };
        vk::PipelineMultisampleStateCreateInfo multisampleStateCI{ VK_SAMPLE_COUNT_1_BIT };
        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicStateCI{ dynamicStateEnables };
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfoCI{ pipelineLayout, renderPass };
        pipelineCreateInfoCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCreateInfoCI.pRasterizationState = &rasterizationStateCI;
        pipelineCreateInfoCI.pColorBlendState = &colorBlendStateCI;
        pipelineCreateInfoCI.pMultisampleState = &multisampleStateCI;
        pipelineCreateInfoCI.pViewportState = &viewportStateCI;
        pipelineCreateInfoCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCreateInfoCI.pDynamicState = &dynamicStateCI;
        pipelineCreateInfoCI.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfoCI.pStages = shaderStages.data();
        pipelineCreateInfoCI.pVertexInputState = &vertexInputState;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/inlineuniformblocks/pbr.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/inlineuniformblocks/pbr.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfoCI, nullptr, &pipeline));
    }

    void prepareUniformBuffers() {
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.scene,
                                                   sizeof(uboMatrices)));
        VK_CHECK_RESULT(uniformBuffers.scene.map());
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboMatrices.projection = camera.matrices.perspective;
        uboMatrices.view = camera.matrices.view;
        uboMatrices.model = glm::mat4(1.0f);
        uboMatrices.camPos = camera.position * -1.0f;
        memcpy(uniformBuffers.scene.mapped, &uboMatrices, sizeof(uboMatrices));
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
        setupDescriptorSets();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        if (camera.updated)
            updateUniformBuffers();
    }

    /*
		[POI] Update descriptor sets at runtime
	*/
    void updateMaterials() {
        // Setup random materials for every object in the scene
        for (uint32_t i = 0; i < objects.size(); i++) {
            objects[i].setRandomMaterial();
        }

        for (auto& object : objects) {
            /*
				[POI] New structure that defines size and data of the inline uniform block needs to be chained into the write descriptor set
				We will be using this inline uniform block to pass per-object material information to the fragment shader
			*/
            vk::WriteDescriptorSetInlineUniformBlockEXT writeDescriptorSetInlineUniformBlock{};
            writeDescriptorSetInlineUniformBlock.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK_EXT;
            writeDescriptorSetInlineUniformBlock.dataSize = sizeof(Object::Material);
            // Uniform data for the inline block
            writeDescriptorSetInlineUniformBlock.pData = &object.material;

            /*
				[POI] Update the object's inline uniform block
			*/
            vk::WriteDescriptorSet writeDescriptorSet{};
            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.descriptorType = vk::DescriptorType::eINLINE_UNIFORM_BLOCK_EXT;
            writeDescriptorSet.dstSet = object.descriptorSet;
            writeDescriptorSet.dstBinding = 0;
            writeDescriptorSet.descriptorCount = sizeof(Object::Material);
            writeDescriptorSet.pNext = &writeDescriptorSetInlineUniformBlock;

            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        }
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->button("Randomize")) {
            updateMaterials();
        }
    }
};

VULKAN_EXAMPLE_MAIN()