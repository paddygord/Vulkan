/*
* Vulkan Example - Compute shader culling and LOD using indirect rendering
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
*/

#include <vkx/vulkanExampleBase.hpp>
#include <vkx/compute.hpp>
#include <vkx/frustum.hpp>
#include <vkx/model.hpp>
#include <khrpp/vks/shaders.hpp>

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1
#define ENABLE_VALIDATION false

// Total number of objects (^3) in the scene
#if defined(__ANDROID__)
#define OBJECT_COUNT 32
#else
#define OBJECT_COUNT 64
#endif

#define MAX_LOD_LEVEL 5

class VulkanExample : public VulkanExampleBase {
    using Parent = VulkanExampleBase;

public:
    bool fixedFrustum = false;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
    } };

    struct {
        vkx::model::Model lodObject;
    } models;

    // Per-instance data block
    struct InstanceData {
        glm::vec3 pos;
        float scale;
    };

    // Contains the instanced data
    vks::Buffer instanceBuffer;
    // Contains the indirect drawing commands
    vks::Buffer indirectCommandsBuffer;
    vks::Buffer indirectDrawCountBuffer;

    // Indirect draw statistics (updated via compute)
    struct IndirectStats {
        uint32_t drawCount;                    // Total number of indirect draw counts to be issued
        uint32_t lodCount[MAX_LOD_LEVEL + 1];  // Statistics for number of draws per LOD level (written by compute shader)
    } indirectStats;

    // Store the indirect draw commands containing index offsets and instance count per object
    std::vector<vk::DrawIndexedIndirectCommand> indirectCommands;

    struct {
        glm::mat4 projection;
        glm::mat4 modelview;
        glm::vec4 cameraPos;
        glm::vec4 frustumPlanes[6];
    } uboScene;

    struct {
        vks::Buffer scene;
    } uniformData;

    struct {
        vk::Pipeline plants;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    // Resources for the compute part of the example
    struct Compute : vkx::Compute {
        vks::Buffer lodLevelsBuffers;                 // Contains index start and counts for the different lod levels
        vk::CommandBuffer commandBuffer;              // Command buffer storing the dispatch commands and barriers
        vk::DescriptorSetLayout descriptorSetLayout;  // Compute shader binding layout
        vk::DescriptorSet descriptorSet;              // Compute shader bindings
        vk::PipelineLayout pipelineLayout;            // Layout of the compute pipeline
        vk::Pipeline pipeline;                        // Compute pipeline for updating particle positions
    } compute;

    // View frustum for culling invisible objects
    vkx::Frustum frustum;

    uint32_t objectCount = 0;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Vulkan Example - Compute cull and lod";
        camera.type = Camera::CameraType::firstperson;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
        camera.setTranslation(glm::vec3(0.5f, 0.0f, 0.0f));
        camera.movementSpeed = 5.0f;
        settings.overlay = true;
        defaultClearColor = vks::util::clearColor({ 0.18f, 0.27f, 0.5f, 0.0f });
        memset(&indirectStats, 0, sizeof(indirectStats));
    }

    ~VulkanExample() {
        device.destroy(pipelines.plants);
        device.destroy(pipelineLayout);
        device.destroy(descriptorSetLayout);

        models.lodObject.destroy();
        instanceBuffer.destroy();
        indirectCommandsBuffer.destroy();
        uniformData.scene.destroy();
        indirectDrawCountBuffer.destroy();
        compute.lodLevelsBuffers.destroy();

        device.destroy(compute.pipelineLayout);
        device.destroy(compute.descriptorSetLayout);
        device.destroy(compute.pipeline);
        compute.destroy();
    }

    void getEnabledFeatures() override {
        // Enable multi draw indirect if supported
        if (deviceFeatures.multiDrawIndirect) {
            enabledFeatures.multiDrawIndirect = VK_TRUE;
        }
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& commandBuffer) override {
        commandBuffer.setViewport(0, vks::util::viewport(size));
        commandBuffer.setScissor(0, vks::util::rect2D(size));

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);

        // Mesh containing the LODs
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.plants);

        commandBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, models.lodObject.vertices.buffer, { 0 });
        commandBuffer.bindVertexBuffers(INSTANCE_BUFFER_BIND_ID, instanceBuffer.buffer, { 0 });
        commandBuffer.bindIndexBuffer(models.lodObject.indices.buffer, 0, vk::IndexType::eUint32);

        if (deviceFeatures.multiDrawIndirect) {
            commandBuffer.drawIndexedIndirect(indirectCommandsBuffer.buffer, 0, indirectCommands.size(), sizeof(vk::DrawIndexedIndirectCommand));
        } else {
            // If multi draw is not available, we must issue separate draw commands
            for (auto j = 0; j < indirectCommands.size(); j++) {
                commandBuffer.drawIndexedIndirect(indirectCommandsBuffer.buffer, j * sizeof(vk::DrawIndexedIndirectCommand), 1,
                                                  sizeof(vk::DrawIndexedIndirectCommand));
            }
        }

        //drawUI(drawCmdBuffers[i]);
    }

    void loadAssets() {
        models.lodObject.loadFromFile(context, getAssetPath() + "models/suzanne_lods.dae", vertexLayout, 0.1f);
    }

    void addGraphicsToComputeBarrier(const vk::CommandBuffer& commandBuffer) {
        if (context.queueFamilyIndices.graphics != context.queueFamilyIndices.compute) {
            vk::BufferMemoryBarrier bufferBarrier{ vk::AccessFlagBits::eIndirectCommandRead,
                                                   vk::AccessFlagBits::eShaderWrite,
                                                   context.queueFamilyIndices.graphics,
                                                   context.queueFamilyIndices.compute,
                                                   indirectCommandsBuffer.buffer,
                                                   0,
                                                   VK_WHOLE_SIZE };
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eDrawIndirect, vk::PipelineStageFlagBits::eComputeShader, {}, nullptr, bufferBarrier,
                                          nullptr);
        }
    }

    void addComputeToGraphicsBarrier(const vk::CommandBuffer& commandBuffer) {
        if (context.queueFamilyIndices.graphics != context.queueFamilyIndices.compute) {
            vk::BufferMemoryBarrier bufferBarrier{ vk::AccessFlagBits::eShaderWrite,
                                                   vk::AccessFlagBits::eIndirectCommandRead,
                                                   context.queueFamilyIndices.compute,
                                                   context.queueFamilyIndices.graphics,
                                                   indirectCommandsBuffer.buffer,
                                                   0,
                                                   VK_WHOLE_SIZE };
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eDrawIndirect, {}, nullptr, bufferBarrier,
                                          nullptr);
        }
    }

    void buildComputeCommandBuffer() {
        compute.commandBuffer.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eSimultaneousUse });

        // Add memory barrier to ensure that the indirect commands have been consumed before the compute shader updates them
        addGraphicsToComputeBarrier(compute.commandBuffer);

        compute.commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute.pipeline);
        compute.commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute.pipelineLayout, 0, compute.descriptorSet, nullptr);

        // Dispatch the compute job
        // The compute shader will do the frustum culling and adjust the indirect draw calls depending on object visibility.
        // It also determines the lod to use depending on distance to the viewer.
        compute.commandBuffer.dispatch(objectCount / 16, 1, 1);

        // Add memory barrier to ensure that the compute shader has finished writing the indirect command buffer before it's consumed
        addComputeToGraphicsBarrier(compute.commandBuffer);

        // todo: barrier for indirect stats buffer?
        compute.commandBuffer.end();
    }

    void setupDescriptorPool() {
        // Example uses one ubo
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eUniformBuffer, 2 },
            { vk::DescriptorType::eStorageBuffer, 4 },
        };

        descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 2, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0: Vertex shader uniform buffer
            { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
        };

        descriptorSetLayout = device.createDescriptorSetLayout({ {}, static_cast<uint32_t>(setLayoutBindings.size()), setLayoutBindings.data() });
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        descriptorSet = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool, 1, &descriptorSetLayout })[0];
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            // Binding 0: Vertex shader uniform buffer
            { descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.scene.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder builder{ device, pipelineLayout, renderPass };
        builder.rasterizationState.frontFace = vk::FrontFace::eClockwise;

        builder.vertexInputState.bindingDescriptions = {
            { VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex },
            { INSTANCE_BUFFER_BIND_ID, sizeof(InstanceData), vk::VertexInputRate::eInstance },
        };

        builder.vertexInputState.attributeDescriptions = {
            // Location 0 : Position
            { 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, 0 },
            // Location 1 : Normal
            { 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3 },
            // Location 2 : Color
            { 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, sizeof(float) * 6 },

            // Instanced attributes
            // Location 4: Position
            { 4, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(InstanceData, pos) },
            // Location 5: Scale
            { 5, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32Sfloat, offsetof(InstanceData, scale) },
        };

        builder.loadShader(getAssetPath() + "shaders/computecullandlod/indirectdraw.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/computecullandlod/indirectdraw.frag.spv", vk::ShaderStageFlagBits::eFragment);

        // Indirect (and instanced) pipeline for the plants
        pipelines.plants = builder.create(pipelineCache);
    }

    void prepareBuffers() {
        objectCount = OBJECT_COUNT * OBJECT_COUNT * OBJECT_COUNT;

        std::vector<InstanceData> instanceData(objectCount);
        indirectCommands.resize(objectCount);
        // Indirect draw commands
        for (uint32_t x = 0; x < OBJECT_COUNT; x++) {
            for (uint32_t y = 0; y < OBJECT_COUNT; y++) {
                for (uint32_t z = 0; z < OBJECT_COUNT; z++) {
                    uint32_t index = x + y * OBJECT_COUNT + z * OBJECT_COUNT * OBJECT_COUNT;
                    indirectCommands[index].instanceCount = 1;
                    indirectCommands[index].firstInstance = index;
                    // firstIndex and indexCount are written by the compute shader
                }
            }
        }
        indirectStats.drawCount = static_cast<uint32_t>(indirectCommands.size());
        indirectCommandsBuffer =
            context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer, indirectCommands);
        indirectDrawCountBuffer =
            context.createBuffer(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
                                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, sizeof(indirectStats));

        // Map for host access
        indirectDrawCountBuffer.map();

        // Instance data
        for (uint32_t x = 0; x < OBJECT_COUNT; x++) {
            for (uint32_t y = 0; y < OBJECT_COUNT; y++) {
                for (uint32_t z = 0; z < OBJECT_COUNT; z++) {
                    uint32_t index = x + y * OBJECT_COUNT + z * OBJECT_COUNT * OBJECT_COUNT;
                    instanceData[index].pos = glm::vec3((float)x, (float)y, (float)z) - glm::vec3((float)OBJECT_COUNT / 2.0f);
                    instanceData[index].scale = 2.0f;
                }
            }
        }

        instanceBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer, instanceData);

        // Shader storage buffer containing index offsets and counts for the LODs
        struct LOD {
            uint32_t firstIndex;
            uint32_t indexCount;
            float distance;
            float _pad0;
        };
        std::vector<LOD> LODLevels;
        uint32_t n = 0;
        for (auto modelPart : models.lodObject.parts) {
            LOD lod;
            lod.firstIndex = modelPart.indexBase;   // First index for this LOD
            lod.indexCount = modelPart.indexCount;  // Index count for this LOD
            lod.distance = 5.0f + n * 5.0f;         // Starting distance (to viewer) for this LOD
            n++;
            LODLevels.push_back(lod);
        }

        compute.lodLevelsBuffers = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eStorageBuffer, LODLevels);

        // Scene uniform buffer
        uniformData.scene = context.createUniformBuffer(uboScene);
        updateUniformBuffer(true);
    }

    void prepareCompute() {
        // Create a compute capable device queue
        compute.prepare(context);

        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines even if they use the same queue (family index)

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0: Instance input data buffer
            { 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            // Binding 1: Indirect draw command output buffer (input)
            { 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            // Binding 2: Uniform buffer with global matrices (input)
            { 2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            // Binding 3: Indirect draw stats (output)
            { 3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
            // Binding 4: LOD info (input)
            { 4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
        };

        compute.descriptorSetLayout = device.createDescriptorSetLayout({ {}, static_cast<uint32_t>(setLayoutBindings.size()), setLayoutBindings.data() });
        compute.pipelineLayout = device.createPipelineLayout({ {}, 1, &compute.descriptorSetLayout });
        compute.descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &compute.descriptorSetLayout })[0];

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets = {
            // Binding 0: Instance input data buffer
            { compute.descriptorSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &instanceBuffer.descriptor },
            // Binding 1: Indirect draw command output buffer
            { compute.descriptorSet, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &indirectCommandsBuffer.descriptor },
            // Binding 2: Uniform buffer with global matrices
            { compute.descriptorSet, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.scene.descriptor },
            // Binding 3: Atomic counter (written in shader)
            { compute.descriptorSet, 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &indirectDrawCountBuffer.descriptor },
            // Binding 4: LOD info
            { compute.descriptorSet, 4, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &compute.lodLevelsBuffers.descriptor },
        };

        device.updateDescriptorSets(computeWriteDescriptorSets, nullptr);

        // Create pipeline
        vk::ComputePipelineCreateInfo computePipelineCreateInfo;
        computePipelineCreateInfo.layout = compute.pipelineLayout;
        computePipelineCreateInfo.stage =
            vks::shaders::loadShader(device, getAssetPath() + "shaders/computecullandlod/cull.comp.spv", vk::ShaderStageFlagBits::eCompute);

        // Use specialization constants to pass max. level of detail (determined by no. of meshes)
        vk::SpecializationMapEntry specializationEntry{};
        specializationEntry.constantID = 0;
        specializationEntry.offset = 0;
        specializationEntry.size = sizeof(uint32_t);

        uint32_t specializationData = static_cast<uint32_t>(models.lodObject.parts.size()) - 1;

        vk::SpecializationInfo specializationInfo;
        specializationInfo.mapEntryCount = 1;
        specializationInfo.pMapEntries = &specializationEntry;
        specializationInfo.dataSize = sizeof(specializationData);
        specializationInfo.pData = &specializationData;

        computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;

        compute.pipeline = device.createComputePipeline(pipelineCache, computePipelineCreateInfo);

        // Create a command buffer for compute operations
        compute.commandBuffer = device.allocateCommandBuffers({ compute.commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];

        // Build a single command buffer containing the compute dispatch commands
        buildComputeCommandBuffer();

        synchronization.renderSignalSemaphores.push_back(compute.semaphores.ready);
        addRenderWaitSemaphore(compute.semaphores.complete, vk::PipelineStageFlagBits::eVertexInput);
    }

    void updateUniformBuffer(bool viewChanged) {
        if (viewChanged) {
            uboScene.projection = camera.matrices.perspective;
            uboScene.modelview = camera.matrices.view;
            if (!fixedFrustum) {
                uboScene.cameraPos = glm::vec4(camera.position, 1.0f) * -1.0f;
                frustum.update(uboScene.projection * uboScene.modelview);
                memcpy(uboScene.frustumPlanes, frustum.planes.data(), sizeof(glm::vec4) * 6);
            }
        }

        memcpy(uniformData.scene.mapped, &uboScene, sizeof(uboScene));
    }

    void draw() override {
        // Submit compute shader for frustum culling
        compute.submit(compute.commandBuffer);
        // Submit graphics command buffer
        Parent::draw();
        // Get draw count from compute
        memcpy(&indirectStats, indirectDrawCountBuffer.mapped, sizeof(indirectStats));
    }

    void prepare() override {
        VulkanExampleBase::prepare();
        prepareBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        prepareCompute();
        buildCommandBuffers();
        context.withPrimaryCommandBuffer([&](const vk::CommandBuffer& commandBuffer) { updateCommandBufferPostDraw(commandBuffer); }, compute.semaphores.ready);
        prepared = true;
    }

    void viewChanged() override {
        updateUniformBuffer(true);
    }

    void OnUpdateUIOverlay(vks::UIOverlay* overlay) override {
        if (overlay->header("Settings")) {
            if (overlay->checkBox("Freeze frustum", &fixedFrustum)) {
                updateUniformBuffer(true);
            }
        }
        if (overlay->header("Statistics")) {
            overlay->text("Visible objects: %d", indirectStats.drawCount);
            for (uint32_t i = 0; i < MAX_LOD_LEVEL + 1; i++) {
                overlay->text("LOD %d: %d", i, indirectStats.lodCount[i]);
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()