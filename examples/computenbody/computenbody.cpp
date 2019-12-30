/*
* Vulkan Example - Compute shader N-body simulation using two passes and shared compute shader memory
*
* Copyright (C) by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vkx/vulkanExampleBase.hpp>
#include <vkx/texture.hpp>

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false
#if defined(__ANDROID__)
// Lower particle count on Android for performance reasons
#define PARTICLES_PER_ATTRACTOR 3 * 1024
#else
#define PARTICLES_PER_ATTRACTOR 4 * 1024
#endif

class VulkanExample : public VulkanExampleBase {
public:
    uint32_t numParticles;

    struct {
        vkx::texture::Texture2D particle;
        vkx::texture::Texture2D gradient;
    } textures;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    // Resources for the graphics part of the example
    struct {
        vks::Buffer uniformBuffer;                    // Contains scene matrices
        vk::DescriptorSetLayout descriptorSetLayout;  // Particle system rendering shader binding layout
        vk::DescriptorSet descriptorSet;              // Particle system rendering shader bindings
        vk::PipelineLayout pipelineLayout;            // Layout of the graphics pipeline
        vk::Pipeline pipeline;                        // Particle rendering pipeline
        vk::Semaphore semaphore;                      // Execution dependency between compute & graphic submission
        struct {
            glm::mat4 projection;
            glm::mat4 view;
            glm::vec2 screenDim;
        } ubo;
    } graphics;

    // Resources for the compute part of the example
    struct {
        vks::Buffer storageBuffer;                    // (Shader) storage buffer object containing the particles
        vks::Buffer uniformBuffer;                    // Uniform buffer object containing particle system parameters
        vk::Queue queue;                              // Separate queue for compute commands (queue family may differ from the one used for graphics)
        vk::CommandPool commandPool;                  // Use a separate command pool (queue family may differ from the one used for graphics)
        vk::CommandBuffer commandBuffer;              // Command buffer storing the dispatch commands and barriers
        vk::Semaphore semaphore;                      // Execution dependency between compute & graphic submission
        vk::DescriptorSetLayout descriptorSetLayout;  // Compute shader binding layout
        vk::DescriptorSet descriptorSet;              // Compute shader bindings
        vk::PipelineLayout pipelineLayout;            // Layout of the compute pipeline
        vk::Pipeline pipelineCalculate;               // Compute pipeline for N-Body velocity calculation (1st pass)
        vk::Pipeline pipelineIntegrate;               // Compute pipeline for euler integration (2nd pass)
        vk::Pipeline blur;
        vk::PipelineLayout pipelineLayoutBlur;
        vk::DescriptorSetLayout descriptorSetLayoutBlur;
        vk::DescriptorSet descriptorSetBlur;
        struct computeUBO {  // Compute shader uniform block object
            float deltaT;    //		Frame delta time
            int32_t particleCount;
        } ubo;
    } compute;

    // SSBO particle declaration
    struct Particle {
        glm::vec4 pos;  // xyz = position, w = mass
        glm::vec4 vel;  // xyz = velocity, w = gradient texture position
    };

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Compute shader N-body system";
        settings.overlay = true;
        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
        camera.setRotation(glm::vec3(-26.0f, 75.0f, 0.0f));
        camera.setTranslation(glm::vec3(0.0f, 0.0f, -14.0f));
        camera.movementSpeed = 2.5f;
    }

    ~VulkanExample() {
        // Graphics
        graphics.uniformBuffer.destroy();
        vkDestroyPipeline(device, graphics.pipeline, nullptr);
        vkDestroyPipelineLayout(device, graphics.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, graphics.descriptorSetLayout, nullptr);
        vkDestroySemaphore(device, graphics.semaphore, nullptr);

        // Compute
        compute.storageBuffer.destroy();
        compute.uniformBuffer.destroy();
        vkDestroyPipelineLayout(device, compute.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, compute.descriptorSetLayout, nullptr);
        vkDestroyPipeline(device, compute.pipelineCalculate, nullptr);
        vkDestroyPipeline(device, compute.pipelineIntegrate, nullptr);
        vkDestroySemaphore(device, compute.semaphore, nullptr);
        vkDestroyCommandPool(device, compute.commandPool, nullptr);

        textures.particle.destroy();
        textures.gradient.destroy();
    }

    void loadAssets() {
        textures.particle.loadFromFile(context, getAssetPath() + "textures/particle01_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
        textures.gradient.loadFromFile(context, getAssetPath() + "textures/particle_gradient_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
    }

    void buildCommandBuffers() {
        // Destroy command buffers if already present
        if (!checkCommandBuffers()) {
            destroyCommandBuffers();
            createCommandBuffers();
        }

        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
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

            // Draw the particle system using the update vertex buffer

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            vk::Rect2D scissor{ width, height, 0, 0 };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipeline);
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipelineLayout, 0, 1, &graphics.descriptorSet, 0, NULL);

            vk::DeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &compute.storageBuffer.buffer, offsets);
            vkCmdDraw(drawCmdBuffers[i], numParticles, 1, 0, 0);

            drawUI(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void buildComputeCommandBuffer() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        VK_CHECK_RESULT(vkBeginCommandBuffer(compute.commandBuffer, &cmdBufInfo));

        // First pass: Calculate particle movement
        // -------------------------------------------------------------------------------------------------------
        vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineCalculate);
        vkCmdBindDescriptorSets(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0, 0);
        vkCmdDispatch(compute.commandBuffer, numParticles / 256, 1, 1);

        // Add memory barrier to ensure that the computer shader has finished writing to the buffer
        vk::BufferMemoryBarrier bufferBarrier;
        bufferBarrier.buffer = compute.storageBuffer.buffer;
        bufferBarrier.size = compute.storageBuffer.descriptor.range;
        bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        // Transfer ownership if compute and graphics queue familiy indices differ
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        vkCmdPipelineBarrier(compute.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_FLAGS_NONE, 0, nullptr, 1,
                             &bufferBarrier, 0, nullptr);

        // Second pass: Integrate particles
        // -------------------------------------------------------------------------------------------------------
        vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineIntegrate);
        vkCmdDispatch(compute.commandBuffer, numParticles / 256, 1, 1);

        vkEndCommandBuffer(compute.commandBuffer);
    }

    // Setup and fill the compute shader storage buffers containing the particles
    void prepareStorageBuffers() {
#if 0
		std::vector<glm::vec3> attractors = {
			glm::vec3(2.5f, 1.5f, 0.0f),
			glm::vec3(-2.5f, -1.5f, 0.0f),
		};
#else
        std::vector<glm::vec3> attractors = {
            glm::vec3(5.0f, 0.0f, 0.0f),  glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 5.0f),
            glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 4.0f, 0.0f),  glm::vec3(0.0f, -8.0f, 0.0f),
        };
#endif

        numParticles = static_cast<uint32_t>(attractors.size()) * PARTICLES_PER_ATTRACTOR;

        // Initial particle positions
        std::vector<Particle> particleBuffer(numParticles);

        std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
        std::normal_distribution<float> rndDist(0.0f, 1.0f);

        for (uint32_t i = 0; i < static_cast<uint32_t>(attractors.size()); i++) {
            for (uint32_t j = 0; j < PARTICLES_PER_ATTRACTOR; j++) {
                Particle& particle = particleBuffer[i * PARTICLES_PER_ATTRACTOR + j];

                // First particle in group as heavy center of gravity
                if (j == 0) {
                    particle.pos = glm::vec4(attractors[i] * 1.5f, 90000.0f);
                    particle.vel = glm::vec4(glm::vec4(0.0f));
                } else {
                    // Position
                    glm::vec3 position(attractors[i] + glm::vec3(rndDist(rndEngine), rndDist(rndEngine), rndDist(rndEngine)) * 0.75f);
                    float len = glm::length(glm::normalize(position - attractors[i]));
                    position.y *= 2.0f - (len * len);

                    // Velocity
                    glm::vec3 angular = glm::vec3(0.5f, 1.5f, 0.5f) * (((i % 2) == 0) ? 1.0f : -1.0f);
                    glm::vec3 velocity =
                        glm::cross((position - attractors[i]), angular) + glm::vec3(rndDist(rndEngine), rndDist(rndEngine), rndDist(rndEngine) * 0.025f);

                    float mass = (rndDist(rndEngine) * 0.5f + 0.5f) * 75.0f;
                    particle.pos = glm::vec4(position, mass);
                    particle.vel = glm::vec4(velocity, 0.0f);
                }

                // Color gradient offset
                particle.vel.w = (float)i * 1.0f / static_cast<uint32_t>(attractors.size());
            }
        }

        compute.ubo.particleCount = numParticles;

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

        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vk::vertexInputBindingDescription{VERTEX_BUFFER_BIND_ID, sizeof(Particle), vk::VertexInputRate::eVertex};

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(2);
        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32A32sFloat, offsetof(Particle, pos)};
        // Location 1 : Velocity (used for gradient lookup)
        vertices.attributeDescriptions[1] =
            vk::vertexInputAttributeDescription{VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32A32sFloat, offsetof(Particle, vel)};

        // Assign to vertex buffer
        vertices.inputState;
        vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 2 },
                                                          { vk::DescriptorType::eStorageBuffer, 1 },
                                                          { vk::DescriptorType::eCombinedImageSampler, 2 } };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vk::descriptorPoolCreateInfo{static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 2};

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings;
        setLayoutBindings = {
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0 },
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1 },
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 2 },
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size())};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &graphics.descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&graphics.descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &graphics.pipelineLayout));
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &graphics.descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &graphics.descriptorSet));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets;
        writeDescriptorSets = {
            { graphics.descriptorSet, vk::DescriptorType::eCombinedImageSampler, 0, &textures.particle.descriptor },
            { graphics.descriptorSet, vk::DescriptorType::eCombinedImageSampler, 1, &textures.gradient.descriptor },
            { graphics.descriptorSet, vk::DescriptorType::eUniformBuffer, 2, &graphics.uniformBuffer.descriptor },
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vk::pipelineInputAssemblyStateCreateInfo{VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 0, VK_FALSE};

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vk::pipelineRasterizationStateCreateInfo{VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0};

        vk::PipelineColorBlendAttachmentState blendAttachmentState = vk::pipelineColorBlendAttachmentState{0xf, VK_FALSE};

        vk::PipelineColorBlendStateCreateInfo colorBlendState = vk::pipelineColorBlendStateCreateInfo{1, &blendAttachmentState};

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vk::pipelineDepthStencilStateCreateInfo{VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS};

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = vk::pipelineMultisampleStateCreateInfo{VK_SAMPLE_COUNT_1_BIT, 0};

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vk::pipelineDynamicStateCreateInfo{dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0};

        // Rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/computenbody/particle.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/computenbody/particle.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo = vk::pipelineCreateInfo{graphics.pipelineLayout, renderPass, 0};

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

    void prepareGraphics() {
        prepareStorageBuffers();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorSet();

        // Semaphore for compute & graphics sync
        vk::SemaphoreCreateInfo semaphoreCreateInfo;
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &graphics.semaphore));
    }

    void prepareCompute() {
        // Create a compute capable device queue
        // The VulkanDevice::createLogicalDevice functions finds a compute capable queue and prefers queue families that only support compute
        // Depending on the implementation this may result in different queue family indices for graphics and computes,
        // requiring proper synchronization (see the memory barriers in buildComputeCommandBuffer)
        vk::DeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.pNext = NULL;
        queueCreateInfo.queueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
        queueCreateInfo.queueCount = 1;
        vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.compute, 0, &compute.queue);

        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines even if they use the same queue (family index)

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Particle position storage buffer
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCOMPUTE, 0},
            // Binding 1 : Uniform buffer
            vk::descriptorSetLayoutBinding{vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eCOMPUTE, 1},
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::descriptorSetLayoutCreateInfo{setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size())};

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &compute.descriptorSetLayout));

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vk::pipelineLayoutCreateInfo{&compute.descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &compute.pipelineLayout));

        vk::DescriptorSetAllocateInfo allocInfo = vk::descriptorSetAllocateInfo{descriptorPool, &compute.descriptorSetLayout, 1};

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &compute.descriptorSet));

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets = {
            // Binding 0 : Particle position storage buffer
            vk::writeDescriptorSet{compute.descriptorSet, vk::DescriptorType::eStorageBuffer, 0, &compute.storageBuffer.descriptor},
            // Binding 1 : Uniform buffer
            vk::writeDescriptorSet{compute.descriptorSet, vk::DescriptorType::eUniformBuffer, 1, &compute.uniformBuffer.descriptor}
        };

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(computeWriteDescriptorSets.size()), computeWriteDescriptorSets.data(), 0, NULL);

        // Create pipelines
        vk::ComputePipelineCreateInfo computePipelineCreateInfo{ compute.pipelineLayout, 0 };

        // 1st pass
        computePipelineCreateInfo.stage = loadShader(getAssetPath() + "shaders/computenbody/particle_calculate.comp.spv", vk::ShaderStageFlagBits::eCOMPUTE);

        // Set shader parameters via specialization constants
        struct SpecializationData {
            uint32_t sharedDataSize;
            float gravity;
            float power;
            float soften;
        } specializationData;

        std::vector<vk::SpecializationMapEntry> specializationMapEntries;
                specializationMapEntries.push_back({ 0, offsetof(SpecializationData, sharedDataSize), sizeof(uint32_t)) };
		specializationMapEntries.push_back({ 1, offsetof(SpecializationData, gravity), sizeof(float)) };
		specializationMapEntries.push_back({ 2, offsetof(SpecializationData, power), sizeof(float)) };
		specializationMapEntries.push_back({ 3, offsetof(SpecializationData, soften), sizeof(float)) };

		specializationData.sharedDataSize = std::min((uint32_t)1024, (uint32_t)(vulkanDevice->properties.limits.maxComputeSharedMemorySize / sizeof(glm::vec4)));

		specializationData.gravity = 0.002f;
		specializationData.power = 0.75f;
		specializationData.soften = 0.05f;

		vk::SpecializationInfo specializationInfo = 
			{ static_cast<uint32_t>(specializationMapEntries.size()), specializationMapEntries.data(), sizeof(specializationData), &specializationData };
		computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;

		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &compute.pipelineCalculate));

		// 2nd pass
		computePipelineCreateInfo.stage = loadShader(getAssetPath() + "shaders/computenbody/particle_integrate.comp.spv", vk::ShaderStageFlagBits::eCOMPUTE);
		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &compute.pipelineIntegrate));

		// Separate command pool as queue family for compute may be different than graphics
		vk::CommandPoolCreateInfo cmdPoolInfo = {};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &compute.commandPool));

		// Create a command buffer for compute operations
		vk::CommandBufferAllocateInfo cmdBufAllocateInfo =
			vks::initializers::commandBufferAllocateInfo(
				compute.commandPool,
				VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				1);	

		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &compute.commandBuffer));

		// Semaphore for compute & graphics sync
		vk::SemaphoreCreateInfo semaphoreCreateInfo;
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &compute.semaphore));

		// Signal the semaphore
		vk::SubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &compute.semaphore;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VK_CHECK_RESULT(vkQueueWaitIdle(queue));

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

        // Vertex shader uniform buffer block
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &graphics.uniformBuffer, sizeof(graphics.ubo));

        // Map for host access
        VK_CHECK_RESULT(graphics.uniformBuffer.map());

        updateComputeUniformBuffers();
        updateGraphicsUniformBuffers();
    }

    void updateComputeUniformBuffers() {
        compute.ubo.deltaT = paused ? 0.0f : frameTimer * 0.05f;
        memcpy(compute.uniformBuffer.mapped, &compute.ubo, sizeof(compute.ubo));
    }

    void updateGraphicsUniformBuffers() {
        graphics.ubo.projection = camera.matrices.perspective;
        graphics.ubo.view = camera.matrices.view;
        graphics.ubo.screenDim = glm::vec2((float)width, (float)height);
        memcpy(graphics.uniformBuffer.mapped, &graphics.ubo, sizeof(graphics.ubo));
    }

    void draw() {
        VulkanExampleBase::prepareFrame();

        vk::PipelineStageFlags graphicsWaitStageMasks[] = { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        vk::Semaphore graphicsWaitSemaphores[] = { compute.semaphore, semaphores.presentComplete };
        vk::Semaphore graphicsSignalSemaphores[] = { graphics.semaphore, semaphores.renderComplete };

        // Submit graphics commands
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        submitInfo.waitSemaphoreCount = 2;
        submitInfo.pWaitSemaphores = graphicsWaitSemaphores;
        submitInfo.pWaitDstStageMask = graphicsWaitStageMasks;
        submitInfo.signalSemaphoreCount = 2;
        submitInfo.pSignalSemaphores = graphicsSignalSemaphores;
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

        VulkanExampleBase::submitFrame();

        // Wait for rendering finished
        vk::PipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        // Submit compute commands
        vk::SubmitInfo computeSubmitInfo;
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;
        computeSubmitInfo.waitSemaphoreCount = 1;
        computeSubmitInfo.pWaitSemaphores = &graphics.semaphore;
        computeSubmitInfo.pWaitDstStageMask = &waitStageMask;
        computeSubmitInfo.signalSemaphoreCount = 1;
        computeSubmitInfo.pSignalSemaphores = &compute.semaphore;
        VK_CHECK_RESULT(vkQueueSubmit(compute.queue, 1, &computeSubmitInfo, VK_NULL_HANDLE));
    }

    void prepare() {
        VulkanExampleBase::prepare();

        setupDescriptorPool();
        prepareGraphics();
        prepareCompute();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        updateComputeUniformBuffers();
        if (camera.updated) {
            updateGraphicsUniformBuffers();
        }
    }
};

VULKAN_EXAMPLE_MAIN()