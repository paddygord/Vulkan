/*
* Vulkan Example - Using device timestamps for performance measurements
*
* Copyright (C) 2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"

#define OBJ_DIM 0.05f

class VulkanExample : public vkx::ExampleBase {
public:
    // Vertex layout for the models
    vks::model::VertexLayout vertexLayout = vks::model::VertexLayout({
        vks::model::VERTEX_COMPONENT_POSITION,
        vks::model::VERTEX_COMPONENT_NORMAL,
        vks::model::VERTEX_COMPONENT_COLOR,
    });

    struct Models {
        vks::model::Model skybox;
        std::vector<vks::model::Model> objects;
        int32_t objectIndex = 3;
        std::vector<std::string> names;
    } models;

    struct {
        vks::Buffer VS;
    } uniformBuffers;

    struct UBOVS {
        glm::mat4 projection;
        glm::mat4 modelview;
        glm::vec4 lightPos = glm::vec4(-10.0f, -10.0f, 10.0f, 1.0f);
    } uboVS;

    std::vector<vk::Pipeline> pipelines;
    std::vector<std::string> pipelineNames;
    int32_t pipelineIndex = 0;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    vk::QueryPool queryPool;

    std::vector<float> timings;

    int32_t gridSize = 3;

    VulkanExample() {
        title = "Device timestamps";
        camera.type = Camera::CameraType::firstperson;
        camera.setPosition(glm::vec3(-4.0f, 3.0f, -3.75f));
        camera.setRotation(glm::vec3(-15.25f, -46.5f, 0.0f));
        camera.movementSpeed = 4.0f;
        camera.setPerspective(60.0f, (float)size.width / (float)size.height, 0.1f, 256.0f);
        camera.rotationSpeed = 0.25f;
        settings.overlay = true;
    }

    ~VulkanExample() {
        for (auto& pipeline : pipelines) {
            device.destroy(pipeline);
        }

        device.destroy(pipelineLayout);
        device.destroy(descriptorSetLayout);

        device.destroy(queryPool);

        uniformBuffers.VS.destroy();

        for (auto& model : models.objects) {
            model.destroy();
        }
        models.skybox.destroy();
    }

    // Setup a query pool for storing device timestamp query results
    void setupQueryPool() {
        timings.resize(3);
        // Create query pool
        queryPool = device.createQueryPool({ {}, vk::QueryType::eTimestamp, static_cast<uint32_t>(timings.size() - 1) });
    }

    // Retrieves the results of the occlusion queries submitted to the command buffer
    void getQueryResults() {
        timings.resize(2);

        uint32_t start = 0;
        uint32_t end = 0;
        device.getQueryPoolResults<uint32_t>(queryPool, 0, 1, start, 0, vk::QueryResultFlagBits::eWait);

        // timestampPeriod is the number of nanoseconds per timestamp value increment
        static const float factor = 1e6f * context.deviceProperties.limits.timestampPeriod;

        device.getQueryPoolResults<uint32_t>(queryPool, 1, 1, end, 0, vk::QueryResultFlagBits::eWait);
        timings[0] = (float)(end - start) / factor;
        //end = start;

        device.getQueryPoolResults<uint32_t>(queryPool, 2, 1, end, 0, vk::QueryResultFlagBits::eWait);
        timings[1] = (float)(end - start) / factor;
    }

    void updateCommandBufferPreDraw(const vk::CommandBuffer& drawCommandBuffer) override {
        // Reset timestamp query pool
        drawCommandBuffer.resetQueryPool(queryPool, 0, 2);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& drawCommandBuffer) override {
        vk::Viewport viewport{ 0, 0, (float)size.width, (float)size.height, 0.0f, 1.0f };
        drawCommandBuffer.setViewport(0, viewport);

        vk::Rect2D scissor{ { 0, 0}, size };
        drawCommandBuffer.setScissor(0, scissor);

        vk::DeviceSize offsets[1] = { 0 };

        drawCommandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, queryPool, 0);
        drawCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines[pipelineIndex]);
        drawCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        drawCommandBuffer.bindVertexBuffers(0, models.objects[models.objectIndex].vertices.buffer, { 0 });
        drawCommandBuffer.bindIndexBuffer(models.objects[models.objectIndex].indices.buffer, 0, vk::IndexType::eUint32);

        for (uint32_t y = 0; y < gridSize; y++) {
            for (uint32_t x = 0; x < gridSize; x++) {
                glm::vec3 pos = glm::vec3(float(x - (gridSize / 2.0f)) * 2.5f, 0.0f, float(y - (gridSize / 2.0f)) * 2.5f);
                drawCommandBuffer.pushConstants<glm::vec3>(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pos);
                drawCommandBuffer.drawIndexed(models.objects[models.objectIndex].indexCount, 1, 0, 0, 0);
            }
        }

        drawCommandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eFragmentShader, queryPool, 1);
        drawCommandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eFragmentShader, queryPool, 2);
    }

    void draw() {
        prepareFrame();
        drawCurrentCommandBuffer();
        // Read query results for displaying in next frame
        getQueryResults();
        submitFrame();
    }

    void loadAssets() override {
        // Skybox
        // models.skybox.loadFromFile(getAssetPath() + "models/cube.obj", vertexLayout, 1.0f, vulkanDevice, queue);
        // Objects
        std::vector<std::string> filenames = { "geosphere.obj", "teapot.dae", "torusknot.obj", "venus.fbx" };
        for (auto file : filenames) {
            vks::model::Model model;
            model.loadFromFile(context, getAssetPath() + "models/" + file, vertexLayout, OBJ_DIM * (file == "venus.fbx" ? 3.0f : 1.0f));
            models.objects.push_back(model);
        }
        models.names = { "Sphere", "Teapot", "Torusknot", "Venus" };
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes{
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 3 },
        };
        descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 2, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
        };

        descriptorSetLayout = device.createDescriptorSetLayout({ {}, static_cast<uint32_t>(setLayoutBindings.size()), setLayoutBindings.data() });
        vk::PushConstantRange pushConstantRange{ vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::vec3) };
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout, 1, &pushConstantRange });
    }

    void setupDescriptorSets() {
        descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &descriptorSetLayout })[0];

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            { descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformBuffers.VS.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, {});
    }

    void preparePipelines() {
        pipelines.resize(3);
        vks::pipelines::GraphicsPipelineBuilder builder{ device, pipelineLayout, renderPass };
        builder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        // Vertex bindings and attributes
        builder.vertexInputState.appendVertexLayout(vertexLayout);
        // Phong shading
        builder.loadShader(getAssetPath() + "shaders/timestampquery/mesh.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/timestampquery/mesh.frag.spv", vk::ShaderStageFlagBits::eFragment);

        pipelines[0] = builder.create(context.pipelineCache);
        builder.destroyShaderModules();

        // Color only
        builder.loadShader(getAssetPath() + "shaders/timestampquery/simple.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/timestampquery/simple.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines[1] = builder.create(context.pipelineCache);
        builder.destroyShaderModules();


        // Blending
        builder.loadShader(getAssetPath() + "shaders/timestampquery/occluder.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/timestampquery/occluder.frag.spv", vk::ShaderStageFlagBits::eFragment);
        builder.rasterizationState.cullMode = vk::CullModeFlagBits::eFront;
        builder.rasterizationState.frontFace = vk::FrontFace::eClockwise;
        auto& blendAttachmentState = builder.colorBlendState.blendAttachmentStates[0];
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;;
        builder.depthStencilState.depthWriteEnable = false;
        pipelines[2] = builder.create(context.pipelineCache);
        pipelineNames = { "Shaded", "Color only", "Blending" };
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformBuffers.VS = context.createUniformBuffer(uboVS);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboVS.projection = camera.matrices.perspective;
        uboVS.modelview = camera.matrices.view;
        memcpy(uniformBuffers.VS.mapped, &uboVS, sizeof(uboVS));
    }

    void prepare() {
        ExampleBase::prepare();
        setupQueryPool();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSets();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
    }

    virtual void viewChanged() { updateUniformBuffers(); }

    virtual void OnUpdateUIOverlay() {
        if (ui.header("Settings")) {
            if (ui.comboBox("Object type", &models.objectIndex, models.names)) {
                updateUniformBuffers();
                buildCommandBuffers();
            }
            if (ui.comboBox("Pipeline", &pipelineIndex, pipelineNames)) {
                buildCommandBuffers();
            }
            if (ui.sliderInt("Grid size", &gridSize, 1, 10)) {
                buildCommandBuffers();
            }
        }
        if (ui.header("Timings")) {
            if (!timings.empty()) {
                ui.text("Frame start to VS = %.3f", timings[0]);
                ui.text("VS to FS = %.3f", timings[1]);
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
