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
vks::model::VertexLayout vertexLayout { {
    vks::model::Component::VERTEX_COMPONENT_POSITION,
    vks::model::Component::VERTEX_COMPONENT_NORMAL,
    vks::model::Component::VERTEX_COMPONENT_UV,
    vks::model::Component::VERTEX_COMPONENT_COLOR
} };

class VulkanExample : public vkx::ExampleBase {
public:
    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

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

    void loadMeshes() {
        meshes.example.loadFromFile(context, getAssetPath() + "models/rock01.dae", vertexLayout, 0.1f);
    }

    void loadTextures() {
        textures.colorMap.loadFromFile(context, 
            getAssetPath() + "textures/texturearray_rocks_bc3.ktx",
            vk::Format::eBc3UnormBlock);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions = {
            // Mesh vertex buffer (description) at binding point 0
            // Step for each vertex rendered
            vk::VertexInputBindingDescription{ VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex },
            // Step for each instance rendered
            vk::VertexInputBindingDescription{ INSTANCE_BUFFER_BIND_ID, sizeof(InstanceData), vk::VertexInputRate::eInstance },
        };


        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions = {
            // Per-Vertex attributes

            // Location 0 : Position
            vk::VertexInputAttributeDescription{ 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(0) },
            // Location 1 : Normal
            vk::VertexInputAttributeDescription{ 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(1) },
            // Location 2 : Texture coordinates
            vk::VertexInputAttributeDescription{ 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, vertexLayout.offset(2) },
            // Location 3 : Color
            vk::VertexInputAttributeDescription{ 3, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(3) },

            // Instanced attributes

            // Location 4 : Instance Position
            vk::VertexInputAttributeDescription{ 4, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(InstanceData, pos) },
            // Location 5 : Instance Rotation
            vk::VertexInputAttributeDescription{ 5, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(InstanceData, rot) },
            // Location 6 : Instance Scale
            vk::VertexInputAttributeDescription{ 6, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32Sfloat, offsetof(InstanceData, scale) },
            // Location 7 : Instance array layer
            vk::VertexInputAttributeDescription{ 7, INSTANCE_BUFFER_BIND_ID, vk::Format::eR32Sint, offsetof(InstanceData, texIndex) },
        };

        vertices.inputState.vertexBindingDescriptionCount = (uint32_t)vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = (uint32_t)vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // Example uses one ubo 
        std::vector<vk::DescriptorPoolSize> poolSizes {
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 1 },
        };

        descriptorPool = device.createDescriptorPool(
            vk::DescriptorPoolCreateInfo{ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() }
        );
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Vertex shader uniform buffer
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader combined sampler
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        descriptorSetLayout = device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo{ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() }
        );

         pipelineLayout = device.createPipelineLayout(
             vk::PipelineLayoutCreateInfo{ {}, 1, &descriptorSetLayout }
         );
    }

    void setupDescriptorSet() {
        descriptorSet = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool , 1, &descriptorSetLayout })[0];

        vk::DescriptorImageInfo texDescriptor =
            vk::DescriptorImageInfo{ textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral };

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets {
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet { descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.vsScene.descriptor },
            // Binding 1 : Color map 
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptor },
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{ {}, vk::PrimitiveTopology::eTriangleList };
        vk::PipelineRasterizationStateCreateInfo rasterizationState; 
        rasterizationState.lineWidth = 1.0f;
        rasterizationState.cullMode = vk::CullModeFlagBits::eBack;
        rasterizationState.frontFace = vk::FrontFace::eClockwise;

        vk::PipelineColorBlendAttachmentState blendAttachmentState;
        blendAttachmentState.colorWriteMask = vks::util::fullColorWriteMask();
        vk::PipelineColorBlendStateCreateInfo colorBlendState;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = &blendAttachmentState;
        vk::PipelineDepthStencilStateCreateInfo depthStencilState{ {}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual };
        vk::PipelineViewportStateCreateInfo viewportState{ {}, 1, nullptr, 1, nullptr };
        vk::PipelineMultisampleStateCreateInfo multisampleState;
        std::vector<vk::DynamicState> dynamicStateEnables = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState{ {}, (uint32_t)dynamicStateEnables.size(),  dynamicStateEnables.data() };

        // Instacing pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing/instancing.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing/instancing.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.renderPass = renderPass;
        pipelineCreateInfo.pVertexInputState = &vertices.inputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();

        pipelines.solid = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];

        for (const auto shaderStage : shaderStages) {
            context.device.destroyShaderModule(shaderStage.module);
        }
    }

    float rnd(float range) {
        return range * (rand() / float(RAND_MAX));
    }

    void prepareInstanceData() {
        std::vector<InstanceData> instanceData;
        instanceData.resize(INSTANCE_COUNT);

        std::mt19937 rndGenerator((uint32_t)time(NULL));
        std::uniform_real_distribution<double> uniformDist(0.0, 1.0);

        for (auto i = 0; i < INSTANCE_COUNT; i++) {
            instanceData[i].rot = glm::vec3(M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator));
            float theta = 2 * M_PI * uniformDist(rndGenerator);
            float phi = acos(1 - 2 * uniformDist(rndGenerator));
            glm::vec3 pos;
            instanceData[i].pos = glm::vec3(sin(phi) * cos(theta), sin(theta) * uniformDist(rndGenerator) / 1500.0f, cos(phi)) * 7.5f;
            instanceData[i].scale = 1.0f + uniformDist(rndGenerator) * 2.0f;
            instanceData[i].texIndex = rnd(textures.colorMap.layerCount);
        }

        // Staging
        // Instanced data is static, copy to device local memory 
        // This results in better performance
        instanceBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, instanceData);
    }

    void prepareUniformBuffers() {
        uniformData.vsScene= context.createUniformBuffer(uboVS);
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
        setupVertexDescriptions();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        updateDrawCommandBuffers();
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

    void viewChanged() override {
        updateUniformBuffer(true);
    }
};

RUN_EXAMPLE(VulkanExample)
