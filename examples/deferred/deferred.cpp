/*
* Vulkan Example - Deferred shading with multiple render targets (aka G-Buffer) example
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vkx/vulkanOffscreenExampleBase.hpp>
#include <vkx/texture.hpp>
#include <vkx/model.hpp>

#define VERTEX_BUFFER_BIND_ID 0

// Texture properties
#define TEX_DIM 2048
#define TEX_FILTER VK_FILTER_LINEAR

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM

class VulkanExample : public VulkanExampleBase {
public:
    bool debugDisplay = false;

    struct {
        struct {
            vkx::texture::Texture2D colorMap;
            vkx::texture::Texture2D normalMap;
        } model;
        struct {
            vkx::texture::Texture2D colorMap;
            vkx::texture::Texture2D normalMap;
        } floor;
    } textures;

    vkx::Offscreen offscreen;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_UV,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_TANGENT,
    } };

    struct {
        vkx::model::Model model;
        vkx::model::Model floor;
        vkx::model::Model quad;
    } models;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 view;
        glm::vec4 instancePos[3];
    } uboVS, uboOffscreenVS;

    struct Light {
        glm::vec4 position;
        glm::vec3 color;
        float radius;
    };

    struct {
        Light lights[6];
        glm::vec4 viewPos;
    } uboFragmentLights;

    struct {
        vks::Buffer vsFullScreen;
        vks::Buffer vsOffscreen;
        vks::Buffer fsLights;
    } uniformBuffers;

    struct {
        vk::Pipeline deferred;
        vk::Pipeline offscreen;
        vk::Pipeline debug;
    } pipelines;

    struct {
        vk::PipelineLayout deferred;
        vk::PipelineLayout offscreen;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet model;
        vk::DescriptorSet floor;
    } descriptorSets;

    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample()
        : VulkanExampleBase() {
        title = "Deferred shading (2016 by Sascha Willems)";
        camera.type = Camera::CameraType::firstperson;
        camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
        camera.rotationSpeed = 0.25f;
#endif
        camera.position = { 2.15f, 0.3f, -8.75f };
        camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
        settings.overlay = true;
        offscreen.colorFormats = {
            vk::Format::eR16G16B16A16Sfloat,
            vk::Format::eR16G16B16A16Sfloat,
            vk::Format::eR8G8B8A8Unorm,
        };
        offscreen.depthFormat = vk::Format::eD32SfloatS8Uint;
        offscreen.size = { FB_DIM, FB_DIM };
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        offscreen.destroy();

        device.destroy(pipelines.deferred);
        device.destroy(pipelines.offscreen);
        device.destroy(pipelines.debug);
        device.destroy(pipelineLayouts.deferred);
        device.destroy(pipelineLayouts.offscreen);
        device.destroy(descriptorSetLayout);

        // Meshes
        models.model.destroy();
        models.floor.destroy();
        models.quad.destroy();

        // Uniform buffers
        uniformBuffers.vsOffscreen.destroy();
        uniformBuffers.vsFullScreen.destroy();
        uniformBuffers.fsLights.destroy();

        textures.model.colorMap.destroy();
        textures.model.normalMap.destroy();
        textures.floor.colorMap.destroy();
        textures.floor.normalMap.destroy();
    }

    // Enable physical device features required for this example
    virtual void getEnabledFeatures() {
        // Enable anisotropic filtering if supported
        if (deviceFeatures.samplerAnisotropy) {
            enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
        // Enable texture compression
        if (deviceFeatures.textureCompressionBC) {
            enabledFeatures.textureCompressionBC = VK_TRUE;
        } else if (deviceFeatures.textureCompressionASTC_LDR) {
            enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
        } else if (deviceFeatures.textureCompressionETC2) {
            enabledFeatures.textureCompressionETC2 = VK_TRUE;
        }
    };

    // Prepare a new framebuffer and attachments for offscreen rendering (G-Buffer)

    // Build command buffer for rendering the scene to the offscreen frame buffer attachments
    void buildDeferredCommandBuffer() {
        // Clear values for all attachments written in the fragment sahder
        std::array<vk::ClearValue, 4> clearValues;
        clearValues[0].color = vks::util::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        clearValues[1].color = vks::util::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        clearValues[2].color = vks::util::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        clearValues[3].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = offscreen.renderPass;
        renderPassBeginInfo.framebuffer = offscreen.framebuffers[0].framebuffer;
        renderPassBeginInfo.renderArea.extent = offscreen.size;
        renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassBeginInfo.pClearValues = clearValues.data();

        offscreen.cmdBuffer.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eSimultaneousUse });
        offscreen.cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        offscreen.cmdBuffer.setViewport(0, vks::util::viewport(offscreen.size));
        offscreen.cmdBuffer.setScissor(0, vks::util::rect2D(offscreen.size));
        offscreen.cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.offscreen);

        // Background
        //offscreen.cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.floor, nullptr);
        //offscreen.cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, models.floor.vertices.buffer, { 0 });
        //offscreen.cmdBuffer.bindIndexBuffer(models.floor.indices.buffer, 0, vk::IndexType::eUint32);
        //offscreen.cmdBuffer.drawIndexed(models.floor.indexCount, 1, 0, 0, 0);

        // Object
        offscreen.cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.model, nullptr);
        offscreen.cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, models.model.vertices.buffer, { 0 });
        offscreen.cmdBuffer.bindIndexBuffer(models.model.indices.buffer, 0, vk::IndexType::eUint32);
        offscreen.cmdBuffer.drawIndexed(models.model.indexCount, 3, 0, 0, 0);
        offscreen.cmdBuffer.endRenderPass();
        offscreen.cmdBuffer.end();
    }

    void loadAssets() override {
        models.model.loadFromFile(context, getAssetPath() + "models/armor/armor.dae", vertexLayout, 1.0f);

        vkx::model::ModelCreateInfo modelCreateInfo;
        modelCreateInfo.scale = glm::vec3(2.0f);
        modelCreateInfo.uvscale = glm::vec2(4.0f);
        modelCreateInfo.center = glm::vec3(0.0f, 2.35f, 0.0f);
        models.floor.loadFromFile(context, getAssetPath() + "models/plane.obj", vertexLayout, modelCreateInfo);

        // Textures
        std::string texFormatSuffix;
        vk::Format texFormat;
        // Get supported compressed texture format
        if (context.enabledFeatures.textureCompressionBC) {
            texFormatSuffix = "_bc3_unorm";
            texFormat = vk::Format::eBc3UnormBlock;
        } else if (context.enabledFeatures.textureCompressionASTC_LDR) {
            texFormatSuffix = "_astc_8x8_unorm";
            texFormat = vk::Format::eAstc8x8UnormBlock;
        } else if (context.enabledFeatures.textureCompressionETC2) {
            texFormatSuffix = "_etc2_unorm";
            texFormat = vk::Format::eEtc2R8G8B8A8UnormBlock;
        } else {
            vks::tools::exitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
        }

        textures.model.colorMap.loadFromFile(context, getAssetPath() + "models/armor/color" + texFormatSuffix + ".ktx", texFormat);
        textures.model.normalMap.loadFromFile(context, getAssetPath() + "models/armor/normal" + texFormatSuffix + ".ktx", texFormat);
        textures.floor.colorMap.loadFromFile(context, getAssetPath() + "textures/stonefloor01_color" + texFormatSuffix + ".ktx", texFormat);
        textures.floor.normalMap.loadFromFile(context, getAssetPath() + "textures/stonefloor01_normal" + texFormatSuffix + ".ktx", texFormat);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& drawCmdBuffer) override {
        vk::Viewport viewport = vks::util::viewport(size);
        drawCmdBuffer.setViewport(0, viewport);

        vk::Rect2D scissor = vks::util::rect2D(size);
        drawCmdBuffer.setScissor(0, 1, &scissor);

        vk::DeviceSize offsets[1] = { 0 };
        drawCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.deferred, 0, descriptorSet, nullptr);

        if (debugDisplay) {
            drawCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.debug);
            drawCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, 1, &models.quad.vertices.buffer, offsets);
            drawCmdBuffer.bindIndexBuffer(models.quad.indices.buffer, 0, vk::IndexType::eUint32);
            drawCmdBuffer.drawIndexed(models.quad.indexCount, 1, 0, 0, 1);
            // Move viewport to display final composition in lower right corner
            viewport.x = viewport.width * 0.5f;
            viewport.y = viewport.height * 0.5f;
            viewport.width = viewport.width * 0.5f;
            viewport.height = viewport.height * 0.5f;
            drawCmdBuffer.setViewport(0, 1, &viewport);
        }

        // Final composition as full screen quad
        drawCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.deferred);
        drawCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, 1, &models.quad.vertices.buffer, offsets);
        drawCmdBuffer.bindIndexBuffer(models.quad.indices.buffer, 0, vk::IndexType::eUint32);
        drawCmdBuffer.drawIndexed(6, 1, 0, 0, 1);
        //drawUI(drawCmdBuffers[i]);
    }

    struct Vertex {
        vec3 pos;
        vec2 uv;
        vec3 col;
        vec3 normal;
        vec3 tangent;
    };

    void generateQuads() {
        // Setup vertices for multiple screen aligned quads
        // Used for displaying final result and debug

        std::vector<Vertex> vertexBuffer;

        float x = 0.0f;
        float y = 0.0f;
        for (uint32_t i = 0; i < 3; i++) {
            // Last component of normal is used for debug display sampler index
            vertexBuffer.push_back({ { x + 1.0f, y + 1.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
            vertexBuffer.push_back({ { x, y + 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
            vertexBuffer.push_back({ { x, y, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
            vertexBuffer.push_back({ { x + 1.0f, y, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, (float)i } });
            x += 1.0f;
            if (x > 1.0f) {
                x = 0.0f;
                y += 1.0f;
            }
        }

        models.quad.vertices = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);
        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0, 1, 2, 2, 3, 0 };
        for (uint32_t i = 0; i < 3; ++i) {
            uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
            for (auto index : indices) {
                indexBuffer.push_back(i * 4 + index);
            }
        }
        models.quad.indexCount = static_cast<uint32_t>(indexBuffer.size());
        models.quad.indices = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
        models.quad.device = device;
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes{
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 8 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 9 },
        };
        descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 3, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        // Deferred shading layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Vertex shader uniform buffer
            { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Position texture target / Scene colormap
            { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
            // Binding 2 : Normals texture target
            { 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
            // Binding 3 : Albedo texture target
            { 3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
            // Binding 4 : Fragment shader uniform buffer
            { 4, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment },
        };

        descriptorSetLayout = device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo{ {}, static_cast<uint32_t>(setLayoutBindings.size()), setLayoutBindings.data() });
        pipelineLayouts.deferred = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, 1, &descriptorSetLayout });

        // Offscreen (scene) rendering pipeline layout
        pipelineLayouts.offscreen = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        {
            descriptorSet = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool, 1, &descriptorSetLayout })[0];
            descriptorSets.model = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool, 1, &descriptorSetLayout })[0];
            descriptorSets.floor = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool, 1, &descriptorSetLayout })[0];
        }

        const auto& colors = offscreen.framebuffers[0].colors;
        // Image descriptors for the offscreen color attachments
        vk::DescriptorImageInfo texDescriptorPosition{ offscreen.sampler, colors[0].view, vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::DescriptorImageInfo texDescriptorNormal{ offscreen.sampler, colors[1].view, vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::DescriptorImageInfo texDescriptorAlbedo{ offscreen.sampler, colors[2].view, vk::ImageLayout::eShaderReadOnlyOptimal };

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets;
        writeDescriptorSets = {
            // Textured quad descriptor set
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformBuffers.vsFullScreen.descriptor },
            // Binding 1 : Position texture target
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorPosition },
            // Binding 2 : Normals texture target
            vk::WriteDescriptorSet{ descriptorSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorNormal },
            // Binding 3 : Albedo texture target
            vk::WriteDescriptorSet{ descriptorSet, 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorAlbedo },
            // Binding 4 : Fragment shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSet, 4, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformBuffers.fsLights.descriptor },
            // Model
            // Binding 0: Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSets.model, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformBuffers.vsOffscreen.descriptor },
            // Binding 1: Color map
            vk::WriteDescriptorSet{ descriptorSets.model, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &textures.model.colorMap.descriptor },
            // Binding 2: Normal map
            vk::WriteDescriptorSet{ descriptorSets.model, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &textures.model.normalMap.descriptor },
            // Background
            // Binding 0: Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSets.floor, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformBuffers.vsOffscreen.descriptor },
            // Binding 1: Color map
            vk::WriteDescriptorSet{ descriptorSets.model, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &textures.floor.colorMap.descriptor },
            // Binding 2: Normal map
            vk::WriteDescriptorSet{ descriptorSets.model, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &textures.floor.normalMap.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        {
            vks::pipelines::GraphicsPipelineBuilder builder{ device, pipelineLayouts.deferred, renderPass };
            builder.rasterizationState.frontFace = vk::FrontFace::eClockwise;
            // Final fullscreen composition pass pipeline
            // Empty vertex input state, quads are generated by the vertex shader
            builder.loadShader(getAssetPath() + "shaders/deferred/deferred.vert.spv", vk::ShaderStageFlagBits::eVertex);
            builder.loadShader(getAssetPath() + "shaders/deferred/deferred.frag.spv", vk::ShaderStageFlagBits::eFragment);
            pipelines.deferred = builder.create(context.pipelineCache);
        }

        // Binding description
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions{
            vk::VertexInputBindingDescription{ VERTEX_BUFFER_BIND_ID, vertexLayout.stride() },
        };
        // Attribute descriptions
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions{
            // Location 0: Position
            vk::VertexInputAttributeDescription{ 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos) },
            // Location 1: Texture coordinates
            vk::VertexInputAttributeDescription{ 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv) },
            // Location 2: Color
            vk::VertexInputAttributeDescription{ 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, col) },
            // Location 3: Normal
            vk::VertexInputAttributeDescription{ 3, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal) },
            // Location 4: Tangent
            vk::VertexInputAttributeDescription{ 4, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, tangent) },
        };

        // Debug display pipeline
        {
            vks::pipelines::GraphicsPipelineBuilder builder{ device, pipelineLayouts.deferred, renderPass };
            builder.rasterizationState.frontFace = vk::FrontFace::eClockwise;
            builder.loadShader(getAssetPath() + "shaders/deferred/debug.vert.spv", vk::ShaderStageFlagBits::eVertex);
            builder.loadShader(getAssetPath() + "shaders/deferred/debug.frag.spv", vk::ShaderStageFlagBits::eFragment);
            builder.vertexInputState.bindingDescriptions = bindingDescriptions;
            builder.vertexInputState.attributeDescriptions = attributeDescriptions;
            pipelines.debug = builder.create(context.pipelineCache);
        }

        // Offscreen pipeline
        {
            vks::pipelines::GraphicsPipelineBuilder builder{ device, pipelineLayouts.offscreen, offscreen.renderPass };
            builder.rasterizationState.frontFace = vk::FrontFace::eClockwise;
            builder.loadShader(getAssetPath() + "shaders/deferred/mrt.vert.spv", vk::ShaderStageFlagBits::eVertex);
            builder.loadShader(getAssetPath() + "shaders/deferred/mrt.frag.spv", vk::ShaderStageFlagBits::eFragment);
            builder.vertexInputState.bindingDescriptions = bindingDescriptions;
            builder.vertexInputState.attributeDescriptions = attributeDescriptions;
            builder.colorBlendState.blendAttachmentStates.resize(3);
            pipelines.offscreen = builder.create(context.pipelineCache);
        }
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Init some values
        uboOffscreenVS.instancePos[0] = glm::vec4(0.0f);
        uboOffscreenVS.instancePos[1] = glm::vec4(-4.0f, 0.0, -4.0f, 0.0f);
        uboOffscreenVS.instancePos[2] = glm::vec4(4.0f, 0.0, -4.0f, 0.0f);

        // Fullscreen vertex shader
        uniformBuffers.vsFullScreen = context.createUniformBuffer(uboVS);
        // Deferred vertex shader
        uniformBuffers.vsOffscreen = context.createUniformBuffer(uboOffscreenVS);
        // Deferred fragment shader
        uniformBuffers.fsLights = context.createUniformBuffer(uboFragmentLights);

        // Update
        updateUniformBuffersScreen();
        updateUniformBufferDeferredMatrices();
        updateUniformBufferDeferredLights();
    }

    void updateUniformBuffersScreen() {
        if (debugDisplay) {
            uboVS.projection = glm::ortho(0.0f, 2.0f, 0.0f, 2.0f, -1.0f, 1.0f);
        } else {
            uboVS.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
        }
        uboVS.model = glm::mat4(1.0f);

        memcpy(uniformBuffers.vsFullScreen.mapped, &uboVS, sizeof(uboVS));
    }

    void updateUniformBufferDeferredMatrices() {
        uboOffscreenVS.projection = camera.matrices.perspective;
        uboOffscreenVS.view = camera.matrices.view;
        uboOffscreenVS.model = glm::mat4(1.0f);

        memcpy(uniformBuffers.vsOffscreen.mapped, &uboOffscreenVS, sizeof(uboOffscreenVS));
    }

    // Update fragment shader light position uniform block
    void updateUniformBufferDeferredLights() {
        // White
        uboFragmentLights.lights[0].position = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        uboFragmentLights.lights[0].color = glm::vec3(1.5f);
        uboFragmentLights.lights[0].radius = 15.0f * 0.25f;
        // Red
        uboFragmentLights.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
        uboFragmentLights.lights[1].color = glm::vec3(1.0f, 0.0f, 0.0f);
        uboFragmentLights.lights[1].radius = 15.0f;
        // Blue
        uboFragmentLights.lights[2].position = glm::vec4(2.0f, 1.0f, 0.0f, 0.0f);
        uboFragmentLights.lights[2].color = glm::vec3(0.0f, 0.0f, 2.5f);
        uboFragmentLights.lights[2].radius = 5.0f;
        // Yellow
        uboFragmentLights.lights[3].position = glm::vec4(0.0f, 0.9f, 0.5f, 0.0f);
        uboFragmentLights.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
        uboFragmentLights.lights[3].radius = 2.0f;
        // Green
        uboFragmentLights.lights[4].position = glm::vec4(0.0f, 0.5f, 0.0f, 0.0f);
        uboFragmentLights.lights[4].color = glm::vec3(0.0f, 1.0f, 0.2f);
        uboFragmentLights.lights[4].radius = 5.0f;
        // Yellow
        uboFragmentLights.lights[5].position = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        uboFragmentLights.lights[5].color = glm::vec3(1.0f, 0.7f, 0.3f);
        uboFragmentLights.lights[5].radius = 25.0f;

        uboFragmentLights.lights[0].position.x = sin(glm::radians(360.0f * timer)) * 5.0f;
        uboFragmentLights.lights[0].position.z = cos(glm::radians(360.0f * timer)) * 5.0f;

        uboFragmentLights.lights[1].position.x = -4.0f + sin(glm::radians(360.0f * timer) + 45.0f) * 2.0f;
        uboFragmentLights.lights[1].position.z = 0.0f + cos(glm::radians(360.0f * timer) + 45.0f) * 2.0f;

        uboFragmentLights.lights[2].position.x = 4.0f + sin(glm::radians(360.0f * timer)) * 2.0f;
        uboFragmentLights.lights[2].position.z = 0.0f + cos(glm::radians(360.0f * timer)) * 2.0f;

        uboFragmentLights.lights[4].position.x = 0.0f + sin(glm::radians(360.0f * timer + 90.0f)) * 5.0f;
        uboFragmentLights.lights[4].position.z = 0.0f - cos(glm::radians(360.0f * timer + 45.0f)) * 5.0f;

        uboFragmentLights.lights[5].position.x = 0.0f + sin(glm::radians(-360.0f * timer + 135.0f)) * 10.0f;
        uboFragmentLights.lights[5].position.z = 0.0f - cos(glm::radians(-360.0f * timer - 45.0f)) * 10.0f;

        // Current view position
        uboFragmentLights.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

        memcpy(uniformBuffers.fsLights.mapped, &uboFragmentLights, sizeof(uboFragmentLights));
    }

    void draw() {
        VulkanExampleBase::prepareFrame();

        // The scene render command buffer has to wait for the offscreen
        // rendering to be finished before we can use the framebuffer
        // color image for sampling during final rendering
        // To ensure this we use a dedicated offscreen synchronization
        // semaphore that will be signaled when offscreen renderin
        // has been finished
        // This is necessary as an implementation may start both
        // command buffers at the same time, there is no guarantee
        // that command buffers will be executed in the order they
        // have been submitted by the application

        // Offscreen rendering
        // Wait for swap chain presentation to finish
        synchronization.renderWaitSemaphores = { semaphores.acquireComplete };
        synchronization.renderWaitStages = { vk::PipelineStageFlagBits::eBottomOfPipe };
        synchronization.renderSignalSemaphores = { offscreen.renderComplete };

        submitWithSynchronization(offscreen.cmdBuffer);

        // Scene rendering
        // Wait for offscreen semaphore
        synchronization.renderWaitSemaphores = { offscreen.renderComplete };
        synchronization.renderWaitStages = { vk::PipelineStageFlagBits::eBottomOfPipe };
        synchronization.renderSignalSemaphores = { semaphores.renderComplete };
        // Submit work
        drawCurrentCommandBuffer();

        VulkanExampleBase::submitFrame();
    }

    void prepare() {
        VulkanExampleBase::prepare();
        offscreen.prepare(context);

        generateQuads();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        buildDeferredCommandBuffer();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        updateUniformBufferDeferredLights();
    }

    void viewChanged() override {
        updateUniformBufferDeferredMatrices();
    }

    void OnUpdateUIOverlay(vks::UIOverlay* overlay) override {
        if (overlay->header("Settings")) {
            if (overlay->checkBox("Display render targets", &debugDisplay)) {
                buildCommandBuffers();
                updateUniformBuffersScreen();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
