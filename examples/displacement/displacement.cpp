/*
* Vulkan Example - Displacement mapping with tessellation shaders
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>

// Vertex layout for this example
vks::model::VertexLayout vertexLayout{ {
    vks::model::Component::VERTEX_COMPONENT_POSITION,
    vks::model::Component::VERTEX_COMPONENT_NORMAL,
    vks::model::Component::VERTEX_COMPONENT_UV,
} };

class VulkanExample : public vkx::ExampleBase {
    using Parent = vkx::ExampleBase;

private:
    struct {
        vks::texture::Texture2D colorMap;
        vks::texture::Texture2D heightMap;
    } textures;

public:
    bool splitScreen = true;

    struct {
        vks::model::Model object;
    } meshes;

    vks::Buffer uniformDataTC, uniformDataTE;

    struct UboTC {
        float tessLevel = 8.0;
    } uboTC;

    struct UboTE {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec4 lightPos = glm::vec4(0.0, -25.0, 0.0, 0.0);
        float tessAlpha = 1.0;
        float tessStrength = 1.0;
    } uboTE;

    struct {
        vk::Pipeline solid;
        vk::Pipeline wire;
        vk::Pipeline solidPassThrough;
        vk::Pipeline wirePassThrough;
    } pipelines;
    vk::Pipeline* pipelineLeft = &pipelines.solidPassThrough;
    vk::Pipeline* pipelineRight = &pipelines.solid;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() {
        camera.dolly(-50.25f);
        camera.setRotation(glm::vec3(-20.0f, 45.0f, 0.0f));
        title = "Tessellation shader displacement";
    }

    void initVulkan() override {
        Parent::initVulkan();
        // Support for tessellation shaders is optional, so check first
        if (!context.deviceFeatures.tessellationShader) {
            throw std::runtime_error("Selected GPU does not support tessellation shaders!");
        }
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        device.destroyPipeline(pipelines.solid);
        device.destroyPipeline(pipelines.wire);
        device.destroyPipeline(pipelines.solidPassThrough);
        device.destroyPipeline(pipelines.wirePassThrough);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        meshes.object.destroy();

        device.destroyBuffer(uniformDataTC.buffer);
        device.freeMemory(uniformDataTC.memory);

        device.destroyBuffer(uniformDataTE.buffer);
        device.freeMemory(uniformDataTE.memory);

        textures.colorMap.destroy();
        textures.heightMap.destroy();
    }

    void loadTextures() {
        textures.colorMap.loadFromFile(context, getAssetPath() + "textures/stonewall_colormap_bc3.dds", vk::Format::eBc3UnormBlock);
        textures.heightMap.loadFromFile(context, getAssetPath() + "textures/stonewall_heightmap_rgba.dds", vk::Format::eR8G8B8A8Unorm);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        vk::Viewport viewport = vks::util::viewport(splitScreen ? (float)size.width / 2.0f : (float)size.width, (float)size.height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.setLineWidth(1.0f);
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        cmdBuffer.bindVertexBuffers(0, meshes.object.vertices.buffer, { 0 });
        cmdBuffer.bindIndexBuffer(meshes.object.indices.buffer, 0, vk::IndexType::eUint32);

        if (splitScreen) {
            cmdBuffer.setViewport(0, viewport);
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineLeft);
            cmdBuffer.drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);
            viewport.x += viewport.width;
        }

        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineRight);
        cmdBuffer.drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);
    }

    void loadMeshes() { meshes.object.loadFromFile(context, getAssetPath() + "models/torus.obj", vertexLayout, 0.25f); }

    void setupVertexDescriptions() {}

    void setupDescriptorPool() {
        // Example uses two ubos and two image samplers
        std::vector<vk::DescriptorPoolSize> poolSizes = { vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 2),
                                                          vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2) };

        descriptorPool = device.createDescriptorPool({ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Tessellation control shader ubo
            { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eTessellationControl },
            // Binding 1 : Tessellation evaluation shader ubo
            { 1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eTessellationEvaluation },
            // Binding 2 : Tessellation evaluation shader displacement map image sampler
            { 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eTessellationEvaluation },
            // Binding 3 : Fragment shader color map image sampler
            { 3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &descriptorSetLayout })[0];
        // Displacement map image descriptor
        vk::DescriptorImageInfo texDescriptorDisplacementMap{ textures.heightMap.sampler, textures.heightMap.view, vk::ImageLayout::eGeneral };
        // Color map image descriptor
        vk::DescriptorImageInfo texDescriptorColorMap{ textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral };

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            // Binding 0 : Tessellation control shader ubo
            { descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformDataTC.descriptor },
            // Binding 1 : Tessellation evaluation shader ubo
            { descriptorSet, 1, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformDataTE.descriptor },
            // Binding 2 : Displacement map
            { descriptorSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorDisplacementMap },
            // Binding 3 : Color map
            { descriptorSet, 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorColorMap },
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayout, renderPass };
        pipelineBuilder.inputAssemblyState.topology = vk::PrimitiveTopology::ePatchList;
        pipelineBuilder.depthStencilState = { true };
        pipelineBuilder.dynamicState.dynamicStateEnables = { vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth };

        vk::PipelineTessellationStateCreateInfo tessellationState{ {}, 3 };
        pipelineBuilder.pipelineCreateInfo.pTessellationState = &tessellationState;

        // Tessellation pipeline
        // Load shaders
        pipelineBuilder.loadShader(getAssetPath() + "shaders/displacement/base.vert.spv", vk::ShaderStageFlagBits::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/displacement/base.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/displacement/displacement.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/displacement/displacement.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation);
        pipelineBuilder.vertexInputState.appendVertexLayout(vertexLayout);
        // Solid pipeline
        pipelines.solid = pipelineBuilder.create(context.pipelineCache);

        // Wireframe pipeline
        pipelineBuilder.rasterizationState.polygonMode = vk::PolygonMode::eLine;
        pipelines.wire = pipelineBuilder.create(context.pipelineCache);

        // Pass through pipelines
        // Load pass through tessellation shaders (Vert and frag are reused)
        context.device.destroyShaderModule(pipelineBuilder.shaderStages[2].module);
        context.device.destroyShaderModule(pipelineBuilder.shaderStages[3].module);
        pipelineBuilder.shaderStages.resize(2);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/displacement/passthrough.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/displacement/passthrough.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation);

        // Solid
        pipelineBuilder.rasterizationState.polygonMode = vk::PolygonMode::eFill;
        pipelines.solidPassThrough = pipelineBuilder.create(context.pipelineCache);

        // Wireframe
        pipelineBuilder.rasterizationState.polygonMode = vk::PolygonMode::eLine;
        pipelines.wirePassThrough = pipelineBuilder.create(context.pipelineCache);
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Tessellation evaluation shader uniform buffer
        uniformDataTE = context.createUniformBuffer(uboTE);
        // Tessellation control shader uniform buffer
        uniformDataTC = context.createUniformBuffer(uboTC);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Tessellation eval
        uboTE.projection = glm::perspective(glm::radians(45.0f), (float)(size.width * ((splitScreen) ? 0.5f : 1.0f)) / (float)size.height, 0.1f, 256.0f);
        uboTE.model = camera.matrices.view;
        uniformDataTE.copy(uboTE);

        // Tessellation control
        uniformDataTC.copy(uboTC);
    }

    void prepare() override {
        ExampleBase::prepare();
        loadMeshes();
        loadTextures();
        setupVertexDescriptions();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    void render() override {
        if (!prepared)
            return;
        draw();
    }

    void viewChanged() override { updateUniformBuffers(); }

    void changeTessellationLevel(float delta) {
        uboTC.tessLevel += delta;
        // Clamp
        uboTC.tessLevel = fmax(1.0f, fmin(uboTC.tessLevel, 32.0f));
        updateUniformBuffers();
    }

    void togglePipelines() {
        context.queue.waitIdle();
        context.device.waitIdle();
        if (pipelineRight == &pipelines.solid) {
            pipelineRight = &pipelines.wire;
            pipelineLeft = &pipelines.wirePassThrough;
        } else {
            pipelineRight = &pipelines.solid;
            pipelineLeft = &pipelines.solidPassThrough;
        }
        buildCommandBuffers();
    }

    void toggleSplitScreen() {
        splitScreen = !splitScreen;
        buildCommandBuffers();
        updateUniformBuffers();
    }

    void keyPressed(uint32_t key) override {
        switch (key) {
            case KEY_KPADD:
                changeTessellationLevel(0.25);
                break;
            case KEY_KPSUB:
                changeTessellationLevel(-0.25);
                break;
            case KEY_W:
                togglePipelines();
                break;
            case KEY_S:
                toggleSplitScreen();
                break;
        }
    }
};

RUN_EXAMPLE(VulkanExample)
