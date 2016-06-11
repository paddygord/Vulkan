/*
* Vulkan Example - Multi pass offscreen rendering (bloom)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"

// Texture properties
#define TEX_DIM 256
#define TEX_FORMAT  vk::Format::eR8G8B8A8Unorm
#define TEX_FILTER vk::Filter::eLinear;

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM
#define FB_COLOR_FORMAT TEX_FORMAT

// Vertex layout for this example
vkx::MeshLayout vertexLayout =
{
    vkx::VertexLayout::VERTEX_LAYOUT_POSITION,
    vkx::VertexLayout::VERTEX_LAYOUT_UV,
    vkx::VertexLayout::VERTEX_LAYOUT_COLOR,
    vkx::VertexLayout::VERTEX_LAYOUT_NORMAL
};

class VulkanExample : public vkx::ExampleBase {
public:
    bool bloom = true;

    struct {
        vkx::Texture cubemap;
    } textures;

    struct {
        vkx::MeshBuffer ufo;
        vkx::MeshBuffer ufoGlow;
        vkx::MeshBuffer skyBox;
        vkx::MeshBuffer quad;
    } meshes;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::UniformData vsScene;
        vkx::UniformData vsFullScreen;
        vkx::UniformData vsSkyBox;
        vkx::UniformData fsVertBlur;
        vkx::UniformData fsHorzBlur;
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

    // Framebuffers for offscreen rendering
    vkx::Framebuffer offScreenFrameBufA, offScreenFrameBufB;
    vk::Semaphore offscreenSemaphore;
    vk::CommandBuffer offScreenCmdBuffer;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        zoom = -10.25f;
        rotation = { 7.5f, -343.0f, 0.0f };
        timerSpeed *= 0.5f;
        enableTextOverlay = true;
        title = "Vulkan Example - Bloom";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        offScreenFrameBufA.destroy();
        offScreenFrameBufB.destroy();

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

        device.freeCommandBuffers(cmdPool, offScreenCmdBuffer);

        textures.cubemap.destroy();
    }

    // Prepare a new framebuffer for offscreen rendering
    // The contents of this framebuffer are then
    // blitted to our render target
    void prepareOffscreenFramebuffer(vkx::Framebuffer &frameBuf, vk::CommandBuffer cmdBuffer) {
        frameBuf.size = glm::uvec2(FB_DIM);
        frameBuf.colorFormat = FB_COLOR_FORMAT;
        // Find a suitable depth format
        frameBuf.depthFormat = vkx::getSupportedDepthFormat(physicalDevice);
        frameBuf.create(*this, renderPass);

        // Create sampler
        vk::SamplerCreateInfo sampler;
        sampler.magFilter = TEX_FILTER;
        sampler.minFilter = TEX_FILTER;
        sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        frameBuf.color.sampler = device.createSampler(sampler);
    }

    // Prepare the offscreen framebuffers used for the vertical- and horizontal blur 
    void prepareOffscreenFramebuffers() {
        withPrimaryCommandBuffer([&](vk::CommandBuffer& cmdBuffer) {
            prepareOffscreenFramebuffer(offScreenFrameBufA, cmdBuffer);
            prepareOffscreenFramebuffer(offScreenFrameBufB, cmdBuffer);
        });
    }

    void createOffscreenCommandBuffer() {
        offScreenCmdBuffer = createCommandBuffer();
    }

    // Render the 3D scene into a texture target
    void buildOffscreenCommandBuffer() {

        vk::Viewport viewport = vkx::viewport(offScreenFrameBufA.size);
        vk::Rect2D scissor = vkx::rect2D(offScreenFrameBufA.size);
        vk::DeviceSize offset = 0;

        // Horizontal blur
        vk::ClearValue clearValues[2];
        clearValues[0].color = vkx::clearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        clearValues[1].depthStencil = { 1.0f, 0 };


        vk::CommandBufferBeginInfo cmdBufInfo;
        offScreenCmdBuffer.begin(cmdBufInfo);

        // Draw the unblurred geometry to framebuffer 1
        offScreenCmdBuffer.setViewport(0, viewport);
        offScreenCmdBuffer.setScissor(0, scissor);

        vkx::setImageLayout(
            offScreenCmdBuffer,
            offScreenFrameBufA.color.image,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal);

        // Draw the bloom geometry.
        {
            vk::RenderPassBeginInfo renderPassBeginInfo;
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = offScreenFrameBufA.frameBuffer;
            renderPassBeginInfo.renderArea.extent.width = offScreenFrameBufA.size.x;
            renderPassBeginInfo.renderArea.extent.height = offScreenFrameBufA.size.y;
            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;
            offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);
            offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.phongPass);
            offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.ufoGlow.vertices.buffer, offset);
            offScreenCmdBuffer.bindIndexBuffer(meshes.ufoGlow.indices.buffer, 0, vk::IndexType::eUint32);
            offScreenCmdBuffer.drawIndexed(meshes.ufoGlow.indexCount, 1, 0, 0, 0);
            offScreenCmdBuffer.endRenderPass();
        }

        // Switch framebuffer 1 into read mode and framebuffer 2 into write mode
        vkx::setImageLayout(
            offScreenCmdBuffer,
            offScreenFrameBufA.color.image,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal);

        vkx::setImageLayout(
            offScreenCmdBuffer,
            offScreenFrameBufB.color.image,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal);

        {
            vk::RenderPassBeginInfo renderPassBeginInfo;
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = offScreenFrameBufB.frameBuffer;
            renderPassBeginInfo.renderArea.extent.width = offScreenFrameBufB.size.x;
            renderPassBeginInfo.renderArea.extent.height = offScreenFrameBufB.size.y;
            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;
            // Draw a vertical blur pass from framebuffer 1's texture into framebuffer 2
            offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.radialBlur, 0, descriptorSets.verticalBlur, nullptr);
            offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.blur);
            offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, offset);
            offScreenCmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
            offScreenCmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
            offScreenCmdBuffer.endRenderPass();
        }

        offScreenCmdBuffer.end();
    }

    void loadTextures() {
        textures.cubemap = textureLoader->loadCubemap(
            getAssetPath() + "textures/cubemap_space.ktx",
            vk::Format::eR8G8B8A8Unorm);
    }

    void reBuildCommandBuffers() {
        if (!checkCommandBuffers()) {
            destroyCommandBuffers();
            createCommandBuffers();
        }
        buildCommandBuffers();
    }

    void buildCommandBuffers() override {
        vk::CommandBufferBeginInfo cmdBufInfo;
        vk::ClearValue clearValues[2];
        clearValues[0].color = defaultClearColor;
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vk::Viewport viewport = vkx::viewport(width, height);
        vk::Rect2D scissor = vkx::rect2D(width, height);
        vk::DeviceSize offset = 0;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            drawCmdBuffers[i].begin(cmdBufInfo);

            drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            drawCmdBuffers[i].setViewport(0, viewport);
            drawCmdBuffers[i].setScissor(0, scissor);

            // Skybox 
            drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.skyBox, nullptr);
            drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skyBox);
            drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.skyBox.vertices.buffer, offset);
            drawCmdBuffers[i].bindIndexBuffer(meshes.skyBox.indices.buffer, 0, vk::IndexType::eUint32);
            drawCmdBuffers[i].drawIndexed(meshes.skyBox.indexCount, 1, 0, 0, 0);

            // 3D scene
            drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);
            drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.phongPass);
            drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.ufo.vertices.buffer, offset);
            drawCmdBuffers[i].bindIndexBuffer(meshes.ufo.indices.buffer, 0, vk::IndexType::eUint32);
            drawCmdBuffers[i].drawIndexed(meshes.ufo.indexCount, 1, 0, 0, 0);

            // Render vertical blurred scene applying a horizontal blur
            if (bloom) {
                vkx::setImageLayout(
                    drawCmdBuffers[i],
                    offScreenFrameBufB.color.image,
                    vk::ImageAspectFlagBits::eColor,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal);

                drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.radialBlur, 0, descriptorSets.horizontalBlur, nullptr);
                drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.blur);
                drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, offset);
                drawCmdBuffers[i].bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
                drawCmdBuffers[i].drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
            }

            drawCmdBuffers[i].endRenderPass();

            drawCmdBuffers[i].end();
        }

        if (bloom) {
            buildOffscreenCommandBuffer();
        }
    }

    void loadMeshes() {
        meshes.ufo = loadMesh(getAssetPath() + "models/retroufo.dae", vertexLayout, 0.05f);
        meshes.ufoGlow = loadMesh(getAssetPath() + "models/retroufo_glow.dae", vertexLayout, 0.05f);
        meshes.skyBox = loadMesh(getAssetPath() + "models/cube.obj", vertexLayout, 1.0f);
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
        meshes.quad.vertices = createBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
        meshes.quad.indexCount = indexBuffer.size();
        meshes.quad.indices = createBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
    }

    void setupVertexDescriptions() {
        // Binding description
        // Same for all meshes used in this example
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vkx::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vkx::vertexSize(vertexLayout), vk::VertexInputRate::eVertex);

        // Attribute descriptions
        vertices.attributeDescriptions.resize(4);
        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);
        // Location 1 : Texture coordinates
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32Sfloat, sizeof(float) * 3);
        // Location 2 : Color
        vertices.attributeDescriptions[2] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32Sfloat, sizeof(float) * 5);
        // Location 3 : Normal
        vertices.attributeDescriptions[3] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32B32Sfloat, sizeof(float) * 8);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 8),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 6)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 5);

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        // Textured quad pipeline layout

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
                1),
            // Binding 2 : Framgnet shader image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eFragment,
                2),
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

        pipelineLayouts.radialBlur = device.createPipelineLayout(pPipelineLayoutCreateInfo);

        // Offscreen pipeline layout
        pipelineLayouts.scene = device.createPipelineLayout(pPipelineLayoutCreateInfo);
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        // Full screen blur descriptor sets
        // Vertical blur
        descriptorSets.verticalBlur = device.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo texDescriptorVert =
            vkx::descriptorImageInfo(offScreenFrameBufA.color.sampler, offScreenFrameBufA.color.view, vk::ImageLayout::eShaderReadOnlyOptimal);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.verticalBlur,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsScene.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.verticalBlur,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptorVert),
            // Binding 2 : Fragment shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.verticalBlur,
                vk::DescriptorType::eUniformBuffer,
                2,
                &uniformData.fsVertBlur.descriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);

        // Horizontal blur
        descriptorSets.horizontalBlur = device.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo texDescriptorHorz =
            vkx::descriptorImageInfo(offScreenFrameBufB.color.sampler, offScreenFrameBufB.color.view, vk::ImageLayout::eGeneral);

        writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.horizontalBlur,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsScene.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.horizontalBlur,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptorHorz),
            // Binding 2 : Fragment shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.horizontalBlur,
                vk::DescriptorType::eUniformBuffer,
                2,
                &uniformData.fsHorzBlur.descriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);

        // 3D scene
        descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

        writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.scene,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsFullScreen.descriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);

        // Skybox
        descriptorSets.skyBox = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the cube map texture
        vk::DescriptorImageInfo cubeMapDescriptor =
            vkx::descriptorImageInfo(textures.cubemap.sampler, textures.cubemap.view, vk::ImageLayout::eGeneral);

        writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.skyBox,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsSkyBox.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.skyBox,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &cubeMapDescriptor),
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
            vkx::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);

        vk::PipelineViewportStateCreateInfo viewportState =
            vkx::pipelineViewportStateCreateInfo(1, 1);

        vk::PipelineMultisampleStateCreateInfo multisampleState;

        std::vector<vk::DynamicState> dynamicStateEnables = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        // Vertical gauss blur
        // Load shaders
        shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/gaussblur.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/gaussblur.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
            vkx::pipelineCreateInfo(pipelineLayouts.radialBlur, renderPass);

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

        // Additive blending
        blendAttachmentState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;

        pipelines.blur = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Phong pass (3D model)
        shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/phongpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/phongpass.frag.spv", vk::ShaderStageFlagBits::eFragment);

        pipelineCreateInfo.layout = pipelineLayouts.scene;
        blendAttachmentState.blendEnable = VK_FALSE;
        depthStencilState.depthWriteEnable = VK_TRUE;

        pipelines.phongPass = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Color only pass (offscreen blur base)
        shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/colorpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/colorpass.frag.spv", vk::ShaderStageFlagBits::eFragment);

        pipelines.colorPass = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Skybox (cubemap
        shaderStages[0] = loadShader(getAssetPath() + "shaders/bloom/skybox.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/bloom/skybox.frag.spv", vk::ShaderStageFlagBits::eFragment);
        depthStencilState.depthWriteEnable = VK_FALSE;
        pipelines.skyBox = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Phong and color pass vertex shader uniform buffer
        uniformData.vsScene = createUniformBuffer(ubos.scene);
        uniformData.vsScene.map();

        // Fullscreen quad display vertex shader uniform buffer
        uniformData.vsFullScreen = createUniformBuffer(ubos.fullscreen);
        uniformData.vsFullScreen.map();

        // Fullscreen quad fragment shader uniform buffers
        // Vertical blur
        uniformData.fsVertBlur = createUniformBuffer(ubos.vertBlur);
        uniformData.fsVertBlur.map();

        // Horizontal blur
        uniformData.fsHorzBlur = createUniformBuffer(ubos.horzBlur);
        uniformData.fsHorzBlur.map();

        // Skybox
        uniformData.vsSkyBox = createUniformBuffer(ubos.skyBox);
        uniformData.vsSkyBox.map();

        // Intialize uniform buffers
        updateUniformBuffersScene();
        updateUniformBuffersScreen();
    }

    // Update uniform buffers for rendering the 3D scene
    void updateUniformBuffersScene() {
        // UFO
        ubos.fullscreen.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
        glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, -1.0f, zoom));
        ubos.fullscreen.model = viewMatrix *
            glm::translate(glm::mat4(), glm::vec3(sin(glm::radians(timer * 360.0f)) * 0.25f, 0.0f, cos(glm::radians(timer * 360.0f)) * 0.25f) + cameraPos);

        ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, -sinf(glm::radians(timer * 360.0f)) * 0.15f, glm::vec3(1.0f, 0.0f, 0.0f));
        ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(timer * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubos.fullscreen.model = glm::rotate(ubos.fullscreen.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        uniformData.vsFullScreen.copy(ubos.fullscreen);

        // Skybox
        ubos.skyBox.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
        ubos.skyBox.model = glm::mat4();
        ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        ubos.skyBox.model = glm::rotate(ubos.skyBox.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
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

    void draw() override {
        prepareFrame();

        // Offscreen rendering
        if (bloom) {
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &offScreenCmdBuffer;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &semaphores.presentComplete;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &offscreenSemaphore;
            queue.submit(submitInfo, VK_NULL_HANDLE);
        }

        // Scene rendering
        {
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = bloom ? &offscreenSemaphore : &semaphores.presentComplete;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &semaphores.renderComplete;
            queue.submit(submitInfo, VK_NULL_HANDLE);
        }

        submitFrame();
    }

    void prepare() {
        ExampleBase::prepare();
        offscreenSemaphore = device.createSemaphore(vk::SemaphoreCreateInfo());

        loadTextures();
        generateQuad();
        loadMeshes();
        setupVertexDescriptions();
        prepareUniformBuffers();
        prepareOffscreenFramebuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        createOffscreenCommandBuffer();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        if (!paused) {
            updateUniformBuffersScene();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffersScene();
        updateUniformBuffersScreen();
    }

    virtual void keyPressed(uint32_t keyCode) {
        switch (keyCode) {
        case GLFW_KEY_KP_ADD:
        case GAMEPAD_BUTTON_R1:
            changeBlurScale(0.25f);
            break;
        case GLFW_KEY_KP_SUBTRACT:
        case GAMEPAD_BUTTON_L1:
            changeBlurScale(-0.25f);
            break;
        case GLFW_KEY_B:
        case GAMEPAD_BUTTON_A:
            toggleBloom();
            break;
        }
    }

    virtual void getOverlayText(vkx::TextOverlay *textOverlay) {
#if defined(__ANDROID__)
        textOverlay->addText("Press \"L1/R1\" to change blur scale", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
        textOverlay->addText("Press \"Button A\" to toggle bloom", 5.0f, 105.0f, vkx::TextOverlay::alignLeft);
#else
        textOverlay->addText("Press \"NUMPAD +/-\" to change blur scale", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
        textOverlay->addText("Press \"B\" to toggle bloom", 5.0f, 105.0f, vkx::TextOverlay::alignLeft);
#endif
    }

    void changeBlurScale(float delta) {
        ubos.vertBlur.blurScale += delta;
        ubos.horzBlur.blurScale += delta;
        updateUniformBuffersScreen();
    }

    void toggleBloom() {
        bloom = !bloom;
        reBuildCommandBuffers();
    }
};

RUN_EXAMPLE(VulkanExample)
