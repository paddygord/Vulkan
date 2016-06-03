/*
* Vulkan Example - Displacement mapping with tessellation shaders
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


// Vertex layout for this example
std::vector<vkx::VertexLayout> vertexLayout =
{
    vkx::VertexLayout::VERTEX_LAYOUT_POSITION,
    vkx::VertexLayout::VERTEX_LAYOUT_NORMAL,
    vkx::VertexLayout::VERTEX_LAYOUT_UV
};

class VulkanExample : public vkx::ExampleBase {
private:
    struct {
        vkx::Texture colorMap;
        vkx::Texture heightMap;
    } textures;
public:
    bool splitScreen = true;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::MeshBuffer object;
    } meshes;

    vkx::UniformData uniformDataTC, uniformDataTE;

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
    vk::Pipeline *pipelineLeft = &pipelines.solidPassThrough;
    vk::Pipeline *pipelineRight = &pipelines.solid;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        zoom = -35;
        rotation = glm::vec3(-35.0, 0.0, 0);
        title = "Vulkan Example - Tessellation shader displacement mapping";
        // Support for tessellation shaders is optional, so check first
        if (!deviceFeatures.tessellationShader) {
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
        textures.colorMap = textureLoader->loadTexture(
            getAssetPath() + "textures/stonewall_colormap_bc3.dds",
             vk::Format::eBc3UnormBlock);
        textures.heightMap = textureLoader->loadTexture(
            getAssetPath() + "textures/stonewall_heightmap_rgba.dds",
             vk::Format::eR8G8B8A8Unorm);
    }

    void reBuildCommandBuffers() {
        if (!checkCommandBuffers()) {
            destroyCommandBuffers();
            createCommandBuffers();
        }
        buildCommandBuffers();
    }

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = defaultClearColor;
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            drawCmdBuffers[i].begin(cmdBufInfo);


            drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

            vk::Viewport viewport = vkx::viewport(splitScreen ? (float)width / 2.0f : (float)width, (float)height, 0.0f, 1.0f);
            drawCmdBuffers[i].setViewport(0, viewport);

            vk::Rect2D scissor = vkx::rect2D(width, height, 0, 0);
            drawCmdBuffers[i].setScissor(0, scissor);

            drawCmdBuffers[i].setLineWidth(1.0f);

            drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);

            vk::DeviceSize offsets = 0;
            drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.object.vertices.buffer, offsets);
            drawCmdBuffers[i].bindIndexBuffer(meshes.object.indices.buffer, 0, vk::IndexType::eUint32);

            if (splitScreen) {
                drawCmdBuffers[i].setViewport(0, viewport);
                drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineLeft);
                drawCmdBuffers[i].drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);
                viewport.x = float(width) / 2;
            }

            drawCmdBuffers[i].setViewport(0, viewport);
            drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineRight);
            drawCmdBuffers[i].drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);

            drawCmdBuffers[i].endRenderPass();

            drawCmdBuffers[i].end();

        }
    }

    void loadMeshes() {
        meshes.object = loadMesh(getAssetPath() + "models/torus.obj", vertexLayout, 0.25f);
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
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0,  vk::Format::eR32G32B32Sfloat, 0);

        // Location 1 : Normals
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1,  vk::Format::eR32G32B32Sfloat, sizeof(float) * 3);

        // Location 2 : Texture coordinates
        vertices.attributeDescriptions[2] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2,  vk::Format::eR32G32Sfloat, sizeof(float) * 6);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // Example uses two ubos and two image samplers
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 2),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
        {
            // Binding 0 : Tessellation control shader ubo
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eTessellationControl,
                0),
            // Binding 1 : Tessellation evaluation shader ubo
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eTessellationEvaluation,
                1),
            // Binding 2 : Tessellation evaluation shader displacement map image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eTessellationEvaluation,
                2),
            // Binding 3 : Fragment shader color map image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                3),
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);


        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

        pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);

    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        // Displacement map image descriptor
        vk::DescriptorImageInfo texDescriptorDisplacementMap =
            vkx::descriptorImageInfo(textures.heightMap.sampler, textures.heightMap.view, vk::ImageLayout::eGeneral);

        // Color map image descriptor
        vk::DescriptorImageInfo texDescriptorColorMap =
            vkx::descriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Tessellation control shader ubo
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformDataTC.descriptor),
            // Binding 1 : Tessellation evaluation shader ubo
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                1,
                &uniformDataTE.descriptor),
            // Binding 2 : Displacement map
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eCombinedImageSampler,
                2,
                &texDescriptorDisplacementMap),
            // Binding 3 : Color map
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eCombinedImageSampler,
                3,
                &texDescriptorColorMap),
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::ePatchList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

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

        // Tessellation pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 4> shaderStages;
        shaderStages[0] = loadShader(getAssetPath() + "shaders/displacement/base.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/displacement/base.frag.spv", vk::ShaderStageFlagBits::eFragment);
        shaderStages[2] = loadShader(getAssetPath() + "shaders/displacement/displacement.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl);
        shaderStages[3] = loadShader(getAssetPath() + "shaders/displacement/displacement.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation);

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

        // Solid pipeline
        pipelines.solid = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Wireframe pipeline
        rasterizationState.polygonMode = vk::PolygonMode::eLine;
        pipelines.wire = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];


        // Pass through pipelines
        // Load pass through tessellation shaders (Vert and frag are reused)
        shaderStages[2] = loadShader(getAssetPath() + "shaders/displacement/passthrough.tesc.spv", vk::ShaderStageFlagBits::eTessellationControl);
        shaderStages[3] = loadShader(getAssetPath() + "shaders/displacement/passthrough.tese.spv", vk::ShaderStageFlagBits::eTessellationEvaluation);
        // Solid
        rasterizationState.polygonMode = vk::PolygonMode::eFill;
        pipelines.solidPassThrough = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Wireframe
        rasterizationState.polygonMode = vk::PolygonMode::eLine;
        pipelines.wirePassThrough = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Tessellation evaluation shader uniform buffer
        uniformDataTE = createBuffer(vk::BufferUsageFlagBits::eUniformBuffer, uboTE);

        // Tessellation control shader uniform buffer
        uniformDataTC = createBuffer(vk::BufferUsageFlagBits::eUniformBuffer, uboTC);

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Tessellation eval
        glm::mat4 viewMatrix = glm::mat4();
        uboTE.projection = glm::perspective(glm::radians(45.0f), (float)(width* ((splitScreen) ? 0.5f : 1.0f)) / (float)height, 0.1f, 256.0f);
        viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, zoom));

        float offset = 0.5f;
        int uboIndex = 1;
        uboTE.model = glm::mat4();
        uboTE.model = viewMatrix * glm::translate(uboTE.model, glm::vec3(0, 0, 0));
        uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        void *pData = device.mapMemory(uniformDataTE.memory, 0, sizeof(uboTE), vk::MemoryMapFlags());
        memcpy(pData, &uboTE, sizeof(uboTE));
        device.unmapMemory(uniformDataTE.memory);

        // Tessellation control
        pData = device.mapMemory(uniformDataTC.memory, 0, sizeof(uboTC), vk::MemoryMapFlags());
        memcpy(pData, &uboTC, sizeof(uboTC));
        device.unmapMemory(uniformDataTC.memory);
    }

    void prepare() {
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

    virtual void render() {
        if (!prepared)
            return;
        vkDeviceWaitIdle(device);
        draw();
        vkDeviceWaitIdle(device);
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }

    void changeTessellationLevel(float delta) {
        uboTC.tessLevel += delta;
        // Clamp
        uboTC.tessLevel = fmax(1.0, fmin(uboTC.tessLevel, 32.0));
        updateUniformBuffers();
    }

    void togglePipelines() {
        if (pipelineRight == &pipelines.solid) {
            pipelineRight = &pipelines.wire;
            pipelineLeft = &pipelines.wirePassThrough;
        } else {
            pipelineRight = &pipelines.solid;
            pipelineLeft = &pipelines.solidPassThrough;
        }
        reBuildCommandBuffers();
    }

    void toggleSplitScreen() {
        splitScreen = !splitScreen;
        reBuildCommandBuffers();
        updateUniformBuffers();
    }


    void keyPressed(uint32_t key) override {
        switch (key) {
        case GLFW_KEY_KP_ADD:
            changeTessellationLevel(0.25);
            break;
        case GLFW_KEY_KP_SUBTRACT:
            changeTessellationLevel(-0.25);
            break;
        case GLFW_KEY_W:
            togglePipelines();
            break;
        case GLFW_KEY_S:
            toggleSplitScreen();
            break;
        }
    }
};

RUN_EXAMPLE(VulkanExample)

