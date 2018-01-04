/*
* Vulkan Example - Attraction based compute shader particle system
*
* Updated compute shader by Lukas Bergdoll (https://github.com/Voultapher)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>

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
        vks::texture::Texture2D particle;
        vks::texture::Texture2D gradient;
    } textures;

    struct {
        vk::Pipeline postCompute;
        // Compute pipelines are separated from
        // graphics pipelines in Vulkan
        vk::Pipeline compute;
    } pipelines;

    vk::Queue computeQueue;
    //vk::CommandBuffer computeCmdBuffer;
    vk::PipelineLayout computePipelineLayout;
    vk::DescriptorSet computeDescriptorSet;
    vk::DescriptorSetLayout computeDescriptorSetLayout;

    vks::Buffer computeStorageBuffer;

    struct ComputeUbo {
        float deltaT;
        float destX;
        float destY;
        int32_t particleCount = PARTICLE_COUNT;
    } computeUbo;

    struct {
        struct {
            vks::Buffer ubo;
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

    VulkanExample() { title = "Vulkan Example - Compute shader particle system"; }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class

        device.destroyPipeline(pipelines.postCompute);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);
        device.destroyBuffer(computeStorageBuffer.buffer);
        device.freeMemory(computeStorageBuffer.memory);

        uniformData.computeShader.ubo.destroy();

        device.destroyPipelineLayout(computePipelineLayout);
        device.destroyDescriptorSetLayout(computeDescriptorSetLayout);
        device.destroyPipeline(pipelines.compute);

        textures.particle.destroy();
        textures.gradient.destroy();
    }

    void loadTextures() {
        textures.particle.loadFromFile(context, getAssetPath() + "textures/particle01_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
        textures.gradient.loadFromFile(context, getAssetPath() + "textures/particle_gradient_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
    }

    void updateCommandBufferPreDraw(const vk::CommandBuffer& cmdBuffer) override {
        // Compute particle movement
        // Add memory barrier to ensure that the (rendering) vertex shader operations have finished
        // Required as the compute shader will overwrite the vertex buffer data
        vk::BufferMemoryBarrier bufferBarrier;
        // Vertex shader invocations have finished reading from the buffer
        bufferBarrier.srcAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
        // Compute shader buffer read and write
        bufferBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
        bufferBarrier.buffer = computeStorageBuffer.buffer;
        bufferBarrier.size = computeStorageBuffer.descriptor.range;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eVertexInput, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags(), nullptr,
                                  bufferBarrier, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines.compute);
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout, 0, computeDescriptorSet, nullptr);
        // Dispatch the compute job
        cmdBuffer.dispatch(PARTICLE_COUNT / 16, 1, 1);

        // Add memory barrier to ensure that compute shader has finished writing to the buffer
        // Without this the (rendering) vertex shader may display incomplete results (partial data from last frame)
        // Compute shader has finished writes to the buffer
        bufferBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        // Vertex shader access (attribute binding)
        bufferBarrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
        bufferBarrier.buffer = computeStorageBuffer.buffer;
        bufferBarrier.size = computeStorageBuffer.descriptor.range;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eVertexInput, vk::DependencyFlags(), nullptr,
                                  bufferBarrier, nullptr);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        // Draw the particle system using the update vertex buffer
        cmdBuffer.setViewport(0, vks::util::viewport(size));
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.postCompute);
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSetPostCompute, nullptr);
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, computeStorageBuffer.buffer, { 0 });
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

        uint32_t storageBufferSize = (uint32_t)(particleBuffer.size() * sizeof(Particle));

        // Staging
        // SSBO is static, copy to device local memory
        // This results in better performance
        computeStorageBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eStorageBuffer, particleBuffer);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBuffer, 1 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 2 },
        };

        descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Particle color map
            { 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
            // Binding 1 : Particle gradient ramp
            { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };
        descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        descriptorSetPostCompute = device.allocateDescriptorSets({ descriptorPool, 1, &descriptorSetLayout })[0];
        // vk::Image descriptor for the color map texture
        std::vector<vk::DescriptorImageInfo> texDescriptors{
            { textures.particle.sampler, textures.particle.view, vk::ImageLayout::eGeneral },
            { textures.gradient.sampler, textures.gradient.view, vk::ImageLayout::eGeneral },
        };

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            // Binding 0 : Particle color map
            { descriptorSetPostCompute, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptors[0] },
            // Binding 1 : Particle gradient ramp
            { descriptorSetPostCompute, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptors[1] },
        };
        device.updateDescriptorSets(writeDescriptorSets, {});
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayout, renderPass };
        pipelineBuilder.inputAssemblyState.topology = vk::PrimitiveTopology::ePointList;
        pipelineBuilder.depthStencilState = { false };
        auto& blendAttachmentState = pipelineBuilder.colorBlendState.blendAttachmentStates[0];
        // Additive blending
        blendAttachmentState.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;

        // Binding description
        pipelineBuilder.vertexInputState.bindingDescriptions = { vk::VertexInputBindingDescription{ VERTEX_BUFFER_BIND_ID, sizeof(Particle),
                                                                                                    vk::VertexInputRate::eVertex } };

        // Attribute descriptions
        // Describes memory layout and shader positions
        pipelineBuilder.vertexInputState.attributeDescriptions = {
            // Location 0 : Position
            vk::VertexInputAttributeDescription{ 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, offsetof(Particle, pos) },
            // Location 1 : Gradient position
            vk::VertexInputAttributeDescription{ 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32A32Sfloat, offsetof(Particle, gradientPos) },
        };

        // Rendering pipeline
        // Load shaders
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computeparticles/particle.vert.spv", vk::ShaderStageFlagBits::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computeparticles/particle.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines.postCompute = pipelineBuilder.create(context.pipelineCache);
    }

    void prepareCompute() {
        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines
        // even if they use the same queue

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Particle position storage buffer
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            // Binding 1 : Uniform buffer
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute },
        };

        computeDescriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        computePipelineLayout = device.createPipelineLayout({ {}, 1, &computeDescriptorSetLayout });

        computeDescriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &computeDescriptorSetLayout })[0];

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets{
            // Binding 0 : Particle position storage buffer
            { computeDescriptorSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &computeStorageBuffer.descriptor },
            // Binding 1 : Uniform buffer
            { computeDescriptorSet, 1, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.computeShader.ubo.descriptor },
        };

        device.updateDescriptorSets(computeWriteDescriptorSets, {});

        // Create pipeline
        vk::ComputePipelineCreateInfo computePipelineCreateInfo;
        computePipelineCreateInfo.layout = computePipelineLayout;
        computePipelineCreateInfo.stage =
            vks::shaders::loadShader(device, getAssetPath() + "shaders/computeparticles/particle.comp.spv", vk::ShaderStageFlagBits::eCompute);

        pipelines.compute = device.createComputePipelines(context.pipelineCache, computePipelineCreateInfo)[0];

        device.destroyShaderModule(computePipelineCreateInfo.stage.module);
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Compute shader uniform buffer block
        uniformData.computeShader.ubo = context.createUniformBuffer(computeUbo);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        computeUbo.deltaT = frameTimer * 2.5f;
        if (animate) {
            computeUbo.destX = sinf(glm::radians(timer * 360.0f)) * 0.75f;
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
        uint32_t queueCount = (uint32_t)queueProps.size();

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
        buildCommandBuffers();
        prepared = true;
    }

    void render() override {
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

    void toggleAnimation() { animate = !animate; }

    void keyPressed(uint32_t key) override {
        switch (key) {
            case KEY_A:
                toggleAnimation();
                break;
        }
    }

    void OnUpdateUIOverlay() override {
        if (ui.header("Settings")) {
            ui.checkBox("Moving attractor", &animate);
        }
    }
};

VULKAN_EXAMPLE_MAIN()
