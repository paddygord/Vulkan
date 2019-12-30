/*
* Vulkan Example - Compute shader sloth simulation
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vkx/vulkanExampleBase.hpp>
#include <vkx/texture.hpp>
#include <vkx/model.hpp>
#include <vkx/compute.hpp>

#define ENABLE_VALIDATION true

class VulkanExample : public VulkanExampleBase {
    using Parent = VulkanExampleBase;

public:
    struct Compute : public vkx::Compute {
        using Parent = vkx::Compute;

        struct StorageBuffers {
            vks::Buffer input;
            vks::Buffer output;
        } storageBuffers;
        vks::Buffer uniformBuffer;
        std::vector<vk::CommandBuffer> commandBuffers;
        vk::DescriptorSetLayout descriptorSetLayout;
        std::array<vk::DescriptorSet, 2> descriptorSets;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        //vk::Fence fence;
        struct computeUBO {
            float deltaT = 0.0f;
            float particleMass = 0.1f;
            float springStiffness = 2000.0f;
            float damping = 0.25f;
            float restDistH;
            float restDistV;
            float restDistD;
            float sphereRadius = 0.5f;
            glm::vec4 spherePos = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            glm::vec4 gravity = glm::vec4(0.0f, 9.8f, 0.0f, 0.0f);
            glm::ivec2 particleCount;
        } ubo;

        void destroy() override {
            // Compute
            storageBuffers.input.destroy();
            storageBuffers.output.destroy();
            uniformBuffer.destroy();
            device.destroy(pipelineLayout);
            device.destroy(descriptorSetLayout);
            device.destroy(pipeline);
            //device.destroy(fence);
            device.destroy(commandPool);
            Parent::destroy();
        }
    } compute;

    uint32_t sceneSetup = 0;
    uint32_t readSet = 0;
    uint32_t indexCount;
    bool simulateWind = false;

    vkx::texture::Texture2D textureCloth;

    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
    } };
    vkx::model::Model modelSphere;

    // Resources for the graphics part of the example
    struct {
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorSet descriptorSet;
        vk::PipelineLayout pipelineLayout;
        struct Pipelines {
            vk::Pipeline cloth;
            vk::Pipeline sphere;
        } pipelines;
        vks::Buffer indices;
        vks::Buffer uniformBuffer;
        struct graphicsUBO {
            glm::mat4 projection;
            glm::mat4 view;
            glm::vec4 lightPos = glm::vec4(-1.0f, 2.0f, -1.0f, 1.0f);
        } ubo;
    } graphics;

    // Resources for the compute part of the example

    // SSBO cloth grid particle declaration
    struct Particle {
        glm::vec4 pos;
        glm::vec4 vel;
        glm::vec4 uv;
        glm::vec4 normal;
        float pinned;
        glm::vec3 _pad0;
    };

    struct Cloth {
        glm::uvec2 gridsize = glm::uvec2(60, 60);
        glm::vec2 size = glm::vec2(2.5f, 2.5f);
    } cloth;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Compute shader cloth simulation";
        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
        camera.setRotation(glm::vec3(-30.0f, -45.0f, 0.0f));
        camera.setTranslation(glm::vec3(0.0f, 0.0f, -3.5f));
        settings.overlay = true;
    }

    ~VulkanExample() {
        // Graphics
        graphics.uniformBuffer.destroy();
        device.destroy(graphics.pipelines.cloth);
        device.destroy(graphics.pipelines.sphere);
        device.destroy(graphics.pipelineLayout);
        device.destroy(graphics.descriptorSetLayout);
        textureCloth.destroy();
        modelSphere.destroy();
        compute.destroy();
    }

    // Enable physical device features required for this example
    virtual void getEnabledFeatures() {
        if (deviceFeatures.samplerAnisotropy) {
            enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
    };

    void loadAssets() override {
        textureCloth.loadFromFile(context, getAssetPath() + "textures/vulkan_cloth_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
        modelSphere.loadFromFile(context, getAssetPath() + "models/geosphere.obj", vertexLayout, compute.ubo.sphereRadius * 0.05f);
    }

    static constexpr auto COMPUTE_STAGE = vk::PipelineStageFlagBits::eComputeShader;
    static constexpr auto VERTEX_STAGE = vk::PipelineStageFlagBits::eVertexInput;

    void addComputeToComputeBarrier(const vk::CommandBuffer& commandBuffer) {
        vk::BufferMemoryBarrier bufferBarrier;
        bufferBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bufferBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        bufferBarrier.srcQueueFamilyIndex = context.queueFamilyIndices.compute;
        bufferBarrier.dstQueueFamilyIndex = context.queueFamilyIndices.compute;
        bufferBarrier.size = VK_WHOLE_SIZE;
        std::vector<vk::BufferMemoryBarrier> bufferBarriers;
        bufferBarrier.buffer = compute.storageBuffers.input.buffer;
        bufferBarriers.push_back(bufferBarrier);
        bufferBarrier.buffer = compute.storageBuffers.output.buffer;
        bufferBarriers.push_back(bufferBarrier);
        commandBuffer.pipelineBarrier(COMPUTE_STAGE, COMPUTE_STAGE, vk::DependencyFlags{}, nullptr, bufferBarriers, nullptr);
    }

    void addComputeToGraphicsBarrier(const vk::CommandBuffer& commandBuffer) {
        vk::BufferMemoryBarrier bufferBarrier;
        bufferBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bufferBarrier.dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead;
        bufferBarrier.srcQueueFamilyIndex = context.queueFamilyIndices.compute;
        bufferBarrier.dstQueueFamilyIndex = context.queueFamilyIndices.graphics;
        bufferBarrier.size = VK_WHOLE_SIZE;
        std::vector<vk::BufferMemoryBarrier> bufferBarriers;
        bufferBarrier.buffer = compute.storageBuffers.input.buffer;
        bufferBarriers.push_back(bufferBarrier);
        bufferBarrier.buffer = compute.storageBuffers.output.buffer;
        bufferBarriers.push_back(bufferBarrier);
        commandBuffer.pipelineBarrier(COMPUTE_STAGE, VERTEX_STAGE, vk::DependencyFlags{}, nullptr, bufferBarriers, nullptr);
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

    void updateDrawCommandBuffer(const vk::CommandBuffer& commandBuffer) override {
        commandBuffer.setViewport(0, vks::util::viewport(size));
        commandBuffer.setScissor(0, vks::util::rect2D(size));

        // Render sphere
        if (sceneSetup == 0) {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics.pipelines.sphere);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphics.pipelineLayout, 0, graphics.descriptorSet, nullptr);
            commandBuffer.bindIndexBuffer(modelSphere.indices.buffer, 0, vk::IndexType::eUint32);
            commandBuffer.bindVertexBuffers(0, modelSphere.vertices.buffer, { 0 });
            commandBuffer.drawIndexed(modelSphere.indexCount, 1, 0, 0, 0);
        }

        // Render cloth
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics.pipelines.cloth);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphics.pipelineLayout, 0, graphics.descriptorSet, nullptr);
        commandBuffer.bindIndexBuffer(graphics.indices.buffer, 0, vk::IndexType::eUint32);
        commandBuffer.bindVertexBuffers(0, compute.storageBuffers.output.buffer, { 0 });
        commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);

        //drawUI(drawCmdBuffers[i]);
    }

    void updateCommandBufferPreDraw(const vk::CommandBuffer& commandBuffer) override {
        addComputeToGraphicsBarrier(commandBuffer);
    }

    void updateCommandBufferPostDraw(const vk::CommandBuffer& commandBuffer) override {
        addGraphicsToComputeBarrier(commandBuffer);
    }

    // todo: check barriers (validation, separate compute queue)
    void buildComputeCommandBuffer() {
        for (uint32_t i = 0; i < 2; i++) {
            const auto& commandBuffer = compute.commandBuffers[i];
            commandBuffer.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eSimultaneousUse });
            addGraphicsToComputeBarrier(commandBuffer);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute.pipeline);
            uint32_t calculateNormals = 0;
            commandBuffer.pushConstants<uint32_t>(compute.pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, calculateNormals);

            // Dispatch the compute job
            const uint32_t iterations = 64;
            for (uint32_t j = 0; j < iterations; j++) {
                readSet = 1 - readSet;
                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute.pipelineLayout, 0, compute.descriptorSets[readSet], nullptr);

                if (j == iterations - 1) {
                    calculateNormals = 1;
                    commandBuffer.pushConstants<uint32_t>(compute.pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, calculateNormals);
                }
                commandBuffer.dispatch(cloth.gridsize.x / 10, cloth.gridsize.y / 10, 1);
                if (j != iterations - 1) {
                    addComputeToComputeBarrier(commandBuffer);
                }
            }

            addComputeToGraphicsBarrier(commandBuffer);
            commandBuffer.end();
        }

        synchronization.renderSignalSemaphores.push_back(compute.semaphores.ready);
        addRenderWaitSemaphore(compute.semaphores.complete);
    }

    // Setup and fill the compute shader storage buffers containing the particles
    void prepareStorageBuffers() {
        std::vector<Particle> particleBuffer(cloth.gridsize.x * cloth.gridsize.y);

        float dx = cloth.size.x / (cloth.gridsize.x - 1);
        float dy = cloth.size.y / (cloth.gridsize.y - 1);
        float du = 1.0f / (cloth.gridsize.x - 1);
        float dv = 1.0f / (cloth.gridsize.y - 1);

        switch (sceneSetup) {
            case 0: {
                // Horz. cloth falls onto sphere
                glm::mat4 transM = glm::translate(glm::mat4(1.0f), glm::vec3(-cloth.size.x / 2.0f, -2.0f, -cloth.size.y / 2.0f));
                for (uint32_t i = 0; i < cloth.gridsize.y; i++) {
                    for (uint32_t j = 0; j < cloth.gridsize.x; j++) {
                        particleBuffer[i + j * cloth.gridsize.y].pos = transM * glm::vec4(dx * j, 0.0f, dy * i, 1.0f);
                        particleBuffer[i + j * cloth.gridsize.y].vel = glm::vec4(0.0f);
                        particleBuffer[i + j * cloth.gridsize.y].uv = glm::vec4(1.0f - du * i, dv * j, 0.0f, 0.0f);
                    }
                }
                break;
            }
            case 1: {
                // Vert. Pinned cloth
                glm::mat4 transM = glm::translate(glm::mat4(1.0f), glm::vec3(-cloth.size.x / 2.0f, -cloth.size.y / 2.0f, 0.0f));
                for (uint32_t i = 0; i < cloth.gridsize.y; i++) {
                    for (uint32_t j = 0; j < cloth.gridsize.x; j++) {
                        particleBuffer[i + j * cloth.gridsize.y].pos = transM * glm::vec4(dx * j, dy * i, 0.0f, 1.0f);
                        particleBuffer[i + j * cloth.gridsize.y].vel = glm::vec4(0.0f);
                        particleBuffer[i + j * cloth.gridsize.y].uv = glm::vec4(du * j, dv * i, 0.0f, 0.0f);
                        // Pin some particles
                        particleBuffer[i + j * cloth.gridsize.y].pinned =
                            (i == 0) &&
                            ((j == 0) || (j == cloth.gridsize.x / 3) || (j == cloth.gridsize.x - cloth.gridsize.x / 3) || (j == cloth.gridsize.x - 1));
                        // Remove sphere
                        compute.ubo.spherePos.z = -10.0f;
                    }
                }
                break;
            }
        }

        // Staging
        // SSBO won't be changed on the host after upload so copy to device local memory
        auto usageFlags = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer;
        compute.storageBuffers.input = context.stageToDeviceBuffer(usageFlags, particleBuffer);
        compute.storageBuffers.output = context.stageToDeviceBuffer(usageFlags, particleBuffer);

        // Indices
        std::vector<uint32_t> indices;
        for (uint32_t y = 0; y < cloth.gridsize.y - 1; y++) {
            for (uint32_t x = 0; x < cloth.gridsize.x; x++) {
                indices.push_back((y + 1) * cloth.gridsize.x + x);
                indices.push_back((y)*cloth.gridsize.x + x);
            }
            // Primitive restart (signlaed by special value 0xFFFFFFFF)
            indices.push_back(0xFFFFFFFF);
        }
        uint32_t indexBufferSize = static_cast<uint32_t>(indices.size()) * sizeof(uint32_t);
        indexCount = static_cast<uint32_t>(indices.size());
        graphics.indices = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indices);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes{
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 3 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBuffer, 5 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 2 },
        };
        descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 3, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupLayoutsAndDescriptors() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };
        graphics.descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        graphics.pipelineLayout = device.createPipelineLayout({ {}, 1, &graphics.descriptorSetLayout });
        graphics.descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &graphics.descriptorSetLayout })[0];
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            vk::WriteDescriptorSet{ graphics.descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &graphics.uniformBuffer.descriptor },
            vk::WriteDescriptorSet{ graphics.descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &textureCloth.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder builder(device, graphics.pipelineLayout, renderPass);
        builder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        builder.inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleStrip;
        builder.loadShader(getAssetPath() + "shaders/computecloth/cloth.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/computecloth/cloth.frag.spv", vk::ShaderStageFlagBits::eFragment);
        builder.inputAssemblyState.primitiveRestartEnable = VK_TRUE;
        builder.vertexInputState.bindingDescriptions = {
            vk::VertexInputBindingDescription{ 0, sizeof(Particle) },
        };
        builder.vertexInputState.attributeDescriptions = {
            vk::VertexInputAttributeDescription{ 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Particle, pos) },
            vk::VertexInputAttributeDescription{ 1, 0, vk::Format::eR32G32Sfloat, offsetof(Particle, uv) },
            vk::VertexInputAttributeDescription{ 2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Particle, normal) },
        };
        graphics.pipelines.cloth = builder.create(pipelineCache);
        builder.destroyShaderModules();

        // Sphere rendering pipeline
        builder.inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;
        builder.inputAssemblyState.primitiveRestartEnable = VK_FALSE;
        builder.loadShader(getAssetPath() + "shaders/computecloth/sphere.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/computecloth/sphere.frag.spv", vk::ShaderStageFlagBits::eFragment);
        builder.vertexInputState.bindingDescriptions = { vertexLayout.generateBindingDescripton() };
        builder.vertexInputState.attributeDescriptions = vertexLayout.genrerateAttributeDescriptions();
        graphics.pipelines.sphere = builder.create(pipelineCache);
    }

    void prepareCompute() {
        compute.prepare(context);

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            vk::DescriptorSetLayoutBinding{ 2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute },
        };

        compute.descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        vk::PushConstantRange pushConstantRange{ vk::ShaderStageFlagBits::eCompute, 0, sizeof(uint32_t) };
        compute.pipelineLayout = device.createPipelineLayout({ {}, 1, &compute.descriptorSetLayout, 1, &pushConstantRange });

        compute.descriptorSets[0] = device.allocateDescriptorSets({ descriptorPool, 1, &compute.descriptorSetLayout })[0];
        compute.descriptorSets[1] = device.allocateDescriptorSets({ descriptorPool, 1, &compute.descriptorSetLayout })[0];

        // Create two descriptor sets with input and output buffers switched
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            { compute.descriptorSets[0], 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &compute.storageBuffers.input.descriptor },
            { compute.descriptorSets[0], 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &compute.storageBuffers.output.descriptor },
            { compute.descriptorSets[0], 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &compute.uniformBuffer.descriptor },
            { compute.descriptorSets[1], 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &compute.storageBuffers.output.descriptor },
            { compute.descriptorSets[1], 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &compute.storageBuffers.input.descriptor },
            { compute.descriptorSets[1], 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &compute.uniformBuffer.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);

        // Create pipeline
        {
            vk::ComputePipelineCreateInfo computePipelineCreateInfo;
            computePipelineCreateInfo.layout = compute.pipelineLayout;
            computePipelineCreateInfo.stage =
                vks::shaders::loadShader(device, getAssetPath() + "shaders/computecloth/cloth.comp.spv", vk::ShaderStageFlagBits::eCompute);
            compute.pipeline = device.createComputePipeline(pipelineCache, computePipelineCreateInfo);
        }

        // Create a command buffer for compute operations
        compute.commandBuffers = device.allocateCommandBuffers({ compute.commandPool, vk::CommandBufferLevel::ePrimary, 2 });
        // Build a single command buffer containing the compute dispatch commands
        buildComputeCommandBuffer();
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Compute shader uniform buffer block
        compute.uniformBuffer = context.createUniformBuffer(compute.ubo);

        // Initial values
        float dx = cloth.size.x / (cloth.gridsize.x - 1);
        float dy = cloth.size.y / (cloth.gridsize.y - 1);

        compute.ubo.restDistH = dx;
        compute.ubo.restDistV = dy;
        compute.ubo.restDistD = sqrtf(dx * dx + dy * dy);
        compute.ubo.particleCount = cloth.gridsize;

        updateComputeUBO();

        // Vertex shader uniform buffer block
        graphics.uniformBuffer = context.createUniformBuffer(graphics.ubo);
        updateGraphicsUBO();
    }

    void updateComputeUBO() {
        if (!paused) {
            compute.ubo.deltaT = 0.000005f;
            // todo: base on frametime
            //compute.ubo.deltaT = frameTimer * 0.0075f;

            if (simulateWind) {
                std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
                std::uniform_real_distribution<float> rd(1.0f, 6.0f);
                compute.ubo.gravity.x = cos(glm::radians(-timer * 360.0f)) * (rd(rndEngine) - rd(rndEngine));
                compute.ubo.gravity.z = sin(glm::radians(timer * 360.0f)) * (rd(rndEngine) - rd(rndEngine));
            } else {
                compute.ubo.gravity.x = 0.0f;
                compute.ubo.gravity.z = 0.0f;
            }
        } else {
            compute.ubo.deltaT = 0.0f;
        }
        memcpy(compute.uniformBuffer.mapped, &compute.ubo, sizeof(compute.ubo));
    }

    void updateGraphicsUBO() {
        graphics.ubo.projection = camera.matrices.perspective;
        graphics.ubo.view = camera.matrices.view;
        memcpy(graphics.uniformBuffer.mapped, &graphics.ubo, sizeof(graphics.ubo));
    }

    void draw() {
        compute.submit(compute.commandBuffers[readSet]);
        Parent::draw();
    }

    void prepare() {
        VulkanExampleBase::prepare();
        prepareStorageBuffers();
        prepareUniformBuffers();
        setupDescriptorPool();
        setupLayoutsAndDescriptors();
        preparePipelines();
        prepareCompute();
        buildCommandBuffers();
        // Release the storage buffers to the compute queue, and signal the compute ready semaphore
        context.withPrimaryCommandBuffer([&](const vk::CommandBuffer& commandBuffer) { updateCommandBufferPostDraw(commandBuffer); }, compute.semaphores.ready);
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();

        updateComputeUBO();
    }

    virtual void viewChanged() {
        updateGraphicsUBO();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {
        if (overlay->header("Settings")) {
            overlay->checkBox("Simulate wind", &simulateWind);
        }
    }
};

VULKAN_EXAMPLE_MAIN()