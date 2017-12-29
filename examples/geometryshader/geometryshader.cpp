/*
* Vulkan Example - Geometry shader (vertex normal debugging)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


// Vertex layout for this example
vks::model::VertexLayout vertexLayout{ {
    vks::model::Component::VERTEX_COMPONENT_POSITION,
    vks::model::Component::VERTEX_COMPONENT_NORMAL,
    vks::model::Component::VERTEX_COMPONENT_COLOR,
} };

class VulkanExample : public vkx::ExampleBase {
public:
    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vks::model::Model object;
    } meshes;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVS;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboGS;

    struct {
        vks::Buffer VS;
        vks::Buffer GS;
    } uniformData;

    struct {
        vk::Pipeline solid;
        vk::Pipeline normals;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() {
        camera.setZoom(-8.0f);
        camera.setRotation({ 0.0f, -25.0f, 0.0f });
        title = "Vulkan Example - Geometry shader";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class
        device.destroyPipeline(pipelines.solid);
        device.destroyPipeline(pipelines.normals);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        meshes.object.destroy();

        uniformData.VS.destroy();
        uniformData.GS.destroy();
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setViewport(0, vks::util::viewport(size));
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.setLineWidth(1.0f);
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.object.vertices.buffer, { 0 });
        cmdBuffer.bindIndexBuffer(meshes.object.indices.buffer, 0, vk::IndexType::eUint32);
        // Solid shading
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
        cmdBuffer.drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);
        // Normal debugging
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.normals);
        cmdBuffer.drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);
    }

    void loadMeshes() {
        meshes.object.loadFromFile(context, getAssetPath() + "models/suzanne.obj", vertexLayout, 0.25f);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions = {
            vk::VertexInputBindingDescription{ VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex }
        };

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions = {
            // Location 0 : Position
            vk::VertexInputAttributeDescription{ 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(0) },
            // Location 1 : Normals
            vk::VertexInputAttributeDescription{ 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(1) },
            // Location 2 : Color
            vk::VertexInputAttributeDescription{ 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, vertexLayout.offset(2) },
        };

        // Assign to vertex buffer
        vertices.inputState.vertexBindingDescriptionCount = (uint32_t)vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = (uint32_t)vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // Example uses two ubos
        std::vector<vk::DescriptorPoolSize> poolSizes{
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 2 },
        };

        descriptorPool = device.createDescriptorPool(
            vk::DescriptorPoolCreateInfo{ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() }
        );
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Vertex shader ubo
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Geometry shader ubo
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eGeometry },
        };

        descriptorSetLayout = device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo{ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() }
        );

        pipelineLayout = device.createPipelineLayout(
            vk::PipelineLayoutCreateInfo{ {}, 1, &descriptorSetLayout }
        );
    }

    void setupDescriptorSet() {
        descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &descriptorSetLayout })[0];

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            // Binding 0 : Vertex shader shader ubo
            vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.VS.descriptor },
            // Binding 1 : Geometry shader ubo
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.GS.descriptor },
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
            vk::DynamicState::eScissor,
            vk::DynamicState::eLineWidth
        };
        vk::PipelineDynamicStateCreateInfo dynamicState{ {}, (uint32_t)dynamicStateEnables.size(), dynamicStateEnables.data() };

        // Tessellation pipeline
        // Load shaders
        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages{
            loadShader(getAssetPath() + "shaders/geometryshader/base.vert.spv", vk::ShaderStageFlagBits::eVertex),
            loadShader(getAssetPath() + "shaders/geometryshader/base.frag.spv", vk::ShaderStageFlagBits::eFragment),
            loadShader(getAssetPath() + "shaders/geometryshader/normaldebug.geom.spv", vk::ShaderStageFlagBits::eGeometry),
        };


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
        pipelineCreateInfo.renderPass = renderPass;

        // Normal debugging pipeline
        pipelines.normals = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo)[0];
        for (const auto shaderStage : shaderStages) {
            context.device.destroyShaderModule(shaderStage.module);
        }

        // Solid rendering pipeline
        // Shader
        shaderStages = {
            loadShader(getAssetPath() + "shaders/geometryshader/mesh.vert.spv", vk::ShaderStageFlagBits::eVertex),
            loadShader(getAssetPath() + "shaders/geometryshader/mesh.frag.spv", vk::ShaderStageFlagBits::eFragment),
        };
        pipelineCreateInfo.stageCount = 2;
        pipelines.solid = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo)[0];

        for (const auto shaderStage : shaderStages) {
            context.device.destroyShaderModule(shaderStage.module);
        }
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformData.VS= context.createUniformBuffer(uboVS);
        // Geometry shader uniform buffer block
        uniformData.GS= context.createUniformBuffer(uboGS);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader
        uboVS.projection = getProjection();
        uboVS.model = camera.matrices.view;
        uniformData.VS.copy(uboVS);

        // Geometry shader
        uboGS.model = uboVS.model;
        uboGS.projection = uboVS.projection;
        uniformData.GS.copy(uboGS);
    }

    void prepare() override {
        ExampleBase::prepare();
        loadMeshes();
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
        if (!prepared)
            return;
        draw();
    }

    void viewChanged() override {
        updateUniformBuffers();
    }
};

RUN_EXAMPLE(VulkanExample)
