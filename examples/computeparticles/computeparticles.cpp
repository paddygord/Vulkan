/*
* Vulkan Example - Attraction based compute shader particle system
*
* Updated compute shader by Lukas Bergdoll (https://github.com/Voultapher)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vkx/vulkanExampleBase.hpp>
#include <vkx/texture.hpp>

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false
#if defined(__ANDROID__)
// Lower particle count on Android for performance reasons
#define PARTICLE_COUNT 128 * 1024
#else
#define PARTICLE_COUNT 256 * 1024
#endif

class VulkanExample : public VulkanExampleBase {
public:
    float timer = 0.0f;
    float animStart = 20.0f;
    bool animate = true;

    struct {
        vkx::texture::Texture2D particle;
        vkx::texture::Texture2D gradient;
    } textures;

    // Resources for the graphics part of the example
    struct {
        vk::DescriptorSetLayout descriptorSetLayout;  // Particle system rendering shader binding layout
        vk::DescriptorSet descriptorSet;              // Particle system rendering shader bindings
        vk::PipelineLayout pipelineLayout;            // Layout of the graphics pipeline
        vk::Pipeline pipeline;                        // Particle rendering pipeline
    } graphics;

    // Resources for the compute part of the example
    struct Compute : public vkx::Compute {
        vks::Buffer storageBuffer;                    // (Shader) storage buffer object containing the particles
        vks::Buffer uniformBuffer;                    // Uniform buffer object containing particle system parameters
        vk::CommandBuffer commandBuffer;              // Command buffer storing the dispatch commands and barriers
        vk::DescriptorSetLayout descriptorSetLayout;  // Compute shader binding layout
        vk::DescriptorSet descriptorSet;              // Compute shader bindings
        vk::PipelineLayout pipelineLayout;            // Layout of the compute pipeline
        vk::Pipeline pipeline;                        // Compute pipeline for updating particle positions
        struct UBO {                           // Compute shader uniform block object
            float deltaT;                             //		Frame delta time
            float destX;                              //		x position of the attractor
            float destY;                              //		y position of the attractor
            int32_t particleCount = PARTICLE_COUNT;
        } ubo;
    } compute;

    // SSBO particle declaration
    struct Particle {
        glm::vec2 pos;          // Particle position
        glm::vec2 vel;          // Particle velocity
        glm::vec4 gradientPos;  // Texture coordiantes for the gradient ramp map
    };

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Compute shader particle system";
        settings.overlay = true;
    }

    ~VulkanExample() {
        // Graphics
        vkDestroyPipeline(device, graphics.pipeline, nullptr);
        vkDestroyPipelineLayout(device, graphics.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, graphics.descriptorSetLayout, nullptr);

        // Compute
        compute.storageBuffer.destroy();
        compute.uniformBuffer.destroy();
        vkDestroyPipelineLayout(device, compute.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, compute.descriptorSetLayout, nullptr);
        vkDestroyPipeline(device, compute.pipeline, nullptr);
        compute.destroy();

        textures.particle.destroy();
        textures.gradient.destroy();
    }

    void loadAssets() override {
        textures.particle.loadFromFile(context, getAssetPath() + "textures/particle01_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
        textures.gradient.loadFromFile(context, getAssetPath() + "textures/particle_gradient_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
    }


    void updateDrawCommandBuffer(const vk::CommandBuffer& commandBuffer) override {
        commandBuffer.setViewport(0, vks::util::viewport(size));
        commandBuffer.setScissor(0, vks::util::rect2D(size));
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics.pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphics.pipelineLayout, 0, graphics.descriptorSet, nullptr);
            commandBuffer.bindVertexBuffers(0, compute.storageBuffer.buffer, { 0 });
            commandBuffer.draw(PARTICLE_COUNT, 1, 0, 0);
    }


    void addComputeToGraphicsBarrier(const vk::CommandBuffer& commandBuffer, const vk::ArrayProxy<const vk::Buffer>& buffers) {
        vk::BufferMemoryBarrier bufferBarrier;
        bufferBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bufferBarrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
        bufferBarrier.srcQueueFamilyIndex = context.queueFamilyIndices.compute;
        bufferBarrier.dstQueueFamilyIndex = context.queueFamilyIndices.graphics;
        bufferBarrier.size = VK_WHOLE_SIZE;
        std::vector<vk::BufferMemoryBarrier> bufferBarriers;
        for (const auto& buffer : buffers) {
            bufferBarrier.buffer = buffer;
            bufferBarriers.push_back(bufferBarrier);
        }
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eVertexInput, {}, nullptr, bufferBarriers, nullptr);
    }

    void addGraphicsToComputeBarrier(const vk::CommandBuffer& commandBuffer) {
        vk::BufferMemoryBarrier bufferBarrier;
        bufferBarrier.srcAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
        bufferBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        bufferBarrier.srcQueueFamilyIndex = context.queueFamilyIndices.graphics;
        bufferBarrier.dstQueueFamilyIndex = context.queueFamilyIndices.compute;
        bufferBarrier.size = VK_WHOLE_SIZE;
        std::vector<vk::BufferMemoryBarrier> bufferBarriers;
        bufferBarrier.buffer = compute.storageBuffers.input.buffer;
        bufferBarriers.push_back(bufferBarrier);
        bufferBarrier.buffer = compute.storageBuffers.output.buffer;
        bufferBarriers.push_back(bufferBarrier);
        commandBuffer.pipelineBarrier(VERTEX_STAGE, COMPUTE_STAGE, vk::DependencyFlags{}, nullptr, bufferBarriers, nullptr);
    }


    void buildComputeCommandBuffer() {
        compute.commandBuffer.begin({ vk::CommandBufferUsageFlagBits::eSimultaneousUse });

        // Compute particle movement

        // Add memory barrier to ensure that the (graphics) vertex shader has fetched attributes before compute starts to write to the buffer
        vk::BufferMemoryBarrier bufferBarrier;
        bufferBarrier.buffer = compute.storageBuffer.buffer;
        bufferBarrier.size = compute.storageBuffer.descriptor.range;
        bufferBarrier.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;  // Vertex shader invocations have finished reading from the buffer
        bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;           // Compute shader wants to write to the buffer
        // Compute and graphics queue may have different queue families (see VulkanDevice::createLogicalDevice)
        // For the barrier to work across different queues, we need to set their family indices
        bufferBarrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;  // Required as compute and graphics queue may have different families
        bufferBarrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;   // Required as compute and graphics queue may have different families

        vkCmdPipelineBarrier(compute.commandBuffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_FLAGS_NONE, 0, nullptr, 1,
                             &bufferBarrier, 0, nullptr);

        vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
        vkCmdBindDescriptorSets(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0, 0);

        // Dispatch the compute job
        vkCmdDispatch(compute.commandBuffer, PARTICLE_COUNT / 256, 1, 1);

        // Add memory barrier to ensure that compute shader has finished writing to the buffer
        // Without this the (rendering) vertex shader may display incomplete results (partial data from last frame)
        bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;           // Compute shader has finished writes to the buffer
        bufferBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;  // Vertex shader invocations want to read from the buffer
        bufferBarrier.buffer = compute.storageBuffer.buffer;
        bufferBarrier.size = compute.storageBuffer.descriptor.range;
        // Compute and graphics queue may have different queue families (see VulkanDevice::createLogicalDevice)
        // For the barrier to work across different queues, we need to set their family indices
        bufferBarrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;   // Required as compute and graphics queue may have different families
        bufferBarrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;  // Required as compute and graphics queue may have different families

        vkCmdPipelineBarrier(compute.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_FLAGS_NONE, 0, nullptr, 1,
                             &bufferBarrier, 0, nullptr);

        vkEndCommandBuffer(compute.commandBuffer);
    }

    // Setup and fill the compute shader storage buffers containing the particles
    void prepareStorageBuffers() {
        std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
        std::uniform_real_distribution<float> rndDist(-1.0f, 1.0f);

        // Initial particle positions
        std::vector<Particle> particleBuffer(PARTICLE_COUNT);
        for (auto& particle : particleBuffer) {
            particle.pos = glm::vec2(rndDist(rndEngine), rndDist(rndEngine));
            particle.vel = glm::vec2(0.0f);
            particle.gradientPos.x = particle.pos.x / 2.0f;
        }

        vk::DeviceSize storageBufferSize = particleBuffer.size() * sizeof(Particle);

        // Staging
        // SSBO won't be changed on the host after upload so copy to device local memory

        vks::Buffer stagingBuffer;

        vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer,
                                   storageBufferSize, particleBuffer.data());

        vulkanDevice->createBuffer(
            // The SSBO will be used as a storage buffer for the compute pipeline and as a vertex buffer in the graphics pipeline
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &compute.storageBuffer, storageBufferSize);

        // Copy to staging buffer
        vk::CommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        vk::BufferCopy copyRegion = {};
        copyRegion.size = storageBufferSize;
        vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, compute.storageBuffer.buffer, 1, &copyRegion);
        VulkanExampleBase::flushCommandBuffer(copyCmd, queue, true);

        stagingBuffer.destroy();

    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 1 },
                                                          { vk::DescriptorType::eStorageBuffer, 1 },
                                                          { vk::DescriptorType::eCombinedImageSampler, 2 } };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo = vk::descriptorPoolCreateInfo{ static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 2 };

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Particle color map
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
            // Binding 1 : Particle gradient ramp
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        graphics.descriptorSetLayout = device.createDescriptorSetLayout({ {}, static_cast<uint32_t>(setLayoutBindings.size()), setLayoutBindings.data() });
        graphics.pipelineLayout = device.createPipelineLayout({ {}, 1,  &graphics.descriptorSetLayout });
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{ descriptorPool, &graphics.descriptorSetLayout, 1 };

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &graphics.descriptorSet));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets;
        // Binding 0 : Particle color map
        writeDescriptorSets.push_back(
            vk::writeDescriptorSet{graphics.descriptorSet, vk::DescriptorType::eCombinedImageSampler, 0, &textures.particle.descriptor)};
        // Binding 1 : Particle gradient ramp
        writeDescriptorSets.push_back(
            vk::writeDescriptorSet{graphics.descriptorSet, vk::DescriptorType::eCombinedImageSampler, 1, &textures.gradient.descriptor)};

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = vk::pipelineInputAssemblyStateCreateInfo{ VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 0, VK_FALSE };

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vk::pipelineRasterizationStateCreateInfo{ VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0 };

        vk::PipelineColorBlendAttachmentState blendAttachmentState = vk::pipelineColorBlendAttachmentState{ 0xf, VK_FALSE };

        vk::PipelineColorBlendStateCreateInfo colorBlendState = vk::pipelineColorBlendStateCreateInfo{ 1, &blendAttachmentState };

        vk::PipelineDepthStencilStateCreateInfo depthStencilState = vk::pipelineDepthStencilStateCreateInfo{ VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS };

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{ VK_SAMPLE_COUNT_1_BIT, 0 };

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{ dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0 };

        // Rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/computeparticles/particle.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/computeparticles/particle.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::pipelineCreateInfo{ graphics.pipelineLayout, renderPass, 0 };
        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] = vk::vertexInputBindingDescription{ VERTEX_BUFFER_BIND_ID, sizeof(Particle), vk::VertexInputRate::eVertex };

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(2);
        // Location 0 : Position
        vertices.attributeDescriptions[0] = vk::vertexInputAttributeDescription{ VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32sFloat, offsetof(Particle, pos) };
        // Location 1 : Gradient position
        vertices.attributeDescriptions[1] =
            vk::vertexInputAttributeDescription{ VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32A32sFloat, offsetof(Particle, gradientPos) };

        // Assign to vertex buffer
        vertices.inputState;
        vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();


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
        pipelineCreateInfo.renderPass = renderPass;

        // Additive blending
        blendAttachmentState.colorWriteMask = 0xF;
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &graphics.pipeline));
    }

    void prepareCompute() {
        // Create a compute capable device queue
        // The VulkanDevice::createLogicalDevice functions finds a compute capable queue and prefers queue families that only support compute
        // Depending on the implementation this may result in different queue family indices for graphics and computes,
        // requiring proper synchronization (see the memory barriers in buildComputeCommandBuffer)
        vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.compute, 0, &compute.queue);

        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines even if they use the same queue (family index)

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Particle position storage buffer
            vk::descriptorSetLayoutBinding{ vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCOMPUTE, 0 },
            // Binding 1 : Uniform buffer
            vk::descriptorSetLayoutBinding{ vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eCOMPUTE, 1 },
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{ setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()) };

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &compute.descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{ &compute.descriptorSetLayout, 1 };

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &compute.pipelineLayout));

        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{ descriptorPool, &compute.descriptorSetLayout, 1 };

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &compute.descriptorSet));

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets = {
            // Binding 0 : Particle position storage buffer
            vk::writeDescriptorSet{ compute.descriptorSet, vk::DescriptorType::eStorageBuffer, 0, &compute.storageBuffer.descriptor },
            // Binding 1 : Uniform buffer
            vk::writeDescriptorSet{ compute.descriptorSet, vk::DescriptorType::eUniformBuffer, 1, &compute.uniformBuffer.descriptor }
        };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(computeWriteDescriptorSets.size()), computeWriteDescriptorSets.data(), 0, NULL);

        // Create pipeline
        vk::ComputePipelineCreateInfo computePipelineCreateInfo{ compute.pipelineLayout, 0 };
        computePipelineCreateInfo.stage = loadShader(getAssetPath() + "shaders/computeparticles/particle.comp.spv", vk::ShaderStageFlagBits::eCOMPUTE);
        VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &compute.pipeline));

        // Separate command pool as queue family for compute may be different than graphics
        vk::CommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &compute.commandPool));

        // Create a command buffer for compute operations
        vk::CommandBufferAllocateInfo cmdBufAllocateInfo = vk::commandBufferAllocateInfo{ compute.commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };

        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &compute.commandBuffer));

        // Fence for compute CB sync
        vk::FenceCreateInfo fenceCreateInfo{ VK_FENCE_CREATE_SIGNALED_BIT };
        VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &compute.fence));

        // Build a single command buffer containing the compute dispatch commands
        buildComputeCommandBuffer();
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Compute shader uniform buffer block
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &compute.uniformBuffer, sizeof(compute.ubo));

        // Map for host access
        VK_CHECK_RESULT(compute.uniformBuffer.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        compute.ubo.deltaT = frameTimer * 2.5f;
        if (animate) {
            compute.ubo.destX = sin(glm::radians(timer * 360.0f)) * 0.75f;
            compute.ubo.destY = 0.0f;
        } else {
            float normalizedMx = (mousePos.x - static_cast<float>(width / 2)) / static_cast<float>(width / 2);
            float normalizedMy = (mousePos.y - static_cast<float>(height / 2)) / static_cast<float>(height / 2);
            compute.ubo.destX = normalizedMx;
            compute.ubo.destY = normalizedMy;
        }

        memcpy(compute.uniformBuffer.mapped, &compute.ubo, sizeof(compute.ubo));
    }

    void draw() {
        vk::SubmitInfo computeSubmitInfo;
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;

        VK_CHECK_RESULT(vkQueueSubmit(compute.queue, 1, &computeSubmitInfo, compute.fence));

        // Submit graphics commands
        VulkanExampleBase::prepareFrame();

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

        VulkanExampleBase::submitFrame();

        // Submit compute commands
        vkWaitForFences(device, 1, &compute.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &compute.fence);
    }

    void prepare() {
        VulkanExampleBase::prepare();

        prepareStorageBuffers();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        prepareCompute();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();

        if (animate) {
            if (animStart > 0.0f) {
                animStart -= frameTimer * 5.0f;
            } else if (animStart <= 0.0f) {
                timer += frameTimer * 0.04f;
                if (timer > 1.f)
                    timer = 0.f;
            }
        }

        updateUniformBuffers();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Settings")) {
            overlay->checkBox("Moving attractor", &animate);
        }
    }
};

VULKAN_EXAMPLE_MAIN()