/*
* Vulkan Example - Different command buffer update strategies
*
* While for many basic example workloads command buffers are prebuilt and just reused,
* in a real-life setting command buffers are usually recreated all the time
* This sample will demonstrate different command buffer update scenarios
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vkx/vulkanExampleBase.hpp>

#include <vkx/model.hpp>

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase {
public:
    enum RenderMode
    {
        SINGLE_CB_RECREATE = 0,
        MULTIPLE_CB_STATIC = 1,
    };
    RenderMode renderMode;

    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
    } };

    struct {
        vks::Model scene;
    } models;

    struct ShaderValues {
        glm::mat4 projection;
        glm::mat4 model;
    } shaderValues;

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;
    vk::DescriptorSetLayout descriptorSetLayout;

    // Single command buffer scenario
    struct SingleCB {
        vk::Fence waitFence;
        vk::Semaphore renderCompleteSemaphore;
        vk::Semaphore presentCompleteSemaphore;
        vk::CommandPool commandPool;
        vk::CommandBuffer commandBuffer;
        vk::DescriptorSet descriptorSet;
        vks::Buffer uniformBuffer;
        void cleanup(VkDevice device) {
            vkDestroyFence(device, waitFence, nullptr);
            vkDestroySemaphore(device, renderCompleteSemaphore, nullptr);
            vkDestroySemaphore(device, presentCompleteSemaphore, nullptr);
            vkDestroyCommandPool(device, commandPool, nullptr);
            uniformBuffer.destroy();
        }
    } singleCB;

    // Multiple command buffers scenario (render ahead)
    struct MultiCB {
        const uint32_t renderAhead = 2;
        // Synchronization primitives are used to limit render ahead
        std::vector<vk::Fence> waitFences;
        std::vector<vk::Semaphore> renderCompleteSemaphores;
        std::vector<vk::Semaphore> presentCompleteSemaphores;
        // Command buffers and uniform buffers are per swap chain image
        vk::CommandPool commandPool;
        std::vector<vk::CommandBuffer> commandBuffers;
        std::vector<vk::DescriptorSet> descriptorSets;
        std::vector<vks::Buffer> uniformBuffers;
        uint32_t frameIndex = 0;
        void cleanup(vk::Device device) {
            for (auto& fence : waitFences) {
                vkDestroyFence(device, fence, nullptr);
            }
            for (auto& semaphore : renderCompleteSemaphores) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
            for (auto& semaphore : presentCompleteSemaphores) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
            vkDestroyCommandPool(device, commandPool, nullptr);
            for (auto& uniformBuffer : uniformBuffers) {
                uniformBuffer.destroy();
            }
        }
    } multiCB;

    /// @todo: dynamic scene with frustum culling (maybe terrain + simple trees)

    std::array<glm::vec4, 6> pushConstants;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        rotationSpeed = 0.5f;
        timerSpeed *= 0.5f;
        title = "Command buffers";
        settings.overlay = false;
        camera.type = Camera::CameraType::lookat;
        camera.position = { 0.0f, 0.0f, -30.0f };
        camera.setRotation(glm::vec3(-32.5f, 45.0f, 0.0f));
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 64.0f);
    }

    ~VulkanExample() {
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        models.scene.destroy();
        singleCB.cleanup(device);
        multiCB.cleanup(device);
    }

    void setRenderMode(RenderMode mode) {
        renderMode = mode;
        vkDeviceWaitIdle(device);
        switch (renderMode) {
            case SINGLE_CB_RECREATE:
                std::cout << "Using single command buffer, recreating each frame" << std::endl;
                break;
            case MULTIPLE_CB_STATIC:
                recordCommandBuffers();
                std::cout << "Using multiple prebuilt static command buffers for each frame" << std::endl;
                break;
        }
    }

    void setupDescriptors() {
        // Pool
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 + static_cast<uint32_t>(swapChain.imageCount)),
        };
        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 1 + static_cast<uint32_t>(swapChain.imageCount));
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

        // Layouts
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
        };
        vk::DescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
        vk::PushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(pushConstants), 0);
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

        // Descriptors
        // Single CB
        vk::DescriptorSetAllocateInfo descriptorSetAI = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAI, &singleCB.descriptorSet));
        vk::WriteDescriptorSet writeDescriptorSet =
            vks::initializers::writeDescriptorSet(singleCB.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &singleCB.uniformBuffer.descriptor);
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        // Multiple CB
        for (auto i = 0; i < multiCB.descriptorSets.size(); i++) {
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAI, &multiCB.descriptorSets[i]));
            vk::WriteDescriptorSet writeDescriptorSet =
                vks::initializers::writeDescriptorSet(multiCB.descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &multiCB.uniformBuffers[i].descriptor);
            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        }
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCI =
            vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
        vk::PipelineRasterizationStateCreateInfo rasterizationStateCI =
            vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
        vk::PipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
        vk::PipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
        vk::PipelineDepthStencilStateCreateInfo depthStencilStateCI =
            vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
        vk::PipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
        vk::PipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
        std::vector<vk::DynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStates);

        // Vertex bindings and attributes
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = { vks::initializers::vertexInputBindingDescription(0, vertexLayout.stride(),
                                                                                                                              VK_VERTEX_INPUT_RATE_VERTEX) };

        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),                  // Location 0 : Position
            vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),  // Location 1 : Normal
            vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6),     // Location 3 : UV
            vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 8)   // Location 3 : Cpöpr
        };

        vk::PipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
            loadShader(getAssetPath() + "shaders/pushconstants/lights.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getAssetPath() + "shaders/pushconstants/lights.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        vk::GraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
        pipelineCI.pVertexInputState = &vertexInputState;
        pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
        pipelineCI.pRasterizationState = &rasterizationStateCI;
        pipelineCI.pColorBlendState = &colorBlendStateCI;
        pipelineCI.pMultisampleState = &multisampleStateCI;
        pipelineCI.pViewportState = &viewportStateCI;
        pipelineCI.pDepthStencilState = &depthStencilStateCI;
        pipelineCI.pDynamicState = &dynamicStateCI;
        pipelineCI.stageCount = shaderStages.size();
        pipelineCI.pStages = shaderStages.data();

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
    }

    void prepareUniformBuffers() {
        /*
			Single command buffer 
		*/
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &singleCB.uniformBuffer,
                                                   sizeof(ShaderValues)));
        VK_CHECK_RESULT(singleCB.uniformBuffer.map());

        /*
			Multiple command buffers, one ubo per frame
		*/
        for (auto i = 0; i < multiCB.uniformBuffers.size(); i++) {
            VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &multiCB.uniformBuffers[i],
                                                       sizeof(ShaderValues)));
            VK_CHECK_RESULT(multiCB.uniformBuffers[i].map());
        }
    }

    void loadAssets() { models.scene.loadFromFile(getAssetPath() + "models/samplescene.dae", vertexLayout, 0.35f, vulkanDevice, queue); }

    void prepare() {
        VulkanExampleBase::prepare();

        /*
			Single command buffer, single thread
		*/

        vk::CommandPoolCreateInfo commandPoolCI{};
        commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        // This flag will implicitly reset command buffers from this pool when calling vkBeginCommandBuffer
        commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCI.queueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
        VK_CHECK_RESULT(vkCreateCommandPool(device, &commandPoolCI, nullptr, &singleCB.commandPool));

        // A fence is need to check for command buffer completion before we can recreate it
        vk::FenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
        VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &singleCB.waitFence));

        // Semaphores are used to order queue submissions
        vk::SemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &singleCB.presentCompleteSemaphore));
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &singleCB.renderCompleteSemaphore));

        // Create a single command buffer that is recorded every frame
        vk::CommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(singleCB.commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &singleCB.commandBuffer));

        /*
			Multiple command buffers, render ahead, single thread
		*/

        // This flag will tell the implementation that command buffers are short lived, possibly resulting in better performance
        commandPoolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        commandPoolCI.queueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
        VK_CHECK_RESULT(vkCreateCommandPool(device, &commandPoolCI, nullptr, &multiCB.commandPool));

        multiCB.waitFences.resize(multiCB.renderAhead);
        multiCB.presentCompleteSemaphores.resize(multiCB.renderAhead);
        multiCB.renderCompleteSemaphores.resize(multiCB.renderAhead);
        multiCB.commandBuffers.resize(swapChain.imageCount);
        multiCB.uniformBuffers.resize(swapChain.imageCount);
        multiCB.descriptorSets.resize(swapChain.imageCount);
        // Command buffer execution fences
        for (auto& waitFence : multiCB.waitFences) {
            vk::FenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
            VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &waitFence));
        }
        // Queue ordering semaphores
        for (auto& semaphore : multiCB.presentCompleteSemaphores) {
            vk::SemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
        }
        for (auto& semaphore : multiCB.renderCompleteSemaphores) {
            vk::SemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
        }
        // Command buffers
        {
            vk::CommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(multiCB.commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                                                                          static_cast<uint32_t>(multiCB.commandBuffers.size()));
            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, multiCB.commandBuffers.data()));
        }

        loadAssets();
        prepareUniformBuffers();
        setupDescriptors();
        preparePipelines();

        setRenderMode(SINGLE_CB_RECREATE);

        prepared = true;
    }

    /*
		Single command buffer always rendering to the current framebuffer
	*/
    void recordCommandBuffer() {
        // A fence is used to wait until this command buffer has finished execution and is no longer in-flight
        // Command buffers can only be re-recorded or destroyed if they are not in-flight
        VK_CHECK_RESULT(vkWaitForFences(device, 1, &singleCB.waitFence, VK_TRUE, UINT64_MAX));
        VK_CHECK_RESULT(vkResetFences(device, 1, &singleCB.waitFence));

        vk::ClearValue clearValues[2];
        clearValues[0].color = defaultClearColor;
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;
        renderPassBeginInfo.framebuffer = frameBuffers[currentBuffer];

        vk::CommandBuffer currentCB = singleCB.commandBuffer;

        vk::CommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::commandBufferBeginInfo();
        VK_CHECK_RESULT(vkBeginCommandBuffer(currentCB, &commandBufferBeginInfo));

        vkCmdBeginRenderPass(currentCB, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        vk::Viewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
        vkCmdSetViewport(currentCB, 0, 1, &viewport);

        vk::Rect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
        vkCmdSetScissor(currentCB, 0, 1, &scissor);

        // Update light positions
        // w component = light radius scale
        const float r = 7.5f;
        const float sin_t = sin(glm::radians(timer * 360));
        const float cos_t = cos(glm::radians(timer * 360));
        const float y = 4.0f;
        pushConstants[0] = glm::vec4(r * 1.1 * sin_t, y, r * 1.1 * cos_t, 1.0f);
        pushConstants[1] = glm::vec4(-r * sin_t, y, -r * cos_t, 1.0f);
        pushConstants[2] = glm::vec4(r * 0.85f * sin_t, y, -sin_t * 2.5f, 1.5f);
        pushConstants[3] = glm::vec4(0.0f, y, r * 1.25f * cos_t, 1.5f);
        pushConstants[4] = glm::vec4(r * 2.25f * cos_t, y, 0.0f, 1.25f);
        pushConstants[5] = glm::vec4(r * 2.5f * cos_t, y, r * 2.5f * sin_t, 1.25f);

        // Submit via push constant (rather than a UBO)
        vkCmdPushConstants(currentCB, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), pushConstants.data());

        vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &singleCB.descriptorSet, 0, nullptr);

        vk::DeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(currentCB, 0, 1, &models.scene.vertices.buffer, offsets);
        vkCmdBindIndexBuffer(currentCB, models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(currentCB, models.scene.indexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(currentCB);

        VK_CHECK_RESULT(vkEndCommandBuffer(currentCB));
    }

    /*
		Multiple command buffers rendering to different framebuffers
	*/
    void recordCommandBuffers() {
        vk::ClearValue clearValues[2];
        clearValues[0].color = defaultClearColor;
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vk::DeviceSize offsets[1] = { 0 };
        vk::Viewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
        vk::Rect2D scissor = vks::initializers::rect2D(width, height, 0, 0);

        const float r = 7.5f;
        const float sin_t = sin(glm::radians(timer * 360));
        const float cos_t = cos(glm::radians(timer * 360));
        const float y = 4.0f;
        pushConstants[0] = glm::vec4(r * 1.1 * sin_t, y, r * 1.1 * cos_t, 1.0f);
        pushConstants[1] = glm::vec4(-r * sin_t, y, -r * cos_t, 1.0f);
        pushConstants[2] = glm::vec4(r * 0.85f * sin_t, y, -sin_t * 2.5f, 1.5f);
        pushConstants[3] = glm::vec4(0.0f, y, r * 1.25f * cos_t, 1.5f);
        pushConstants[4] = glm::vec4(r * 2.25f * cos_t, y, 0.0f, 1.25f);
        pushConstants[5] = glm::vec4(r * 2.5f * cos_t, y, r * 2.5f * sin_t, 1.25f);

        for (auto i = 0; i < swapChain.imageCount; i++) {
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            vk::CommandBuffer currentCB = multiCB.commandBuffers[i];

            vk::CommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(currentCB, &commandBufferBeginInfo));
            vkCmdBeginRenderPass(currentCB, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdSetViewport(currentCB, 0, 1, &viewport);
            vkCmdSetScissor(currentCB, 0, 1, &scissor);

            vkCmdPushConstants(currentCB, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), pushConstants.data());
            vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &multiCB.descriptorSets[2], 0, nullptr);
            vkCmdBindVertexBuffers(currentCB, 0, 1, &models.scene.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(currentCB, models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(currentCB, models.scene.indexCount, 1, 0, 0, 0);

            vkCmdEndRenderPass(currentCB);
            VK_CHECK_RESULT(vkEndCommandBuffer(currentCB));
        }
    }

    void draw() {
        // Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
        const vk::PipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        switch (renderMode) {
            /*
			Render using a single command buffer that's recreated each frame
		*/
            case SINGLE_CB_RECREATE: {
                // Acquire the next image from the swap chain
                vk::Result acquire = swapChain.acquireNextImage(singleCB.presentCompleteSemaphore, &currentBuffer);
                if ((acquire == VK_ERROR_OUT_OF_DATE_KHR) || (acquire == VK_SUBOPTIMAL_KHR)) {
                    windowResize();
                } else {
                    VK_CHECK_RESULT(acquire);
                }

                memcpy(singleCB.uniformBuffer.mapped, &shaderValues, sizeof(ShaderValues));

                // (Re-)record command buffer
                if (!paused) {
                    recordCommandBuffer();
                }

                // Submit the command buffer to the graphics queue
                vk::SubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.pWaitDstStageMask = &waitStageMask;
                submitInfo.pWaitSemaphores = &singleCB.presentCompleteSemaphore;
                submitInfo.waitSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = &singleCB.renderCompleteSemaphore;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pCommandBuffers = &singleCB.commandBuffer;
                submitInfo.commandBufferCount = 1;

                // Submit to queue
                VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, singleCB.waitFence));

                // Present
                vk::Result present = swapChain.queuePresent(queue, currentBuffer, singleCB.renderCompleteSemaphore);
                if (!((present == VK_SUCCESS) || (present == VK_SUBOPTIMAL_KHR))) {
                    if (present == VK_ERROR_OUT_OF_DATE_KHR) {
                        windowResize();
                        return;
                    } else {
                        VK_CHECK_RESULT(present);
                    }
                }

                break;
            }

            /*
			Render using multiple command buffers (per frame) with render ahead
		*/
            case MULTIPLE_CB_STATIC: {
                vkWaitForFences(device, 1, &multiCB.waitFences[multiCB.frameIndex], VK_TRUE, UINT64_MAX);
                vkResetFences(device, 1, &multiCB.waitFences[multiCB.frameIndex]);

                vk::Result acquire = swapChain.acquireNextImage(multiCB.presentCompleteSemaphores[multiCB.frameIndex], &currentBuffer);
                if ((acquire == VK_ERROR_OUT_OF_DATE_KHR) || (acquire == VK_SUBOPTIMAL_KHR)) {
                    windowResize();
                } else {
                    VK_CHECK_RESULT(acquire);
                }

                memcpy(multiCB.uniformBuffers[currentBuffer].mapped, &shaderValues, sizeof(ShaderValues));

                // Submit the current command buffer to the graphics queue
                vk::SubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.pWaitDstStageMask = &waitStageMask;
                submitInfo.pWaitSemaphores = &multiCB.presentCompleteSemaphores[multiCB.frameIndex];
                submitInfo.waitSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = &multiCB.renderCompleteSemaphores[multiCB.frameIndex];
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pCommandBuffers = &multiCB.commandBuffers[currentBuffer];
                submitInfo.commandBufferCount = 1;

                // Submit to queue
                VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, multiCB.waitFences[multiCB.frameIndex]));

                // Present
                vk::Result present = swapChain.queuePresent(queue, currentBuffer, multiCB.renderCompleteSemaphores[multiCB.frameIndex]);
                if (!((present == VK_SUCCESS) || (present == VK_SUBOPTIMAL_KHR))) {
                    if (present == VK_ERROR_OUT_OF_DATE_KHR) {
                        windowResize();
                        return;
                    } else {
                        VK_CHECK_RESULT(present);
                    }
                }

                multiCB.frameIndex += 1;
                multiCB.frameIndex %= multiCB.renderAhead;

                break;
            }
        }
    }

    virtual void render() {
        if (!prepared) {
            return;
        }
        draw();
        if (camera.updated) {
            shaderValues.projection = camera.matrices.perspective;
            shaderValues.model = camera.matrices.view;
        }
    }

#if !defined(__ANDROID__)
    virtual void keyPressed(uint32_t keyCode) {
        switch (keyCode) {
            case 0x31:
                setRenderMode(SINGLE_CB_RECREATE);
                break;
            case 0x32:
                setRenderMode(MULTIPLE_CB_STATIC);
                break;
        }
    }
#endif
};

VULKAN_EXAMPLE_MAIN()