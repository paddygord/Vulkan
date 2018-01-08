/*
* Vulkan Example - Multi pass offscreen rendering (bloom)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanOffscreenExampleBase.hpp>
#include <vks/model.hpp>
#include <vks/texture.hpp>

// Texture properties
#define TEX_DIM 256

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM
#define FB_COLOR_FORMAT TEX_FORMAT

// Vertex layout for this example
vks::model::VertexLayout vertexLayout{ {
    vks::model::Component::VERTEX_COMPONENT_POSITION,
    vks::model::Component::VERTEX_COMPONENT_UV,
    vks::model::Component::VERTEX_COMPONENT_COLOR,
    vks::model::Component::VERTEX_COMPONENT_NORMAL,
} };

class VulkanExample : public vkx::OffscreenExampleBase {
    using Parent = OffscreenExampleBase;
public:
    bool bloom = true;

    struct {
        vks::texture::TextureCubeMap cubemap;
    } textures;

    struct {
        vks::model::Model ufo;
        vks::model::Model ufoGlow;
        vks::model::Model skyBox;
        vks::model::Model quad;
    } meshes;

    struct {
        vks::Buffer vsScene;
        vks::Buffer vsFullScreen;
        vks::Buffer vsSkyBox;
        vks::Buffer fsVertBlur;
        vks::Buffer fsHorzBlur;
    } uniformData;

    struct UBO {
        glm::mat4 projection;
        glm::mat4 model;
    };

    struct UBOBlur {
        int32_t texWidth = TEX_DIM;
        int32_t texHeight = TEX_DIM;
        float blurScale = 1.0f;
        float blurStrength = 1.5f;
        uint32_t horizontal;
    };

    struct {
        UBO scene, fullscreen, skyBox;
        UBOBlur vertBlur, horzBlur;
    } ubos;

    struct {
        vk::Pipeline blur;
        vk::Pipeline colorPass;
        vk::Pipeline phongPass;
        vk::Pipeline skyBox;
    } pipelines;

    struct {
        vk::PipelineLayout radialBlur;
        vk::PipelineLayout scene;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet scene;
        vk::DescriptorSet verticalBlur;
        vk::DescriptorSet horizontalBlur;
        vk::DescriptorSet skyBox;
    } descriptorSets;

    // Descriptor set layout is shared amongst
    // all descriptor sets
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() : vkx::OffscreenExampleBase() {
        camera.setPosition(glm::vec3(0.0f, 0.0f, -10.25f));
        camera.setRotation(glm::vec3(7.5f, -343.0f, 0.0f));
        timerSpeed *= 0.5f;
        title = "Vulkan Example - Bloom";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        device.destroyPipeline(pipelines.blur);
        device.destroyPipeline(pipelines.phongPass);
        device.destroyPipeline(pipelines.colorPass);
        device.destroyPipeline(pipelines.skyBox);

        device.destroyPipelineLayout(pipelineLayouts.radialBlur);
        device.destroyPipelineLayout(pipelineLayouts.scene);

        device.destroyDescriptorSetLayout(descriptorSetLayout);

        // Meshes
        meshes.ufo.destroy();
        meshes.ufoGlow.destroy();
        meshes.skyBox.destroy();
        meshes.quad.destroy();

        // Uniform buffers
        uniformData.vsScene.destroy();
        uniformData.vsFullScreen.destroy();
        uniformData.vsSkyBox.destroy();
        uniformData.fsVertBlur.destroy();
        uniformData.fsHorzBlur.destroy();
        textures.cubemap.destroy();
    }


    // Render the 3D scene into a texture target
    void buildOffscreenCommandBuffer() override {
        vk::Viewport viewport = vks::util::viewport(offscreen.size);
        vk::Rect2D scissor = vks::util::rect2D(offscreen.size);
        vk::DeviceSize offset = 0;

        // Horizontal blur
        vk::ClearValue clearValues[2];
        clearValues[0].color = vks::util::clearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };


        offscreen.cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
        vk::CommandBufferBeginInfo cmdBufInfo;
        cmdBufInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
        offscreen.cmdBuffer.begin(cmdBufInfo);

        // Draw the unblurred geometry to framebuffer 1
        offscreen.cmdBuffer.setViewport(0, viewport);
        offscreen.cmdBuffer.setScissor(0, scissor);

        // Draw the bloom geometry.
        {
            vk::RenderPassBeginInfo renderPassBeginInfo;
            renderPassBeginInfo.renderPass = offscreen.renderPass;
            renderPassBeginInfo.framebuffer = offscreen.framebuffers[0].framebuffer;
            renderPassBeginInfo.renderArea.extent.width = offscreen.size.x;
            renderPassBeginInfo.renderArea.extent.height = offscreen.size.y;
            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;
            offscreen.cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            offscreen.cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);
            offscreen.cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.phongPass);
            offscreen.cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.ufoGlow.vertices.buffer, offset);
            offscreen.cmdBuffer.bindIndexBuffer(meshes.ufoGlow.indices.buffer, 0, vk::IndexType::eUint32);
            offscreen.cmdBuffer.drawIndexed(meshes.ufoGlow.indexCount, 1, 0, 0, 0);
            offscreen.cmdBuffer.endRenderPass();
        }

        {
            vk::RenderPassBeginInfo renderPassBeginInfo;
            renderPassBeginInfo.renderPass = offscreen.renderPass;
            renderPassBeginInfo.framebuffer = offscreen.framebuffers[1].framebuffer;
            renderPassBeginInfo.renderArea.extent.width = offscreen.size.x;
            renderPassBeginInfo.renderArea.extent.height = offscreen.size.y;
            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;
            // Draw a vertical blur pass from framebuffer 1's texture into framebuffer 2
            offscreen.cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            offscreen.cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.radialBlur, 0, descriptorSets.verticalBlur, nullptr);
            offscreen.cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.blur);
            offscreen.cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, offset);
            offscreen.cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
            offscreen.cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
            offscreen.cmdBuffer.endRenderPass();
        }
        offscreen.cmdBuffer.end();
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        vk::DeviceSize offset = 0;
        cmdBuffer.setViewport(0, vks::util::viewport(size));
        cmdBuffer.setScissor(0, vks::util::rect2D(size));

        // Skybox 
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.skyBox, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skyBox);
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.skyBox.vertices.buffer, offset);
        cmdBuffer.bindIndexBuffer(meshes.skyBox.indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(meshes.skyBox.indexCount, 1, 0, 0, 0);

        // 3D scene
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.phongPass);
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.ufo.vertices.buffer, offset);
        cmdBuffer.bindIndexBuffer(meshes.ufo.indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(meshes.ufo.indexCount, 1, 0, 0, 0);

        // Render vertical blurred scene applying a horizontal blur
        if (bloom) {
            context.setImageLayout(
                cmdBuffer,
                offscreen.framebuffers[1].colors[0].image,
                vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal);

            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.radialBlur, 0, descriptorSets.horizontalBlur, nullptr);
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.blur);
            cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, offset);
            cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
            cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
        }
    }

    // Setup vertices for a single uv-mapped quad
    void generateQuad() {
        struct Vertex {
            glm::vec3 pos;
            glm::vec2 uv;
            glm::vec3 col;
            glm::vec3 normal;
        };

#define QUAD_COLOR_NORMAL { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }
        std::vector<Vertex> vertexBuffer =
        {
            { { 1.0f, 1.0f, 0.0f },{ 1.0f, 1.0f }, QUAD_COLOR_NORMAL },
            { { 0.0f, 1.0f, 0.0f },{ 0.0f, 1.0f }, QUAD_COLOR_NORMAL },
            { { 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f }, QUAD_COLOR_NORMAL },
            { { 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f }, QUAD_COLOR_NORMAL }
        };
#undef QUAD_COLOR_NORMAL
        meshes.quad.vertices = context.createBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
        meshes.quad.indexCount = (uint32_t)indexBuffer.size();
        meshes.quad.indices = context.createBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes {
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 8 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 6},
        };

        descriptorPool = device.createDescriptorPool(
            vk::DescriptorPoolCreateInfo{ {}, 5, (uint32_t)poolSizes.size(), poolSizes.data() }
        );
    }

    void setupDescriptorSetLayout() {
        // Textured quad pipeline layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings {
            // Binding 0 : Vertex shader uniform buffer
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eUniformBuffer, 1,  vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader image sampler
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1,  vk::ShaderStageFlagBits::eFragment },
            // Binding 2 : Framgnet shader uniform buffer
            vk::DescriptorSetLayoutBinding{ 2, vk::DescriptorType::eUniformBuffer, 1,  vk::ShaderStageFlagBits::eFragment },
        };

        descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });

        vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{ {}, 1, &descriptorSetLayout };

        pipelineLayouts.radialBlur = device.createPipelineLayout(pipelineLayoutCreateInfo);
        // Offscreen pipeline layout
        pipelineLayouts.scene = device.createPipelineLayout(pipelineLayoutCreateInfo);
    }

    void setupDescriptorSet() {
        const auto allocInfo = vk::DescriptorSetAllocateInfo{ descriptorPool, 1, &descriptorSetLayout };

        // Full screen blur descriptor sets
        // Vertical blur
        descriptorSets.verticalBlur = device.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo texDescriptorVert{
            offscreen.framebuffers[0].colors[0].sampler, offscreen.framebuffers[0].colors[0].view, vk::ImageLayout::eShaderReadOnlyOptimal
        };

        device.updateDescriptorSets({
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSets.verticalBlur, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.vsScene.descriptor },
            // Binding 1 : Fragment shader texture sampler
            vk::WriteDescriptorSet{ descriptorSets.verticalBlur, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorVert },
            // Binding 2 : Fragment shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSets.verticalBlur, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.fsVertBlur.descriptor },
            }, {});

        // Horizontal blur
        descriptorSets.horizontalBlur = device.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo texDescriptorHorz{
            offscreen.framebuffers[1].colors[0].sampler, offscreen.framebuffers[1].colors[0].view, vk::ImageLayout::eGeneral
        };

        device.updateDescriptorSets({
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSets.horizontalBlur, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.vsScene.descriptor },
            // Binding 1 : Fragment shader texture sampler
            vk::WriteDescriptorSet{ descriptorSets.horizontalBlur, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorVert },
            // Binding 2 : Fragment shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSets.horizontalBlur, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.fsHorzBlur.descriptor },
            }, {});

        // 3D scene
        descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

        device.updateDescriptorSets({
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSets.scene, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.vsFullScreen.descriptor },
            }, {});

        // Skybox
        descriptorSets.skyBox = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the cube map texture
        vk::DescriptorImageInfo cubeMapDescriptor{
            textures.cubemap.sampler, textures.cubemap.view, vk::ImageLayout::eGeneral 
        };

        device.updateDescriptorSets({
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSets.skyBox, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformData.vsSkyBox.descriptor },
            // Binding 1 : Fragment shader texture sampler
            vk::WriteDescriptorSet{ descriptorSets.skyBox, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &cubeMapDescriptor },
            }, {});
    }

    void preparePipelines() {
        vks::pipelines::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.appendVertexLayout({ {
            vks::model::VERTEX_COMPONENT_POSITION,
            vks::model::VERTEX_COMPONENT_COLOR,
            vks::model::VERTEX_COMPONENT_UV,
            vks::model::VERTEX_COMPONENT_NORMAL,
        } });

        {
            vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayouts.radialBlur, renderPass };
            pipelineBuilder.rasterizationState.frontFace = vk::FrontFace::eClockwise;
            pipelineBuilder.colorBlendState.blendAttachmentStates.resize(1);
            auto& blendAttachmentState = pipelineBuilder.colorBlendState.blendAttachmentStates[0];
            // Additive blending
            blendAttachmentState.blendEnable = VK_TRUE;
            blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
            blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
            blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
            blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
            blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
            blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;
            pipelineBuilder.vertexInputState = vertexInputState;
            pipelineBuilder.loadShader(getAssetPath() + "shaders/bloom/gaussblur.vert.spv", vk::ShaderStageFlagBits::eVertex);
            pipelineBuilder.loadShader(getAssetPath() + "shaders/bloom/gaussblur.frag.spv", vk::ShaderStageFlagBits::eFragment);
            pipelines.blur = pipelineBuilder.create(context.pipelineCache);
        }

        // Vertical gauss blur
        {
            vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayouts.scene, renderPass };
            pipelineBuilder.rasterizationState.frontFace = vk::FrontFace::eClockwise;
            pipelineBuilder.vertexInputState = vertexInputState;
            pipelineBuilder.loadShader(getAssetPath() + "shaders/bloom/phongpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
            pipelineBuilder.loadShader(getAssetPath() + "shaders/bloom/phongpass.frag.spv", vk::ShaderStageFlagBits::eFragment);
            pipelines.phongPass = pipelineBuilder.create(context.pipelineCache);
        }

        // Color only pass (offscreen blur base)
        {
            vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayouts.scene, renderPass };
            pipelineBuilder.rasterizationState.frontFace = vk::FrontFace::eClockwise;
            pipelineBuilder.vertexInputState = vertexInputState;
            pipelineBuilder.loadShader(getAssetPath() + "shaders/bloom/colorpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
            pipelineBuilder.loadShader(getAssetPath() + "shaders/bloom/colorpass.frag.spv", vk::ShaderStageFlagBits::eFragment);
            pipelines.colorPass = pipelineBuilder.create(context.pipelineCache);
        }

        // Skybox (cubemap
        {
            vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayouts.scene, renderPass };
            pipelineBuilder.vertexInputState = vertexInputState;
            pipelineBuilder.depthStencilState = { false };
            pipelineBuilder.loadShader(getAssetPath() + "shaders/bloom/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex);
            pipelineBuilder.loadShader(getAssetPath() + "shaders/bloom/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment);
            pipelines.skyBox = pipelineBuilder.create(context.pipelineCache);

        }
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Phong and color pass vertex shader uniform buffer
        uniformData.vsScene = context.createUniformBuffer(ubos.scene);
        // Fullscreen quad display vertex shader uniform buffer
        uniformData.vsFullScreen = context.createUniformBuffer(ubos.fullscreen);
        // Fullscreen quad fragment shader uniform buffers
        // Vertical blur
        uniformData.fsVertBlur = context.createUniformBuffer(ubos.vertBlur);
        // Horizontal blur
        uniformData.fsHorzBlur = context.createUniformBuffer(ubos.horzBlur);
        // Skybox
        uniformData.vsSkyBox = context.createUniformBuffer(ubos.skyBox);

        // Intialize uniform buffers
        updateUniformBuffersScene();
        updateUniformBuffersScreen();
    }

    // Update uniform buffers for rendering the 3D scene
    void updateUniformBuffersScene() {
        // UFO
        ubos.fullscreen.projection = camera.matrices.perspective;
        ubos.fullscreen.model = camera.matrices.view * glm::translate(glm::mat4(), glm::vec3(sin(glm::radians(timer * 360.0f)) * 0.25f, 0.0f, cos(glm::radians(timer * 360.0f)) * 0.25f));
        auto rotation = glm::angleAxis(-sinf(glm::radians(timer * 360.0f)) * 0.15f, glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::angleAxis(glm::radians(timer * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubos.fullscreen.model = ubos.fullscreen.model * glm::mat4_cast(rotation);
        uniformData.vsFullScreen.copy(ubos.fullscreen);

        // Skybox
        ubos.skyBox.projection = camera.matrices.perspective;
        ubos.skyBox.model = camera.matrices.skyboxView;
        uniformData.vsSkyBox.copy(ubos.skyBox);
    }

    // Update uniform buffers for the fullscreen quad
    void updateUniformBuffersScreen() {
        // Vertex shader
        ubos.scene.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
        ubos.scene.model = glm::mat4();

        uniformData.vsScene.copy(ubos.scene);

        // Fragment shader
        // Vertical
        ubos.vertBlur.horizontal = 0;
        uniformData.fsVertBlur.copy(ubos.vertBlur);

        // Horizontal
        ubos.horzBlur.horizontal = 1;
        uniformData.fsHorzBlur.copy(ubos.horzBlur);
    }

    void loadMeshes() {
        meshes.ufo.loadFromFile(context, getAssetPath() + "models/retroufo.dae", vertexLayout, 0.05f);
        meshes.ufoGlow.loadFromFile(context, getAssetPath() + "models/retroufo_glow.dae", vertexLayout, 0.05f);
        meshes.skyBox.loadFromFile(context, getAssetPath() + "models/cube.obj", vertexLayout, 1.0f);
    }

    void loadTextures() {
        textures.cubemap.loadFromFile(context,  getAssetPath() + "textures/cubemap_space.ktx", vk::Format::eR8G8B8A8Unorm);
    }

    void draw() override {
        prepareFrame();

        // Offscreen rendering
        if (bloom) {
            context.submit(offscreen.cmdBuffer, { { semaphores.acquireComplete, vk::PipelineStageFlagBits::eBottomOfPipe } }, offscreen.renderComplete);
            renderWaitSemaphores = { offscreen.renderComplete };
        } else {
            renderWaitSemaphores = { semaphores.acquireComplete };
        }

        // Scene rendering
        drawCurrentCommandBuffer();
        submitFrame();
    }

    void prepare() override {
        offscreen.framebuffers.resize(2);
        offscreen.size = glm::uvec2(TEX_DIM);
        Parent::prepare();
        generateQuad();
        loadMeshes();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        buildOffscreenCommandBuffer();
        prepared = true;
    }

    void update(float deltaTime) override {
        Parent::update(deltaTime);
        if (!paused) {
            updateUniformBuffersScene();
        }
    }

    void viewChanged() override {
        updateUniformBuffersScene();
        updateUniformBuffersScreen();
    }

    void keyPressed(uint32_t keyCode) override {
        switch (keyCode) {
        case KEY_KPADD:
        case GAMEPAD_BUTTON_R1:
            changeBlurScale(0.25f);
            break;
        case KEY_KPSUB:
        case GAMEPAD_BUTTON_L1:
            changeBlurScale(-0.25f);
            break;
        case KEY_B:
        case GAMEPAD_BUTTON_A:
            toggleBloom();
            break;
        }
    }

#if 0
    void getOverlayText(vkx::TextOverlay* textOverlay) override {
#if defined(__ANDROID__)
        textOverlay->addText("Press \"L1/R1\" to change blur scale", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
        textOverlay->addText("Press \"Button A\" to toggle bloom", 5.0f, 105.0f, vkx::TextOverlay::alignLeft);
#else
        textOverlay->addText("Press \"NUMPAD +/-\" to change blur scale", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
        textOverlay->addText("Press \"B\" to toggle bloom", 5.0f, 105.0f, vkx::TextOverlay::alignLeft);
#endif
    }
#endif

    void changeBlurScale(float delta) {
        ubos.vertBlur.blurScale += delta;
        ubos.horzBlur.blurScale += delta;
        updateUniformBuffersScreen();
    }

    void toggleBloom() {
        bloom = !bloom;
        buildCommandBuffers();
        if (bloom) {
            buildOffscreenCommandBuffer();
        }
    }
};

RUN_EXAMPLE(VulkanExample)
