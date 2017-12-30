/*
* Vulkan Example - Tessellation shader PN triangles
*
* Based on http://alex.vlachos.com/graphics/CurvedPNTriangles.pdf
* Shaders based on http://onrendering.blogspot.de/2011/12/tessellation-on-gpu-curved-pn-triangles.html
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


// Vertex layout for this example
vks::model::VertexLayout vertexLayout =
{
    vks::model::Component::VERTEX_COMPONENT_POSITION,
    vks::model::Component::VERTEX_COMPONENT_NORMAL,
    vks::model::Component::VERTEX_COMPONENT_UV
};

class VulkanExample : public vkx::ExampleBase {
    using Parent = vkx::ExampleBase;
public:
    bool splitScreen = true;

    struct {
        vkx::Texture colorMap;
    } textures;

    struct {
        vks::model::Model object;
    } meshes;

    vks::Buffer uniformDataTC, uniformDataTE;

    struct UboTC {
        float tessLevel = 3.0f;
    } uboTC;

    struct UboTE {
        glm::mat4 projection;
        glm::mat4 model;
        float tessAlpha = 1.0f;
    } uboTE;

    struct {
        vk::Pipeline solid;
        vk::Pipeline wire;
        vk::Pipeline solidPassThrough;
        vk::Pipeline wirePassThrough;
    } pipelines;
    vk::Pipeline *pipelineLeft = &pipelines.wirePassThrough;
    vk::Pipeline *pipelineRight = &pipelines.wire;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() {
        camera.setRotation({ -350.0f, 60.0f, 0.0f });
        camera.setTranslation({ -3.0f, 2.3f, -6.5f });
        title = "Vulkan Example - Tessellation shader (PN Triangles)";
        enableTextOverlay = true;
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
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {

        vk::Viewport viewport = vks::util::viewport(splitScreen ? (float)size.width / 2.0f : (float)size.width, (float)size.height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.setLineWidth(1.0f);

        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);

        vk::DeviceSize offsets = 0;
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.object.vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(meshes.object.indices.buffer, 0, vk::IndexType::eUint32);

        if (splitScreen) {
            cmdBuffer.setViewport(0, viewport);
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineLeft);
            cmdBuffer.drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);
            viewport.x = float(size.width) / 2;
        }

        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineRight);
        cmdBuffer.drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);
    }

    void loadMeshes() {
        meshes.object = loadMesh(getAssetPath() + "models/lowpoly/deer.dae", vertexLayout, 1.0f);
    }

    void loadTextures() {
        textures.colorMap = textureLoader->loadTexture(
            getAssetPath() + "textures/deer.ktx",
            vk::Format::eBc3UnormBlock);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vkx::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vkx::vertexSize(vertexLayout), vk::VertexInputRate::eVertex);

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(3);

        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);

        // Location 1 : Normals
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3);

        // Location 2 : Texture coordinates
        vertices.attributeDescriptions[2] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 6);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // Example uses two ubos and one combined image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 2),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1),
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vk::DescriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 1);

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
        {
            // Binding 0 : Tessellation control shader ubo
            vk::DescriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eTessellationControl,
                0),
            // Binding 1 : Tessellation evaluation shader ubo
            vk::DescriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eTessellationEvaluation,
                1),
            // Binding 2 : Fragment shader combined sampler
            vk::DescriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                2),
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vk::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

        pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo =
            vk::DescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo texDescriptor =
            vk::DescriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Tessellation control shader ubo
            vk::WriteDescriptorSet(
            descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformDataTC.descriptor),
            // Binding 1 : Tessellation evaluation shader ubo
            vk::WriteDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                1,
                &uniformDataTE.descriptor),
            // Binding 2 : Color map 
            vk::WriteDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eCombinedImageSampler,
                2,
                &texDescriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::ePatchList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise);

        vk::PipelineColorBlendAttachmentState blendAttachmentState =
            vkx::pipelineColorBlendAttachmentState();

        vk::PipelineColorBlendStateCreateInfo colorBlendState =
            vkx::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vkx::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);

        vk::PipelineViewportStateCreateInfo viewportState =
            vkx::pipelineViewportStateCreateInfo(1, 1);

        vk::PipelineMultisampleStateCreateInfo multisampleState =
            vkx::pipelineMultisampleStateCreateInfo(vk::SampleCountFlagBits::e1);

        std::vector<vk::DynamicState> dynamicStateEnables = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
            vk::DynamicState::eLineWidth
        };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        vk::PipelineTessellationStateCreateInfo tessellationState =
            vkx::pipelineTessellationStateCreateInfo(3);

        // Tessellation pipelines
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 4> shaderStages;

        shaderStages[0] = context.loadShader(getAssetPath() + "shaders/tessellation/base.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = context.loadShader(getAssetPath() + "shaders/tessellation/base.frag.spv", vk::ShaderStageFlagBits::eFragment);
        shaderStages[2] = context.loadShader(getAssetPath() + "shaders/tessellation/pntriangles.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl);
        shaderStages[3] = context.loadShader(getAssetPath() + "shaders/tessellation/pntriangles.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
            vkx::pipelineCreateInfo(pipelineLayout, renderPass);

        pipelineCreateInfo.pVertexInputState = &vertices.inputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.pTessellationState = &tessellationState;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.renderPass = renderPass;

        // Tessellation pipelines
        // Solid
        pipelines.solid = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];
        // Wireframe
        rasterizationState.polygonMode = vk::PolygonMode::eLine;
        pipelines.wire = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Pass through pipelines
        // Load pass through tessellation shaders (Vert and frag are reused)
        shaderStages[2] = context.loadShader(getAssetPath() + "shaders/tessellation/passthrough.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl);
        shaderStages[3] = context.loadShader(getAssetPath() + "shaders/tessellation/passthrough.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation);

        // Solid
        rasterizationState.polygonMode = vk::PolygonMode::eFill;
        pipelines.solidPassThrough = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];
        // Wireframe
        rasterizationState.polygonMode = vk::PolygonMode::eLine;
        pipelines.wirePassThrough = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Tessellation evaluation shader uniform buffer
        uniformDataTE= context.createUniformBuffer(uboTE);
        // Tessellation control shader uniform buffer
        uniformDataTC= context.createUniformBuffer(uboTC);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Tessellation eval
        uboTE.projection = glm::perspective(glm::radians(45.0f), (float)(size.width* ((splitScreen) ? 0.5f : 1.0f)) / (float)size.height, 0.1f, 256.0f);
        uboTE.model = camera.matrices.view;
        uniformDataTE.copy(uboTE);

        // Tessellation control uniform block
        uniformDataTC.copy(uboTC);
    }

    void prepare() override {
        ExampleBase::prepare();
        loadTextures();
        loadMeshes();
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

    void viewChanged() override {
        updateUniformBuffers();
    }


    virtual void keyPressed(uint32_t keyCode) override {
        switch (keyCode) {
        case GLFW_KEY_KP_ADD:
        case GAMEPAD_BUTTON_R1:
            changeTessellationLevel(0.25);
            break;
        case GLFW_KEY_KP_SUBTRACT:
        case GAMEPAD_BUTTON_L1:
            changeTessellationLevel(-0.25);
            break;
        case GLFW_KEY_W:
        case GAMEPAD_BUTTON_A:
            togglePipelines();
            break;
        case GLFW_KEY_S:
        case GAMEPAD_BUTTON_X:
            toggleSplitScreen();
            break;
        }
    }

    void getOverlayText(vkx::TextOverlay *textOverlay) override {
        std::stringstream ss;
        ss << std::setprecision(2) << std::fixed << uboTC.tessLevel;
#if defined(__ANDROID__)
        textOverlay->addText("Tessellation level: " + ss.str() + " (Buttons L1/R1 to change)", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
#else
        textOverlay->addText("Tessellation level: " + ss.str() + " (NUMPAD +/- to change)", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
#endif
    }

    void changeTessellationLevel(float delta) {
        uboTC.tessLevel += delta;
        // Clamp
        uboTC.tessLevel = fmax(1.0f, fmin(uboTC.tessLevel, 32.0f));
        updateUniformBuffers();
        updateTextOverlay();
    }

    void togglePipelines() {
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
        updateUniformBuffers();
        buildCommandBuffers();
    }

};

RUN_EXAMPLE(VulkanExample)
