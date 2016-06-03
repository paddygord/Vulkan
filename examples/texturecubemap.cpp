/*
* Vulkan Example - Cube map texture loading and displaying
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
public:
    vkx::Texture cubeMap;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::MeshBuffer skybox, object;
    } meshes;

    struct {
        vkx::UniformData objectVS;
        vkx::UniformData skyboxVS;
    } uniformData;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVS;

    struct {
        vk::Pipeline skybox;
        vk::Pipeline reflect;
    } pipelines;

    struct {
        vk::DescriptorSet object;
        vk::DescriptorSet skybox;
    } descriptorSets;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        zoom = -4.0f;
        rotationSpeed = 0.25f;
        rotation = { -2.25f, -35.0f, 0.0f };
        title = "Vulkan Example - Cube map";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        // Clean up texture resources
        cubeMap.destroy();

        device.destroyPipeline(pipelines.skybox);
        device.destroyPipeline(pipelines.reflect);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        meshes.object.destroy();
        meshes.skybox.destroy();

        uniformData.objectVS.destroy();
        uniformData.skyboxVS.destroy();
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

            vk::Viewport viewport = vkx::viewport((float)width, (float)height, 0.0f, 1.0f);
            drawCmdBuffers[i].setViewport(0, viewport);

            vk::Rect2D scissor = vkx::rect2D(width, height, 0, 0);
            drawCmdBuffers[i].setScissor(0, scissor);

            vk::DeviceSize offsets = 0;

            // Skybox
            drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.skybox, nullptr);
            drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.skybox.vertices.buffer, offsets);
            drawCmdBuffers[i].bindIndexBuffer(meshes.skybox.indices.buffer, 0, vk::IndexType::eUint32);
            drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skybox);
            drawCmdBuffers[i].drawIndexed(meshes.skybox.indexCount, 1, 0, 0, 0);

            // 3D object
            drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.object, nullptr);
            drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.object.vertices.buffer, offsets);
            drawCmdBuffers[i].bindIndexBuffer(meshes.object.indices.buffer, 0, vk::IndexType::eUint32);
            drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.reflect);
            drawCmdBuffers[i].drawIndexed(meshes.object.indexCount, 1, 0, 0, 0);

            drawCmdBuffers[i].endRenderPass();

            drawCmdBuffers[i].end();
        }
    }

    void loadMeshes() {
        meshes.object = loadMesh(getAssetPath() + "models/sphere.obj", vertexLayout, 0.05f);
        meshes.skybox = loadMesh(getAssetPath() + "models/cube.obj", vertexLayout, 0.05f);
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
        // Location 1 : Normal
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3);
        // Location 2 : Texture coordinates
        vertices.attributeDescriptions[2] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 5);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
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
            // Binding 0 : Vertex shader uniform buffer
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eVertex,
                0),
            // Binding 1 : Fragment shader image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                1)
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

        pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);
    }

    void setupDescriptorSets() {
        // vk::Image descriptor for the cube map texture
        vk::DescriptorImageInfo cubeMapDescriptor =
            vkx::descriptorImageInfo(cubeMap.sampler, cubeMap.view, vk::ImageLayout::eGeneral);

        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        // 3D object descriptor set
        descriptorSets.object = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.object,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.objectVS.descriptor),
            // Binding 1 : Fragment shader cubemap sampler
            vkx::writeDescriptorSet(
                descriptorSets.object,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &cubeMapDescriptor)
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);

        // Sky box descriptor set
        descriptorSets.skybox = device.allocateDescriptorSets(allocInfo)[0];

        writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.skybox,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.skyboxVS.descriptor),
            // Binding 1 : Fragment shader cubemap sampler
            vkx::writeDescriptorSet(
                descriptorSets.skybox,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &cubeMapDescriptor)
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

        vk::PipelineColorBlendAttachmentState blendAttachmentState =
            vkx::pipelineColorBlendAttachmentState();

        vk::PipelineColorBlendStateCreateInfo colorBlendState =
            vkx::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vkx::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual);

        vk::PipelineViewportStateCreateInfo viewportState =
            vkx::pipelineViewportStateCreateInfo(1, 1);

        vk::PipelineMultisampleStateCreateInfo multisampleState;

        std::vector<vk::DynamicState> dynamicStateEnables = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        // Skybox pipeline (background cube)
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/cubemap/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/cubemap/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment);

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
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();

        pipelines.skybox = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Cube map reflect pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/cubemap/reflect.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/cubemap/reflect.frag.spv", vk::ShaderStageFlagBits::eFragment);
        depthStencilState.depthWriteEnable = VK_TRUE;
        pipelines.reflect = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // 3D objact
        uniformData.objectVS = createUniformBuffer(uboVS);
        uniformData.objectVS.map();

        // Skybox
        uniformData.skyboxVS = createUniformBuffer(uboVS);
        uniformData.skyboxVS.map();
    }

    void updateUniformBuffers() {
        // 3D object
        glm::mat4 viewMatrix = glm::mat4();
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
        viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, zoom));

        uboVS.model = glm::mat4();
        uboVS.model = viewMatrix * glm::translate(uboVS.model, glm::vec3(0, 0, 0));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        uniformData.objectVS.copy(uboVS);

        // Skysphere
        viewMatrix = glm::mat4();
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);

        uboVS.model = glm::mat4();
        uboVS.model = viewMatrix * glm::translate(uboVS.model, glm::vec3(0, 0, 0));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        uniformData.skyboxVS.copy(uboVS);
    }

    void prepare() {
        ExampleBase::prepare();
        loadMeshes();
        setupVertexDescriptions();
        prepareUniformBuffers();
        cubeMap = textureLoader->loadCubemap(getAssetPath() + "textures/cubemap_yokohama.ktx", vk::Format::eBc3UnormBlock);
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
        vkDeviceWaitIdle(device);
        draw();
        vkDeviceWaitIdle(device);
        updateUniformBuffers();
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }
};

RUN_EXAMPLE(VulkanExample)
