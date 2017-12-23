/*
* Vulkan Example - Attraction based compute shader particle system
*
* Updated compute shader by Lukas Bergdoll (https://github.com/Voultapher)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"

#if defined(__ANDROID__)
// Lower particle count on Android for performance reasons
#define PARTICLE_COUNT 64 * 1024
#else
#define PARTICLE_COUNT 256 * 1024
#endif

class VulkanExample : public vkx::ExampleBase {
public:
    float timer = 0.0f;
    float animStart = 20.0f;
    bool animate = true;

    struct {
        vkx::Texture particle;
        vkx::Texture gradient;
    } textures;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vk::Pipeline postCompute;
        // Compute pipelines are separated from 
        // graphics pipelines in Vulkan
        vk::Pipeline compute;
    } pipelines;

    vk::Queue computeQueue;
    vk::CommandBuffer computeCmdBuffer, transferCmdBuffer;
    vk::Fence computeResultFence;

    vk::PipelineLayout computePipelineLayout;
    vk::DescriptorSet computeDescriptorSet;
    vk::DescriptorSetLayout computeDescriptorSetLayout;
    vkx::CreateBufferResult computeStorageBuffer, drawStorageBuffer;

    struct ComputeUbo {
        float deltaT;
        float destX;
        float destY;
        int32_t particleCount = PARTICLE_COUNT;
    } computeUbo;

    struct {
        struct {
            vkx::UniformData ubo;
        } computeShader;
    } uniformData;

    struct Particle {
        glm::vec2 pos;
        glm::vec2 vel;
        glm::vec4 gradientPos;
    };

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSetPostCompute;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() {
        enableTextOverlay = false;
        title = "Vulkan Example - Compute shader particle system";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class
        if (computeResultFence) {
            device.destroyFence(computeResultFence);
        }

        device.destroyPipeline(pipelines.postCompute);

        computeStorageBuffer.destroy();
        drawStorageBuffer.destroy();

        uniformData.computeShader.ubo.destroy();

        device.destroyPipelineLayout(computePipelineLayout);
        device.destroyDescriptorSetLayout(computeDescriptorSetLayout);
        device.destroyPipeline(pipelines.compute);

        textures.particle.destroy();
        textures.gradient.destroy();
    }

    void loadTextures() {
        textures.particle = textureLoader->loadTexture(getAssetPath() + "textures/particle01_rgba.ktx",  vk::Format::eR8G8B8A8Unorm);
        textures.gradient = textureLoader->loadTexture(getAssetPath() + "textures/particle_gradient_rgba.ktx",  vk::Format::eR8G8B8A8Unorm);
    }

    void updateComputeCommandBuffers() {

        vk::CommandBufferBeginInfo beginInfo;
        computeCmdBuffer.begin(beginInfo);
        // Compute particle movement
        computeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines.compute);
        computeCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout, 0, computeDescriptorSet, nullptr);
        // Dispatch the compute job
        computeCmdBuffer.dispatch(PARTICLE_COUNT / 16, 1, 1);
        computeCmdBuffer.end();



        vk::BufferMemoryBarrier computeBarrier, drawBarrier;
        computeBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
        computeBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        computeBarrier.buffer = computeStorageBuffer.buffer;
        computeBarrier.size = computeStorageBuffer.descriptor.range;
        drawBarrier.srcAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
        drawBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        drawBarrier.buffer = drawStorageBuffer.buffer;
        drawBarrier.size = computeStorageBuffer.descriptor.range;
        transferCmdBuffer.begin(beginInfo);
        transferCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags(), nullptr, { computeBarrier, drawBarrier }, nullptr);
        transferCmdBuffer.copyBuffer(computeStorageBuffer.buffer, drawStorageBuffer.buffer, vk::BufferCopy(0, 0, computeStorageBuffer.size));

        computeBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        computeBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
        drawBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        drawBarrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
        transferCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), nullptr, { computeBarrier, drawBarrier }, nullptr);
        transferCmdBuffer.end();

    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        // Draw the particle system using the update vertex buffer
        cmdBuffer.setViewport(0, vkx::viewport(size));
        cmdBuffer.setScissor(0, vkx::rect2D(size));
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.postCompute);
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSetPostCompute, nullptr);
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, drawStorageBuffer.buffer, { 0 });
        cmdBuffer.draw(PARTICLE_COUNT, 1, 0, 0);
    }

    // Setup and fill the compute shader storage buffers for
    // vertex positions and velocities
    void prepareStorageBuffers() {

        std::mt19937 rGenerator;
        std::uniform_real_distribution<float> rDistribution(-1.0f, 1.0f);

        // Initial particle positions
        std::vector<Particle> particleBuffer(PARTICLE_COUNT);
        for (auto& particle : particleBuffer) {
            particle.pos = glm::vec2(rDistribution(rGenerator), rDistribution(rGenerator));
            particle.vel = glm::vec2(0.0f);
            particle.gradientPos.x = particle.pos.x / 2.0f;
        }

        uint32_t storageBufferSize = particleBuffer.size() * sizeof(Particle);

        // Staging
        // SSBO is static, copy to device local memory 
        // This results in better performance
        computeStorageBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, particleBuffer);
        drawStorageBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, particleBuffer);

        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vkx::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Particle), vk::VertexInputRate::eVertex);

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(2);
        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0,  vk::Format::eR32G32Sfloat, 0);
        // Location 1 : Gradient position
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1,  vk::Format::eR32G32B32A32Sfloat, 4 * sizeof(float));

        // Assign to vertex buffer
        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
            vkx::descriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings;
        // Binding 0 : Particle color map
        setLayoutBindings.push_back(vkx::descriptorSetLayoutBinding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0));
        // Binding 1 : Particle gradient ramp
        setLayoutBindings.push_back(vkx::descriptorSetLayoutBinding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1));

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

        vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

        pipelineLayout = device.createPipelineLayout(pipelineLayoutCreateInfo);
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSetPostCompute = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the color map texture
        std::vector<vk::DescriptorImageInfo> texDescriptors;
        texDescriptors.push_back(vkx::descriptorImageInfo(textures.particle.sampler, textures.particle.view, vk::ImageLayout::eGeneral));
        texDescriptors.push_back(vkx::descriptorImageInfo(textures.gradient.sampler, textures.gradient.view, vk::ImageLayout::eGeneral));

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets;
        // Binding 0 : Particle color map
        writeDescriptorSets.push_back(vkx::writeDescriptorSet(descriptorSetPostCompute, vk::DescriptorType::eCombinedImageSampler, 0, &texDescriptors[0]));
        // Binding 1 : Particle gradient ramp
        writeDescriptorSets.push_back(vkx::writeDescriptorSet(descriptorSetPostCompute, vk::DescriptorType::eCombinedImageSampler, 1, &texDescriptors[1]));

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::ePointList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

        vk::PipelineColorBlendAttachmentState blendAttachmentState =
            vkx::pipelineColorBlendAttachmentState();

        vk::PipelineColorBlendStateCreateInfo colorBlendState =
            vkx::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vkx::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, vk::CompareOp::eAlways);

        vk::PipelineViewportStateCreateInfo viewportState =
            vkx::pipelineViewportStateCreateInfo(1, 1);

        vk::PipelineMultisampleStateCreateInfo multisampleState =
            vkx::pipelineMultisampleStateCreateInfo(vk::SampleCountFlagBits::e1);

        std::vector<vk::DynamicState> dynamicStateEnables = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        // Rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = context.loadShader(getAssetPath() + "shaders/computeparticlesasync/particle.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = context.loadShader(getAssetPath() + "shaders/computeparticlesasync/particle.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
            vkx::pipelineCreateInfo(pipelineLayout, renderPass);

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

        // Additive blending
        blendAttachmentState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;

        pipelines.postCompute = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    void prepareCompute() {
        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines
        // even if they use the same queue

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Particle position storage buffer
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eStorageBuffer,
                vk::ShaderStageFlagBits::eCompute,
                0),
            // Binding 1 : Uniform buffer
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eCompute,
                1),
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        computeDescriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);


        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&computeDescriptorSetLayout, 1);

        computePipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);

        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &computeDescriptorSetLayout, 1);

        computeDescriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets =
        {
            // Binding 0 : Particle position storage buffer
            vkx::writeDescriptorSet(
                computeDescriptorSet,
                vk::DescriptorType::eStorageBuffer,
                0,
                &computeStorageBuffer.descriptor),
            // Binding 1 : Uniform buffer
            vkx::writeDescriptorSet(
                computeDescriptorSet,
                vk::DescriptorType::eUniformBuffer,
                1,
                &uniformData.computeShader.ubo.descriptor)
        };

        device.updateDescriptorSets(computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, NULL);

        // Create pipeline        
        vk::ComputePipelineCreateInfo computePipelineCreateInfo =
            vkx::computePipelineCreateInfo(computePipelineLayout);

        computePipelineCreateInfo.stage = context.loadShader(getAssetPath() + "shaders/computeparticles/particle.comp.spv", vk::ShaderStageFlagBits::eCompute);

        pipelines.compute = device.createComputePipelines(context.pipelineCache, computePipelineCreateInfo, nullptr)[0];

        vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
        cmdBufAllocateInfo.commandPool = context.getCommandPool();
        cmdBufAllocateInfo.commandBufferCount = 1;
        cmdBufAllocateInfo.level = vk::CommandBufferLevel::ePrimary;
        computeCmdBuffer = device.allocateCommandBuffers(cmdBufAllocateInfo)[0];
        transferCmdBuffer = device.allocateCommandBuffers(cmdBufAllocateInfo)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Compute shader uniform buffer block
        uniformData.computeShader.ubo= context.createUniformBuffer(computeUbo);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        computeUbo.deltaT = frameTimer * 2.5f;
        if (animate) {
            computeUbo.destX = sin(glm::radians(timer*360.0)) * 0.75f;
            computeUbo.destY = 0.f;
        } else {
            float normalizedMx = (mousePos.x - static_cast<float>(size.width / 2)) / static_cast<float>(size.width / 2);
            float normalizedMy = (mousePos.y - static_cast<float>(size.height / 2)) / static_cast<float>(size.height / 2);
            computeUbo.destX = normalizedMx;
            computeUbo.destY = normalizedMy;
        }

        memcpy(uniformData.computeShader.ubo.mapped, &computeUbo, sizeof(computeUbo));
    }

    // Find and create a compute capable device queue
    void getComputeQueue() {
        uint32_t queueIndex = 0;
        std::vector<vk::QueueFamilyProperties> queueProps = context.physicalDevice.getQueueFamilyProperties();
        uint32_t queueCount = queueProps.size();


        for (queueIndex = 0; queueIndex < queueCount; queueIndex++) {
            if (queueProps[queueIndex].queueFlags & vk::QueueFlagBits::eCompute)
                break;
        }
        assert(queueIndex < queueCount);

        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = queueIndex;
        queueCreateInfo.queueCount = 1;
        computeQueue = device.getQueue(queueIndex, 0);
    }

    void prepare() override {
        ExampleBase::prepare();
        loadTextures();
        getComputeQueue();
        prepareStorageBuffers();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        prepareCompute();
        updateDrawCommandBuffers();
        updateComputeCommandBuffers();
        prepared = true;
    }

    void copyComputeResults() {
        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &transferCmdBuffer;
        // Submit to queue
        queue.submit(submitInfo, vk::Fence());
    }

    void render() override {
        if (!prepared)
            return;

        // Check for compute operation results
        if (computeResultFence && vk::Result::eSuccess == device.getFenceStatus(computeResultFence)) {
            copyComputeResults();
            device.destroyFence(computeResultFence);
            computeResultFence = vk::Fence();
        }

        if (!computeResultFence) {
            computeResultFence = device.createFence(vk::FenceCreateInfo());
            vk::SubmitInfo computeSubmitInfo;
            computeSubmitInfo.pCommandBuffers = &computeCmdBuffer;
            computeSubmitInfo.commandBufferCount = 1;
            computeQueue.submit(computeSubmitInfo, computeResultFence);
        }

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

    void toggleAnimation() {
        animate = !animate;
    }

    void keyPressed(uint32_t key) override {
        switch (key) {
        case GLFW_KEY_A:
            toggleAnimation();
            break;
        }
    }
};

RUN_EXAMPLE(VulkanExample)
