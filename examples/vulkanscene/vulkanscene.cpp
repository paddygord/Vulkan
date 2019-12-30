/*
* Vulkan Demo Scene 
*
* Don't take this a an example, it's more of a personal playground
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* Note : Different license than the other examples!
*
* This code is licensed under the Mozilla Public License Version 2.0 (http://opensource.org/licenses/MPL-2.0)
*/

#include <vulkanExampleBase.hpp>
#include <vertex.hpp>
#include <model.cpp>

#define VERTEX_BUFFER_BIND_ID 0

class VulkanExample : public vkx::ExampleBase {
    using Parent = vkx::ExampleBase;

public:
    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
    } };

    struct DemoModel {
        vkx::model::Model model;
        vk::Pipeline* pipeline;

        void draw(const vk::CommandBuffer& cmdBuffer) const {
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
            cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, model.vertices.buffer, { 0 });
            cmdBuffer.bindIndexBuffer(model.indices.buffer, 0, vk::IndexType::eUint32);
            cmdBuffer.drawIndexed(model.indexCount, 1, 0, 0, 0);
        }
    };

    std::vector<DemoModel> demoModels;

    struct {
        vks::Buffer meshVS;
    } uniformData;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 normal;
        glm::mat4 view;
        glm::vec4 lightPos;
    } uboVS;

    struct {
        vks::texture::TextureCubeMap skybox;
    } textures;

    struct {
        vk::Pipeline logos;
        vk::Pipeline models;
        vk::Pipeline skybox;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    glm::vec4 lightPos = glm::vec4(1.0f, 2.0f, 0.0f, 0.0f);

    VulkanExample() {
        //zoom = -3.75f;
        rotationSpeed = 0.5f;
        //rotation = glm::vec3(15.0f, 0.f, 0.0f);
        title = "Vulkan Demo Scene - (c) 2016 by Sascha Willems";
        settings.overlay = true;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        device.destroy(pipelines.logos);
        device.destroy(pipelines.models);
        device.destroy(pipelines.skybox);
        device.destroy(pipelineLayout);
        device.destroy(descriptorSetLayout);

        uniformData.meshVS.destroy();

        for (auto& model : demoModels) {
            model.model.destroy();
        }

        textures.skybox.destroy();
    }

    void loadAssets() override {
        // Models
        std::vector<std::string> modelFiles{ "vulkanscenelogos.dae", "vulkanscenebackground.dae", "vulkanscenemodels.dae", "cube.obj" };
        std::vector<vk::Pipeline*> modelPipelines{ &pipelines.logos, &pipelines.models, &pipelines.models, &pipelines.skybox };
        for (auto i = 0; i < modelFiles.size(); i++) {
            DemoModel model;
            model.pipeline = modelPipelines[i];
            vkx::model::ModelCreateInfo modelCreateInfo(glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(0.0f));
            if (modelFiles[i] != "cube.obj") {
                modelCreateInfo.center.y += 1.15f;
            }
            model.model.loadFromFile(context, getAssetPath() + "models/" + modelFiles[i], vertexLayout);
            demoModels.push_back(model);
        }

        // Textures
        textures.skybox.loadFromFile(context, getAssetPath() + "textures/cubemap_vulkan.ktx", vk::Format::eR8G8B8A8Unorm);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setViewport(0, vk::Viewport{ 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f });
        cmdBuffer.setScissor(0, vk::Rect2D{ vk::Offset2D{ 0, 0 }, vk::Extent2D{ width, height } });
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        for (const auto& model : demoModels) {
            model.draw(cmdBuffer);
        }
        //drawUI(drawCmdBuffers[i]);
    }

    void setupDescriptorPool() {
        // Example uses one ubo and one image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes{
            { vk::DescriptorType::eUniformBuffer, 2 },
            { vk::DescriptorType::eCombinedImageSampler, 1 },
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo{ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() };
        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Vertex shader uniform buffer
            { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader color map image sampler
            { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout{ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() };
        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

        vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{ {}, 1, &descriptorSetLayout };
        pipelineLayout = device.createPipelineLayout(pipelineLayoutCreateInfo);
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, 1, &descriptorSetLayout };
        descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        // Cube map image descriptor
        vk::DescriptorImageInfo texDescriptorCubeMap{ textures.skybox.sampler, textures.skybox.view, vk::ImageLayout::eShaderReadOnlyOptimal };

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.meshVS.descriptor, nullptr },
            // Binding 1 : Fragment shader image sampler
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorCubeMap, nullptr, nullptr },
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder builder{ device, pipelineLayout, renderPass };
        builder.rasterizationState.frontFace = vk::FrontFace::eClockwise;
        builder.vertexInputState.bindingDescriptions = {
            { VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };

        // Attribute descriptions
        // Describes memory layout and shader positions
        builder.vertexInputState.attributeDescriptions = {
            vk::VertexInputAttributeDescription{ 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32A32Sfloat, 0 },
            vk::VertexInputAttributeDescription{ 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32A32Sfloat, sizeof(float) * 3 },
            vk::VertexInputAttributeDescription{ 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, sizeof(float) * 5 },
            vk::VertexInputAttributeDescription{ 3, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32A32Sfloat, sizeof(float) * 8 },
        };

        // Default mesh rendering pipeline
        builder.loadShader(getAssetPath() + "shaders/vulkanscene/mesh.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/vulkanscene/mesh.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines.models = builder.create();
        builder.destroyShaderModules();

        // Pipeline for the logos
        builder.loadShader(getAssetPath() + "shaders/vulkanscene/logo.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/vulkanscene/logo.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines.logos = builder.create();
        builder.destroyShaderModules();

        // Pipeline for the sky sphere
        builder.rasterizationState.cullMode = vk::CullModeFlagBits::eFront;  // Inverted culling
        builder.depthStencilState.depthWriteEnable = VK_FALSE;               // No depth writes
        builder.loadShader(getAssetPath() + "shaders/vulkanscene/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/vulkanscene/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines.skybox = builder.create();
        builder.destroyShaderModules();
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        auto bufferType = vk::BufferUsageFlagBits::eUniformBuffer;
        auto memoryFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        uniformData.meshVS = context.createBuffer(bufferType, memoryFlags, sizeof(uboVS));
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
        uboVS.view = glm::lookAt(glm::vec3(0, 0, -zoom), cameraPos, glm::vec3(0, 1, 0));

        uboVS.model = glm::mat4(1.0f);
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        uboVS.normal = glm::inverseTranspose(uboVS.view * uboVS.model);

        uboVS.lightPos = lightPos;

        uniformData.meshVS.map();
        memcpy(uniformData.meshVS.mapped, &uboVS, sizeof(uboVS));
        uniformData.meshVS.unmap();
    }

    void draw() {
        Parent::prepareFrame();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        queue.submit(submitInfo, nullptr);
        Parent::submitFrame();
    }

    void prepare() {
        Parent::prepare();
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
};

VULKAN_EXAMPLE_MAIN()