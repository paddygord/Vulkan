/*
* Vulkan Example - Parallax Mapping
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
    vkx::VertexLayout::VERTEX_LAYOUT_UV,
    vkx::VertexLayout::VERTEX_LAYOUT_NORMAL,
    vkx::VertexLayout::VERTEX_LAYOUT_TANGENT,
    vkx::VertexLayout::VERTEX_LAYOUT_BITANGENT
};

class VulkanExample : public vkx::ExampleBase {
public:
    bool splitScreen = true;

    struct {
        vkx::Texture colorMap;
        // Normals and height are combined in one texture (height = alpha channel)
        vkx::Texture normalHeightMap;
    } textures;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::MeshBuffer quad;
    } meshes;

    struct {
        vkx::UniformData vertexShader;
        vkx::UniformData fragmentShader;
    } uniformData;

    struct {

        struct {
            glm::mat4 projection;
            glm::mat4 model;
            glm::mat4 normal;
            glm::vec4 lightPos;
            glm::vec4 cameraPos;
        } vertexShader;

        struct FragmentShader {
            // Scale and bias control the parallax offset effect
            // They need to be tweaked for each material
            // Getting them wrong destroys the depth effect
            float scale = 0.06f;
            float bias = -0.04f;
            float lightRadius = 1.0f;
            int32_t usePom = 1;
            int32_t displayNormalMap = 0;
        } fragmentShader;

    } ubos;

    struct {
        vk::Pipeline parallaxMapping;
        vk::Pipeline normalMapping;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        camera.setZoom(-1.25f);
        camera.setRotation(40.0, -33.0, 0.0);
        rotationSpeed = 0.25f;
        paused = true;
        title = "Vulkan Example - Parallax Mapping";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class
        device.destroyPipeline(pipelines.parallaxMapping);
        device.destroyPipeline(pipelines.normalMapping);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        meshes.quad.destroy();

        uniformData.vertexShader.destroy();
        uniformData.fragmentShader.destroy();

        textures.colorMap.destroy();
        textures.normalHeightMap.destroy();
    }

    void loadTextures() {
        textures.colorMap = textureLoader->loadTexture(
            getAssetPath() + "textures/rocks_color_bc3.dds",
            vk::Format::eBc3UnormBlock);
        textures.normalHeightMap = textureLoader->loadTexture(
            getAssetPath() + "textures/rocks_normal_height_rgba.dds",
            vk::Format::eR8G8B8A8Unorm);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        vk::Viewport viewport = vkx::viewport((splitScreen) ? (float)size.width / 2.0f : (float)size.width, (float)size.height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.setScissor(0, vkx::rect2D(size));

        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);

        vk::DeviceSize offsets = 0;
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);

        // Parallax enabled
        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.parallaxMapping);
        cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 1);

        // Normal mapping
        if (splitScreen) {
            viewport.x = viewport.width;
            cmdBuffer.setViewport(0, viewport);
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.normalMapping);
            cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 1);
        }
    }

    void loadMeshes() {
        meshes.quad = loadMesh(getAssetPath() + "models/plane_z.obj", vertexLayout, 0.1f);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vkx::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vkx::vertexSize(vertexLayout), vk::VertexInputRate::eVertex);

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(5);
        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);
        // Location 1 : Texture coordinates
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32Sfloat, sizeof(float) * 3);
        // Location 2 : Normal
        vertices.attributeDescriptions[2] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32Sfloat, sizeof(float) * 5);
        // Location 3 : Tangent
        vertices.attributeDescriptions[3] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32B32Sfloat, sizeof(float) * 8);
        // Location 4 : Bitangent
        vertices.attributeDescriptions[4] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 4, vk::Format::eR32G32B32Sfloat, sizeof(float) * 11);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // Example uses two ubos and two image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 2),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 4);

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
            // Binding 1 : Fragment shader color map image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                1),
            // Binding 2 : Fragment combined normal and heightmap
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                2),
            // Binding 3 : Fragment shader uniform buffer
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eFragment,
                3)
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

        // Color map image descriptor
        vk::DescriptorImageInfo texDescriptorColorMap =
            vkx::descriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);

        vk::DescriptorImageInfo texDescriptorNormalHeightMap =
            vkx::descriptorImageInfo(textures.normalHeightMap.sampler, textures.normalHeightMap.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vertexShader.descriptor),
            // Binding 1 : Fragment shader image sampler
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptorColorMap),
            // Binding 2 : Combined normal and heightmap
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eCombinedImageSampler,
                2,
                &texDescriptorNormalHeightMap),
            // Binding 3 : Fragment shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                3,
                &uniformData.fragmentShader.descriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

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
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        // Parallax mapping pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
        shaderStages[0] = loadShader(getAssetPath() + "shaders/parallax/parallax.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/parallax/parallax.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

        pipelines.parallaxMapping = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];


        // Normal mapping (no parallax effect)
        shaderStages[0] = loadShader(getAssetPath() + "shaders/parallax/normalmap.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/parallax/normalmap.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines.normalMapping = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

    }

    void prepareUniformBuffers() {
        // Vertex shader ubo
        uniformData.vertexShader = createUniformBuffer(ubos.vertexShader);
        // Fragment shader ubo
        uniformData.fragmentShader = createUniformBuffer(ubos.fragmentShader);

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader
        ubos.vertexShader.projection = glm::perspective(glm::radians(45.0f), (float)(size.width* ((splitScreen) ? 0.5f : 1.0f)) / (float)size.height, 0.001f, 256.0f);
        ubos.vertexShader.model = camera.matrices.view;
        ubos.vertexShader.normal = glm::inverseTranspose(ubos.vertexShader.model);

        if (!paused) {
            ubos.vertexShader.lightPos.x = sin(glm::radians(timer * 360.0f)) * 0.5;
            ubos.vertexShader.lightPos.y = cos(glm::radians(timer * 360.0f)) * 0.5;
        }

        ubos.vertexShader.cameraPos = glm::vec4(0.0, 0.0, zoom, 0.0);
        uniformData.vertexShader.copy(ubos.vertexShader);

        // Fragment shader
        uniformData.fragmentShader.copy(ubos.fragmentShader);
    }

    void prepare() {
        ExampleBase::prepare();
        loadTextures();
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

    virtual void render() {
        if (!prepared)
            return;
        draw();
        if (!paused) {
            updateUniformBuffers();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }

    void toggleParallaxOffset() {
        ubos.fragmentShader.usePom = !ubos.fragmentShader.usePom;
        updateUniformBuffers();
    }

    void toggleNormalMapDisplay() {
        ubos.fragmentShader.displayNormalMap = !ubos.fragmentShader.displayNormalMap;
        updateUniformBuffers();
    }

    void toggleSplitScreen() {
        splitScreen = !splitScreen;
        updateUniformBuffers();
        updateDrawCommandBuffers();
    }

};

RUN_EXAMPLE(VulkanExample)
