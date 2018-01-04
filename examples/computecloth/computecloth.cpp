/*
* Vulkan Example - Compute shader sloth simulation
*
* Updated compute shader by Lukas Bergdoll (https://github.com/Voultapher)
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>

#define ENABLE_VALIDATION false

class VulkanExample : public vkx::ExampleBase {
public:
    uint32_t sceneSetup = 0;
    uint32_t readSet = 0;
    uint32_t indexCount;
    bool simulateWind = false;

    vks::texture::Texture2D textureCloth;

    vks::model::VertexLayout vertexLayout{ {
        vks::model::VERTEX_COMPONENT_POSITION,
        vks::model::VERTEX_COMPONENT_UV,
        vks::model::VERTEX_COMPONENT_NORMAL,
    } };
    vks::model::Model modelSphere;

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
    struct {
        struct StorageBuffers {
            vks::Buffer input;
            vks::Buffer output;
        } storageBuffers;
        vks::Buffer uniformBuffer;
        vk::Queue queue;
        vk::CommandPool commandPool;
        std::vector<vk::CommandBuffer> commandBuffers;
        vk::Fence fence;
        vk::DescriptorSetLayout descriptorSetLayout;
        std::array<vk::DescriptorSet, 2> descriptorSets;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
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
    } compute;

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

    VulkanExample() {
        title = "Compute shader cloth simulation";
        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(60.0f, (float)size.width / (float)size.height, 0.1f, 512.0f);
        camera.setRotation(glm::vec3(-30.0f, -45.0f, 0.0f));
        camera.setTranslation(glm::vec3(0.0f, 0.0f, -3.5f));
        settings.overlay = true;
        srand((unsigned int)time(NULL));
    }

    ~VulkanExample() {
        // Graphics
        graphics.indices.destroy();
        graphics.uniformBuffer.destroy();
        device.destroyPipeline(graphics.pipelines.cloth, nullptr);
        device.destroyPipeline(graphics.pipelines.sphere, nullptr);
        device.destroyPipelineLayout(graphics.pipelineLayout, nullptr);
        device.destroyDescriptorSetLayout(graphics.descriptorSetLayout, nullptr);
        textureCloth.destroy();
        modelSphere.destroy();

        // Compute
        compute.storageBuffers.input.destroy();
        compute.storageBuffers.output.destroy();
        compute.uniformBuffer.destroy();
        device.destroyPipelineLayout(compute.pipelineLayout, nullptr);
        device.destroyDescriptorSetLayout(compute.descriptorSetLayout, nullptr);
        device.destroyPipeline(compute.pipeline, nullptr);
        device.destroyFence(compute.fence, nullptr);
        device.destroyCommandPool(compute.commandPool, nullptr);
    }

    // Enable physical device features required for this example
    void getEnabledFeatures() override {
        if (context.deviceFeatures.samplerAnisotropy) {
            context.enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
    };

    void loadAssets() override {
        textureCloth.loadFromFile(context, getAssetPath() + "textures/vulkan_cloth_rgba.ktx", vF::eR8G8B8A8Unorm);
        modelSphere.loadFromFile(context, getAssetPath() + "models/geosphere.obj", vertexLayout, compute.ubo.sphereRadius * 0.05f);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& commandBuffer) override {
        commandBuffer.setViewport(0, viewport());
        commandBuffer.setScissor(0, scissor());

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
    }
#if 0
    void buildCommandBuffers() override {

        if (!drawCmdBuffers.empty()) {
            context.trashCommandBuffers(drawCmdBuffers);
        }

        drawCmdBuffers.clear();
        drawCmdBuffers = device.allocateCommandBuffers({ cmdPool, vk::CommandBufferLevel::ePrimary, swapChain.imageCount });

        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = vks::util::clearColor( glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f } );
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent = size;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = framebuffers[i];

            drawCmdBuffers[i].begin(cmdBufInfo);

            // Draw the particle system using the update vertex buffer
            drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            updateDrawCommandBuffer(drawCmdBuffers[i]);
            drawCmdBuffers[i].endRenderPass();
            drawCmdBuffers[i].end();
        }
    }
#endif
    // todo: check barriers (validation, separate compute queue)
    void buildComputeCommandBuffer() {
        for (uint32_t i = 0; i < 2; i++) {
            const auto& cmdBuf = compute.commandBuffers[i];
            cmdBuf.begin({ vk::CommandBufferUsageFlagBits{} });

            std::vector<vk::BufferMemoryBarrier> bufferBarriers;
            {
                vk::BufferMemoryBarrier bufferBarrier;
                bufferBarrier.srcAccessMask = vAF::eShaderRead;
                bufferBarrier.dstAccessMask = vAF::eShaderWrite;
                bufferBarrier.srcQueueFamilyIndex = context.queueIndices.graphics;
                bufferBarrier.dstQueueFamilyIndex = context.queueIndices.compute;
                bufferBarrier.size = VK_WHOLE_SIZE;

                bufferBarrier.buffer = compute.storageBuffers.input.buffer;
                bufferBarriers.push_back(bufferBarrier);
                bufferBarrier.buffer = compute.storageBuffers.output.buffer;
                bufferBarriers.push_back(bufferBarrier);
            }
            cmdBuf.pipelineBarrier(vPS::eComputeShader, vPS::eComputeShader, {}, nullptr, bufferBarriers, nullptr);
            cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, compute.pipeline);
            uint32_t calculateNormals = 0;
            cmdBuf.pushConstants<uint32_t>(compute.pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, calculateNormals);

            for (auto& barrier : bufferBarriers) {
                barrier.srcAccessMask = vAF::eShaderWrite;
                barrier.dstAccessMask = vAF::eShaderRead;
                barrier.srcQueueFamilyIndex = context.queueIndices.compute;
                barrier.dstQueueFamilyIndex = context.queueIndices.graphics;
            }

            // Dispatch the compute job
            const uint32_t iterations = 64;
            for (uint32_t j = 0; j < iterations; j++) {
                readSet = 1 - readSet;
                cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute.pipelineLayout, 0, compute.descriptorSets[readSet], nullptr);
                if (j == iterations - 1) {
                    calculateNormals = 1;
                    cmdBuf.pushConstants<uint32_t>(compute.pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, calculateNormals);
                }
                cmdBuf.dispatch(cloth.gridsize.x / 10, cloth.gridsize.y / 10, 1);
                cmdBuf.pipelineBarrier(vPS::eComputeShader, vPS::eComputeShader, {}, nullptr, bufferBarriers, nullptr);
            }

            for (auto& barrier : bufferBarriers) {
                barrier.srcAccessMask = vAF::eShaderWrite;
                barrier.dstAccessMask = vAF::eShaderRead;
                barrier.srcQueueFamilyIndex = context.queueIndices.compute;
                barrier.dstQueueFamilyIndex = context.queueIndices.graphics;
            }

            cmdBuf.pipelineBarrier(vPS::eComputeShader, vPS::eComputeShader, {}, nullptr, bufferBarriers, nullptr);
            cmdBuf.end();
        }
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

        vk::DeviceSize storageBufferSize = particleBuffer.size() * sizeof(Particle);

        // SSBO won't be changed on the host after upload so copy to device local memory
        compute.storageBuffers.input = context.stageToDeviceBuffer(vBU::eVertexBuffer | vBU::eStorageBuffer, particleBuffer);
        compute.storageBuffers.output = context.stageToDeviceBuffer(vBU::eVertexBuffer | vBU::eStorageBuffer, particleBuffer);

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
        graphics.indices = context.stageToDeviceBuffer(vBU::eIndexBuffer, indices);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vDT::eUniformBuffer, 3 },
            { vDT::eStorageBuffer, 4 },
            { vDT::eCombinedImageSampler, 2 },
        };
        descriptorPool = device.createDescriptorPool({ {}, 3, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupLayoutsAndDescriptors() {
        // Set layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            { 0, vDT::eUniformBuffer, 1, vSS::eVertex },
            { 1, vDT::eCombinedImageSampler, 1, vSS::eFragment },
        };

        graphics.descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        graphics.pipelineLayout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, 1, &graphics.descriptorSetLayout });

        // Set
        graphics.descriptorSet = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool, 1, &graphics.descriptorSetLayout })[0];
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            { graphics.descriptorSet, 0, 0, 1, vDT::eUniformBuffer, nullptr, &graphics.uniformBuffer.descriptor },
            { graphics.descriptorSet, 1, 0, 1, vDT::eCombinedImageSampler, &textureCloth.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {

        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, graphics.pipelineLayout, renderPass };
        pipelineBuilder.inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleStrip;
        pipelineBuilder.inputAssemblyState.primitiveRestartEnable = VK_TRUE;
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        // Binding description
        pipelineBuilder.vertexInputState.bindingDescriptions = {
            { 0, sizeof(Particle), vk::VertexInputRate::eVertex }
        };

        pipelineBuilder.vertexInputState.attributeDescriptions = {
            { 0, 0, vF::eR32G32B32Sfloat, offsetof(Particle, pos) },
            { 1, 0, vF::eR32G32Sfloat, offsetof(Particle, uv) },
            { 2, 0, vF::eR32G32B32Sfloat, offsetof(Particle, normal) },
        };

        pipelineBuilder.loadShader(getAssetPath() + "shaders/computecloth/cloth.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computecloth/cloth.frag.spv", vSS::eFragment);
        graphics.pipelines.cloth = pipelineBuilder.create(context.pipelineCache);
        pipelineBuilder.destroyShaderModules();

        // Sphere rendering pipeline
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computecloth/sphere.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computecloth/sphere.frag.spv", vSS::eFragment);
        pipelineBuilder.vertexInputState = {};
        pipelineBuilder.vertexInputState.appendVertexLayout(vertexLayout);
        pipelineBuilder.inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;
        graphics.pipelines.sphere = pipelineBuilder.create(context.pipelineCache);
    }

    void prepareCompute() {
        // Create a compute capable device queue
        compute.queue = device.getQueue(context.queueIndices.compute, 0);

        // Create compute pipeline
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings  {
            { 0, vDT::eStorageBuffer, 1, vSS::eCompute },
            { 1, vDT::eStorageBuffer, 1, vSS::eCompute },
            { 2, vDT::eUniformBuffer, 1, vSS::eCompute },
        };

        compute.descriptorSetLayout = device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        // Push constants used to pass some parameters
        vk::PushConstantRange pushConstantRange{ vSS::eCompute, 0, sizeof(uint32_t) };

        compute.pipelineLayout = device.createPipelineLayout({ {}, 1, &compute.descriptorSetLayout, 1, &pushConstantRange });

        vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, 1, &compute.descriptorSetLayout };

        // Create two descriptor sets with input and output buffers switched
        compute.descriptorSets[0] = device.allocateDescriptorSets(allocInfo)[0];
        compute.descriptorSets[1] = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets{
            { compute.descriptorSets[0], 0, 0, 1, vDT::eStorageBuffer, nullptr, &compute.storageBuffers.input.descriptor },
            { compute.descriptorSets[0], 1, 0, 1, vDT::eStorageBuffer, nullptr, &compute.storageBuffers.output.descriptor },
            { compute.descriptorSets[0], 2, 0, 1, vDT::eUniformBuffer, nullptr, &compute.uniformBuffer.descriptor },

            { compute.descriptorSets[1], 0, 0, 1, vDT::eStorageBuffer, nullptr, &compute.storageBuffers.output.descriptor },
            { compute.descriptorSets[1], 1, 0, 1, vDT::eStorageBuffer, nullptr, &compute.storageBuffers.input.descriptor },
            { compute.descriptorSets[1], 2, 0, 1, vDT::eUniformBuffer, nullptr, &compute.uniformBuffer.descriptor },
        };

        device.updateDescriptorSets(computeWriteDescriptorSets, nullptr);

        // Create pipeline
        vk::ComputePipelineCreateInfo computePipelineCreateInfo;
        computePipelineCreateInfo.layout = compute.pipelineLayout;
        computePipelineCreateInfo.stage = vks::shaders::loadShader(device, getAssetPath() + "shaders/computecloth/cloth.comp.spv", vSS::eCompute);
        compute.pipeline = device.createComputePipeline(context.pipelineCache, computePipelineCreateInfo);
        device.destroyShaderModule(computePipelineCreateInfo.stage.module);

        // Separate command pool as queue family for compute may be different than graphics
        compute.commandPool = device.createCommandPool({ vk::CommandPoolCreateFlagBits::eResetCommandBuffer, context.queueIndices.compute });
        // Create a command buffer for compute operations
        compute.commandBuffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ compute.commandPool, vk::CommandBufferLevel::ePrimary, 2 });
        // Fence for compute CB sync
        compute.fence = device.createFence({ vk::FenceCreateFlagBits::eSignaled });

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

            std::mt19937 rg((unsigned)time(nullptr));
            std::uniform_real_distribution<float> rd(1.0f, 6.0f);

            if (simulateWind) {
                compute.ubo.gravity.x = cos(glm::radians(-timer * 360.0f)) * (rd(rg) - rd(rg));
                compute.ubo.gravity.z = sin(glm::radians(timer * 360.0f)) * (rd(rg) - rd(rg));
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

    void draw() override {
        // Submit graphics commands
        ExampleBase::draw();

        device.waitForFences(compute.fence, VK_TRUE, UINT64_MAX);
        device.resetFences(compute.fence);

        vk::SubmitInfo computeSubmitInfo;
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &compute.commandBuffers[readSet];
        compute.queue.submit(computeSubmitInfo, compute.fence);
    }

    void prepare() override {
        ExampleBase::prepare();
        prepareStorageBuffers();
        prepareUniformBuffers();
        setupDescriptorPool();
        setupLayoutsAndDescriptors();
        preparePipelines();
        prepareCompute();
        buildCommandBuffers();
        prepared = true;
    }

    void render() override {
        if (!prepared)
            return;
        draw();

        updateComputeUBO();
    }

    void viewChanged() override { updateGraphicsUBO(); }

    void OnUpdateUIOverlay() override {
        if (ui.header("Settings")) {
            ui.checkBox("Simulate wind", &simulateWind);
        }
    }
};

VULKAN_EXAMPLE_MAIN()
