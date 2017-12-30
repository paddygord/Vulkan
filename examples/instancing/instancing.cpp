/*
* Vulkan Example - Instanced mesh rendering, uses a separate vertex buffer for instanced data
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"

#define INSTANCE_COUNT 2048

// Vertex layout for this example
vks::model::VertexLayout vertexLayout{
    { vks::model::Component::VERTEX_COMPONENT_POSITION, vks::model::Component::VERTEX_COMPONENT_NORMAL,
      vks::model::Component::VERTEX_COMPONENT_UV, vks::model::Component::VERTEX_COMPONENT_COLOR }
};

class VulkanExample : public vkx::ExampleBase {
public:
    struct {
        vks::model::Model example;
    } meshes;

    struct {
        vks::texture::Texture2DArray colorMap;
    } textures;

    // Per-instance data block
    struct InstanceData {
        glm::vec3 pos;
        glm::vec3 rot;
        float scale;
        uint32_t texIndex;
    };

    // Contains the instanced data
    vks::Buffer instanceBuffer;

    struct UboVS {
        glm::mat4 projection;
        glm::mat4 view;
        float time = 0.0f;
    } uboVS;

    struct {
        vks::Buffer vsScene;
    } uniformData;

    struct {
        vk::Pipeline solid;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() {
        camera.setZoom(-12.0f);
        rotationSpeed = 0.25f;
        title = "Vulkan Example - Instanced mesh rendering";
        srand((uint32_t)time(NULL));
    }

    ~VulkanExample() {
        device.destroyPipeline(pipelines.solid);
        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);
        instanceBuffer.destroy();
        meshes.example.destroy();
        uniformData.vsScene.destroy();
        textures.colorMap.destroy();
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setViewport(0, vks::util::viewport(size));
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
        // Binding point 0 : Mesh vertex buffer
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buffer, { 0 });
        // Binding point 1 : Instance data buffer
        cmdBuffer.bindVertexBuffers(INSTANCE_BUFFER_BIND_ID, instanceBuffer.buffer, { 0 });
        cmdBuffer.bindIndexBuffer(meshes.example.indices.buffer, 0, vk::IndexType::eUint32);
        // Render instances
        cmdBuffer.drawIndexed(meshes.example.indexCount, INSTANCE_COUNT, 0, 0, 0);
    }

    void loadMeshes() { meshes.example.loadFromFile(context, getAssetPath() + "models/rock01.dae", vertexLayout, 0.1f); }

    void loadTextures() {
        textures.colorMap.loadFromFile(context, getAssetPath() + "textures/texturearray_rocks_bc3.ktx",
                                       vk::Format::eBc3UnormBlock);
    }

    void setupDescriptorPool() {
        // Example uses one ubo
        std::vector<vk::DescriptorPoolSize> poolSizes{
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 1 },
        };

        descriptorPool =
            device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Vertex shader uniform buffer
            { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader combined sampler
            { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        descriptorSet =
            device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool, 1, &descriptorSetLayout })[0];

        vk::DescriptorImageInfo texDescriptor =
            vk::DescriptorImageInfo{ textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral };

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr,
                                    &uniformData.vsScene.descriptor },
            // Binding 1 : Color map
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptor },
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayout, renderPass };
        pipelineBuilder.rasterizationState.frontFace = vk::FrontFace::eClockwise;

        // Binding description
        pipelineBuilder.vertexInputState.bindingDescriptions = {
            // Mesh vertex buffer (description) at binding point 0
            // Step for each vertex rendered
            { VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex },
            // Step for each instance rendered
            { INSTANCE_BUFFER_BIND_ID, sizeof(InstanceData), vk::VertexInputRate::eInstance },
        };

        // Attribute descriptions
        // Describes memory layout and shader positions
        pipelineBuilder.vertexInputState.attributeDescriptions = {
            // Per-Vertex attributes
            // Location 0 : Position
            { 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(0) },
            // Location 1 : Normal
            { 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(1) },
            // Location 2 : Texture coordinates
            { 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, vertexLayout.offset(2) },
            // Location 3 : Color
            { 3, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(3) },

            // Instanced attributes
            // Location 4 : Instance Position
            { 4, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(InstanceData, pos) },
            // Location 5 : Instance Rotation
            { 5, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(InstanceData, rot) },
            // Location 6 : Instance Scale
            { 6, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32Sfloat, offsetof(InstanceData, scale) },
            // Location 7 : Instance array layer
            { 7, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32Sint, offsetof(InstanceData, texIndex) },
        };

        // Load shaders
        pipelineBuilder.loadShader(getAssetPath() + "shaders/instancing/instancing.vert.spv",
                                      vk::ShaderStageFlagBits::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/instancing/instancing.frag.spv",
                                      vk::ShaderStageFlagBits::eFragment);
        // Instacing pipeline
        pipelines.solid = pipelineBuilder.create(context.pipelineCache);
    }

    float rnd(float range) { return range * (rand() / float(RAND_MAX)); }
    uint32_t rnd(uint32_t  range) { return (uint32_t)rnd((float)range); }

    void prepareInstanceData() {
        std::vector<InstanceData> instanceData;
        instanceData.resize(INSTANCE_COUNT);

        std::mt19937 rndGenerator((uint32_t)time(NULL));
        std::uniform_real_distribution<double> uniformDist(0.0, 1.0);

        for (auto i = 0; i < INSTANCE_COUNT; i++) {
            instanceData[i].rot =
                glm::vec3(M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator));
            float theta = (float)(2 * M_PI * uniformDist(rndGenerator));
            float phi = (float)acos(1 - 2 * uniformDist(rndGenerator));
            glm::vec3 pos;
            instanceData[i].pos =
                glm::vec3(sin(phi) * cos(theta), sin(theta) * uniformDist(rndGenerator) / 1500.0f, cos(phi)) * 7.5f;
            instanceData[i].scale = 1.0f + (float)uniformDist(rndGenerator) * 2.0f;
            instanceData[i].texIndex = rnd(textures.colorMap.layerCount);
        }

        // Staging
        // Instanced data is static, copy to device local memory
        // This results in better performance
        instanceBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, instanceData);
    }

    void prepareUniformBuffers() {
        uniformData.vsScene = context.createUniformBuffer(uboVS);
        updateUniformBuffer(true);
    }

    void updateUniformBuffer(bool viewChanged) {
        if (viewChanged) {
            uboVS.projection = getProjection();
            uboVS.view = camera.matrices.view;
        }

        if (!paused) {
            uboVS.time += frameTimer * 0.05f;
        }

        uniformData.vsScene.copy(uboVS);
    }

    void prepare() override {
        ExampleBase::prepare();
        loadTextures();
        loadMeshes();
        prepareInstanceData();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    void render() override {
        if (!prepared) {
            return;
        }
        draw();
        if (!paused) {
            updateUniformBuffer(false);
        }
    }

    void viewChanged() override { updateUniformBuffer(true); }
};

RUN_EXAMPLE(VulkanExample)
