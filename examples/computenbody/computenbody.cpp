/*
* Vulkan Example - Compute shader N-body simulation using two passes and shared compute shader memory
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false
#if defined(__ANDROID__)
// Lower particle count on Android for performance reasons
#define PARTICLES_PER_ATTRACTOR 3 * 1024
#else
#define PARTICLES_PER_ATTRACTOR 4 * 1024
#endif

class VulkanExample : public vkx::ExampleBase {
public:
    uint32_t numParticles;

    struct {
        vks::texture::Texture2D particle;
        vks::texture::Texture2D gradient;
    } textures;

    // Resources for the graphics part of the example
    struct {
        vks::Buffer uniformBuffer;                    // Contains scene matrices
        vk::DescriptorSetLayout descriptorSetLayout;  // Particle system rendering shader binding layout
        vk::DescriptorSet descriptorSet;              // Particle system rendering shader bindings
        vk::PipelineLayout pipelineLayout;            // Layout of the graphics pipeline
        vk::Pipeline pipeline;                        // Particle rendering pipeline
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
        vk::Fence fence;                              // Synchronization fence to avoid rewriting compute CB if still in use
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
            float destX;     //		x position of the attractor
            float destY;     //		y position of the attractor
            int32_t particleCount;
        } ubo;
    } compute;

    // SSBO particle declaration
    struct Particle {
        glm::vec4 pos;  // xyz = position, w = mass
        glm::vec4 vel;  // xyz = velocity, w = gradient texture position
    };

    VulkanExample() {
        title = "Compute shader N-body system";
        settings.overlay = true;
        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(60.0f, (float)size.width / (float)size.height, 0.1f, 512.0f);
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

        // Compute
        compute.storageBuffer.destroy();
        compute.uniformBuffer.destroy();
        vkDestroyPipelineLayout(device, compute.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, compute.descriptorSetLayout, nullptr);
        vkDestroyPipeline(device, compute.pipelineCalculate, nullptr);
        vkDestroyPipeline(device, compute.pipelineIntegrate, nullptr);
        vkDestroyFence(device, compute.fence, nullptr);
        vkDestroyCommandPool(device, compute.commandPool, nullptr);

        textures.particle.destroy();
        textures.gradient.destroy();
    }

    void loadAssets() {
        textures.particle.loadFromFile(context, getAssetPath() + "textures/particle01_rgba.ktx", vF::eR8G8B8A8Unorm);
        textures.gradient.loadFromFile(context, getAssetPath() + "textures/particle_gradient_rgba.ktx", vF::eR8G8B8A8Unorm);
    }

    void updateCommandBufferPreDraw(const vk::CommandBuffer& cmdBuffer) override {
        // Add memory barrier to ensure that the (graphics) vertex shader has fetched attributes before compute starts to write to the buffer
        vk::BufferMemoryBarrier bufferBarrier{
            vk::AccessFlagBits::eShaderRead,           // source access mask
            vk::AccessFlagBits::eVertexAttributeRead,  // dest access mask
            context.queueIndices.compute,              // source queue family
            context.queueIndices.graphics,             // dest queue family
            compute.storageBuffer.buffer,              // buffer handle
            0,                                         // buffer offset
            VK_WHOLE_SIZE                              // buffer size
        };
        cmdBuffer.pipelineBarrier(vPS::eComputeShader, vPS::eVertexInput, {}, nullptr, bufferBarrier, nullptr);
    }

    void updateCommandBufferPostDraw(const vk::CommandBuffer& cmdBuffer) override {
        // Add memory barrier to ensure that the (graphics) vertex shader has fetched attributes before compute starts to write to the buffer
        vk::BufferMemoryBarrier bufferBarrier{
            vk::AccessFlagBits::eVertexAttributeRead,  // source access mask
            vk::AccessFlagBits::eShaderWrite,          // dest access mask
            context.queueIndices.graphics,             // source queue family
            context.queueIndices.compute,              // dest queue family
            compute.storageBuffer.buffer,              // buffer handle
            0,                                         // buffer offset
            VK_WHOLE_SIZE                              // buffer size
        };
        cmdBuffer.pipelineBarrier(vPS::eVertexInput, vPS::eComputeShader, {}, nullptr, bufferBarrier, nullptr);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) {
        cmdBuffer.setViewport(0, viewport());
        cmdBuffer.setScissor(0, scissor());
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics.pipeline);
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphics.pipelineLayout, 0, graphics.descriptorSet, nullptr);
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, compute.storageBuffer.buffer, { 0 });
        cmdBuffer.draw(numParticles, 1, 0, 0);
    }

    void buildComputeCommandBuffer() {
        // Compute particle movement
        compute.commandBuffer.begin(vk::CommandBufferBeginInfo{});

        // First pass: Calculate particle movement
        // -------------------------------------------------------------------------------------------------------
        compute.commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute.pipelineCalculate);
        compute.commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute.pipelineLayout, 0, compute.descriptorSet, nullptr);
        compute.commandBuffer.dispatch(numParticles / 256, 1, 1);

        // Add memory barrier to ensure that compute shader has finished writing to the buffer
        vk::BufferMemoryBarrier bufferBarrier{ vAF::eShaderWrite,
                                               vAF::eShaderRead,
                                               VK_QUEUE_FAMILY_IGNORED,
                                               VK_QUEUE_FAMILY_IGNORED,
                                               compute.storageBuffer.buffer,
                                               0,
                                               VK_WHOLE_SIZE };  

        // Second pass: Integrate particles
        // -------------------------------------------------------------------------------------------------------
        compute.commandBuffer.pipelineBarrier(vPS::eComputeShader, vPS::eComputeShader, {}, nullptr, bufferBarrier, nullptr);
        compute.commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute.pipelineIntegrate);
        compute.commandBuffer.dispatch(numParticles / 256, 1, 1);
        compute.commandBuffer.end();
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

        std::mt19937 rndGen(static_cast<uint32_t>(time(0)));
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
                    glm::vec3 position(attractors[i] + glm::vec3(rndDist(rndGen), rndDist(rndGen), rndDist(rndGen)) * 0.75f);
                    float len = glm::length(glm::normalize(position - attractors[i]));
                    position.y *= 2.0f - (len * len);

                    // Velocity
                    glm::vec3 angular = glm::vec3(0.5f, 1.5f, 0.5f) * (((i % 2) == 0) ? 1.0f : -1.0f);
                    glm::vec3 velocity =
                        glm::cross((position - attractors[i]), angular) + glm::vec3(rndDist(rndGen), rndDist(rndGen), rndDist(rndGen) * 0.025f);

                    float mass = (rndDist(rndGen) * 0.5f + 0.5f) * 75.0f;
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
        compute.storageBuffer = context.stageToDeviceBuffer(vBU::eVertexBuffer | vBU::eStorageBuffer, particleBuffer);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vDT::eUniformBuffer, 2 },
            { vDT::eStorageBuffer, 1 },
            { vDT::eCombinedImageSampler, 2 },
        };

        descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            { 0, vDT::eCombinedImageSampler, 1, vSS::eFragment },
            { 1, vDT::eCombinedImageSampler, 1, vSS::eFragment },
            { 2, vDT::eUniformBuffer, 1, vSS::eVertex },
        };

        graphics.descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        graphics.pipelineLayout = device.createPipelineLayout({ {}, 1, &graphics.descriptorSetLayout });
    }

    void setupDescriptorSet() {
        graphics.descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &graphics.descriptorSetLayout })[0];
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            { graphics.descriptorSet, 0, 0, 1, vDT::eCombinedImageSampler, &textures.particle.descriptor },
            { graphics.descriptorSet, 1, 0, 1, vDT::eCombinedImageSampler, &textures.gradient.descriptor },
            { graphics.descriptorSet, 2, 0, 1, vDT::eUniformBuffer, nullptr, &graphics.uniformBuffer.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        // Rendering pipeline
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, graphics.pipelineLayout, renderPass };
        pipelineBuilder.inputAssemblyState.topology = vk::PrimitiveTopology::ePointList;
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        // Additive blending
        auto& blendAttachmentState = pipelineBuilder.colorBlendState.blendAttachmentStates[0];
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;
        pipelineBuilder.depthStencilState = { false };
        pipelineBuilder.vertexInputState.bindingDescriptions = {
            { VERTEX_BUFFER_BIND_ID, sizeof(Particle), vk::VertexInputRate::eVertex },
        };
        pipelineBuilder.vertexInputState.attributeDescriptions = {
            // Location 0 : Position
            { 0, VERTEX_BUFFER_BIND_ID, vF::eR32G32B32A32Sfloat, offsetof(Particle, pos) },
            // Location 1 : Velocity (used for gradient lookup)
            { 1, VERTEX_BUFFER_BIND_ID, vF::eR32G32B32A32Sfloat, offsetof(Particle, vel) },
        };
        // Load shaders
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computenbody/particle.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computenbody/particle.frag.spv", vSS::eFragment);
        graphics.pipeline = pipelineBuilder.create(context.pipelineCache);
    }

    void prepareCompute() {
        // Create a compute capable device queue
        // The VulkanDevice::createLogicalDevice functions finds a compute capable queue and prefers queue families that only support compute
        // Depending on the implementation this may result in different queue family indices for graphics and computes,
        // requiring proper synchronization (see the memory barriers in buildComputeCommandBuffer)
        compute.queue = device.getQueue(context.queueIndices.compute, 1);

        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines even if they use the same queue (family index)

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Particle position storage buffer
            { 0, vDT::eStorageBuffer, 1, vSS::eCompute },
            // Binding 1 : Uniform buffer
            { 1, vDT::eUniformBuffer, 1, vSS::eCompute },
        };

        compute.descriptorSetLayout =
            device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        compute.pipelineLayout = device.createPipelineLayout({ {}, 1, &compute.descriptorSetLayout });
        compute.descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &compute.descriptorSetLayout })[0];

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets{
            // Binding 0 : Particle position storage buffer
            { compute.descriptorSet, 0, 0, 1, vDT::eStorageBuffer, nullptr, &compute.storageBuffer.descriptor },
            // Binding 1 : Uniform buffer
            { compute.descriptorSet, 1, 0, 1, vDT::eUniformBuffer, nullptr, &compute.uniformBuffer.descriptor },
        };

        device.updateDescriptorSets(computeWriteDescriptorSets, nullptr);

        // Create pipelines
        vk::ComputePipelineCreateInfo computePipelineCreateInfo;
        computePipelineCreateInfo.layout = compute.pipelineLayout;

        // 1st pass
        computePipelineCreateInfo.stage = vks::shaders::loadShader(device, getAssetPath() + "shaders/computenbody/particle_calculate.comp.spv", vSS::eCompute);

        // Set shader parameters via specialization constants
        struct SpecializationData {
            uint32_t sharedDataSize;
            float gravity;
            float power;
            float soften;
        } specializationData;

        std::vector<vk::SpecializationMapEntry> specializationMapEntries{
            { 0, offsetof(SpecializationData, sharedDataSize), sizeof(uint32_t) },
            { 1, offsetof(SpecializationData, gravity), sizeof(float) },
            { 2, offsetof(SpecializationData, power), sizeof(float) },
            { 3, offsetof(SpecializationData, soften), sizeof(float) },
        };

        specializationData.sharedDataSize =
            std::min((uint32_t)1024, (uint32_t)(context.deviceProperties.limits.maxComputeSharedMemorySize / sizeof(glm::vec4)));

        specializationData.gravity = 0.002f;
        specializationData.power = 0.75f;
        specializationData.soften = 0.05f;

        vk::SpecializationInfo specializationInfo{ static_cast<uint32_t>(specializationMapEntries.size()), specializationMapEntries.data(),
                                                   sizeof(specializationData), &specializationData };
        computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;
        compute.pipelineCalculate = device.createComputePipeline(context.pipelineCache, computePipelineCreateInfo);
        device.destroyShaderModule(computePipelineCreateInfo.stage.module);
        // 2nd pass
        computePipelineCreateInfo.stage = vks::shaders::loadShader(device, getAssetPath() + "shaders/computenbody/particle_integrate.comp.spv", vSS::eCompute);
        compute.pipelineIntegrate = device.createComputePipeline(context.pipelineCache, computePipelineCreateInfo);
        device.destroyShaderModule(computePipelineCreateInfo.stage.module);

        // Separate command pool as queue family for compute may be different than graphics
        compute.commandPool = device.createCommandPool({ vk::CommandPoolCreateFlagBits::eResetCommandBuffer, context.queueIndices.compute });

        // Create a command buffer for compute operations
        compute.commandBuffer = device.allocateCommandBuffers({ compute.commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];

        // Fence for compute CB sync
        compute.fence = device.createFence({ vk::FenceCreateFlagBits::eSignaled });

        // Build a single command buffer containing the compute dispatch commands
        buildComputeCommandBuffer();
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Compute shader uniform buffer block
        compute.uniformBuffer = context.createUniformBuffer(compute.ubo);

        // Vertex shader uniform buffer block
        graphics.uniformBuffer = context.createUniformBuffer(graphics.ubo);

        updateGraphicsUniformBuffers();
    }

    void updateUniformBuffers() {
        compute.ubo.deltaT = paused ? 0.0f : frameTimer * 0.05f;
        compute.ubo.destX = sin(glm::radians(timer * 360.0f)) * 0.75f;
        compute.ubo.destY = 0.0f;
        memcpy(compute.uniformBuffer.mapped, &compute.ubo, sizeof(compute.ubo));
    }

    void updateGraphicsUniformBuffers() {
        graphics.ubo.projection = camera.matrices.perspective;
        graphics.ubo.view = camera.matrices.view;
        graphics.ubo.screenDim = glm::vec2((float)size.width, (float)size.height);
        memcpy(graphics.uniformBuffer.mapped, &graphics.ubo, sizeof(graphics.ubo));
    }

    void draw() {
        // Submit graphics commands
        ExampleBase::draw();

        // Submit compute commands
        device.waitForFences(compute.fence, VK_TRUE, UINT64_MAX);
        device.resetFences(compute.fence);

        vk::SubmitInfo computeSubmitInfo;
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;
        compute.queue.submit(computeSubmitInfo, compute.fence);
    }

    void prepare() {
        ExampleBase::prepare();
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
        updateUniformBuffers();
    }

    virtual void viewChanged() { updateGraphicsUniformBuffers(); }
};

VULKAN_EXAMPLE_MAIN()
