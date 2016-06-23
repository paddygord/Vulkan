/*
* Vulkan Example - Fullscreen radial blur (Single pass offscreen effect)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


// Texture properties
#define TEX_DIM 128
#define TEX_FORMAT  vk::Format::eR8G8B8A8Unorm
#define TEX_FILTER vk::Filter::eLinear;

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM
#define FB_COLOR_FORMAT  vk::Format::eR8G8B8A8Unorm

// Vertex layout for this example
std::vector<vkx::VertexLayout> vertexLayout =
{
    vkx::VertexLayout::VERTEX_LAYOUT_POSITION,
    vkx::VertexLayout::VERTEX_LAYOUT_UV,
    vkx::VertexLayout::VERTEX_LAYOUT_COLOR,
    vkx::VertexLayout::VERTEX_LAYOUT_NORMAL
};

class VulkanExample : public vkx::ExampleBase {
public:
    bool blur = true;
    bool displayTexture = false;

    struct {
        vkx::MeshBuffer example;
        vkx::MeshBuffer quad;
    } meshes;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::UniformData vsScene;
        vkx::UniformData vsQuad;
        vkx::UniformData fsQuad;
    } uniformData;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVS;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboQuadVS;

    struct UboQuadFS {
        int32_t texWidth = TEX_DIM;
        int32_t texHeight = TEX_DIM;
        float radialBlurScale = 0.25f;
        float radialBlurStrength = 0.75f;
        glm::vec2 radialOrigin = glm::vec2(0.5f, 0.5f);
    } uboQuadFS;

    struct {
        vk::Pipeline radialBlur;
        vk::Pipeline colorPass;
        vk::Pipeline phongPass;
        vk::Pipeline fullScreenOnly;
    } pipelines;

    struct {
        vk::PipelineLayout radialBlur;
        vk::PipelineLayout scene;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet scene;
        vk::DescriptorSet quad;
    } descriptorSets;

    // Descriptor set layout is shared amongst
    // all descriptor sets
    vk::DescriptorSetLayout descriptorSetLayout;

    // vk::Framebuffer for offscreen rendering
    vkx::Framebuffer offscreenFramebuffer;
    vk::CommandBuffer offscreenCmdBuffer;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        zoom = -12.0f;
        rotation = { -16.25f, -28.75f, 0.0f };
        timerSpeed *= 0.5f;
        enableTextOverlay = true;
        title = "Vulkan Example - Radial blur";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        // Frame buffer
        offscreenFramebuffer.destroy();

        device.destroyPipeline(pipelines.radialBlur);
        device.destroyPipeline(pipelines.phongPass);
        device.destroyPipeline(pipelines.colorPass);
        device.destroyPipeline(pipelines.fullScreenOnly);

        device.destroyPipelineLayout(pipelineLayouts.radialBlur);
        device.destroyPipelineLayout(pipelineLayouts.scene);

        device.destroyDescriptorSetLayout(descriptorSetLayout);

        // Meshes
        meshes.example.destroy();
        meshes.quad.destroy();

        // Uniform buffers
        uniformData.vsScene.destroy();
        uniformData.vsQuad.destroy();
        uniformData.fsQuad.destroy();

        device.freeCommandBuffers(cmdPool, offscreenCmdBuffer);
    }

    // Preapre an empty texture as the blit target from 
    // the offscreen framebuffer
    void prepareSampler() {

        // Create sampler
        vk::SamplerCreateInfo sampler;
        sampler.magFilter = TEX_FILTER;
        sampler.minFilter = TEX_FILTER;
        sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
        sampler.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        sampler.addressModeV = sampler.addressModeU;
        sampler.addressModeW = sampler.addressModeU;
        sampler.mipLodBias = 0.0f;
        sampler.maxAnisotropy = 0;
        sampler.compareOp = vk::CompareOp::eNever;
        sampler.minLod = 0.0f;
        sampler.maxLod = 0.0f;
        sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        offscreenFramebuffer.color.sampler = device.createSampler(sampler);
    }

    // Prepare a new framebuffer for offscreen rendering
    // The contents of this framebuffer are then
    // blitted to our render target
    void prepareOffscreenFramebuffer() {
        offscreenFramebuffer.size = glm::uvec2(FB_DIM);
        offscreenFramebuffer.colorFormat = FB_COLOR_FORMAT;
        offscreenFramebuffer.depthFormat = vkx::getSupportedDepthFormat(physicalDevice);
        offscreenFramebuffer.create(*this, offscreenRenderPass);
    }

    void createOffscreenCommandBuffer() {
        vk::CommandBufferAllocateInfo cmd = vkx::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
        offscreenCmdBuffer = device.allocateCommandBuffers(cmd)[0];
    }

    vk::RenderPass offscreenRenderPass;

    void buildOffscreenRenderPass() {
        std::vector<vk::AttachmentDescription> attachments;
        std::vector<vk::AttachmentReference> attachmentReferences;
        attachments.resize(2);
        attachmentReferences.resize(2);

        // Color attachment
        attachments[0].format = colorformat;
        attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
        attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
        attachments[0].initialLayout = vk::ImageLayout::eUndefined;
        attachments[0].finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        // Depth attachment
        attachments[1].format = depthFormat;
        attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
        attachments[1].storeOp = vk::AttachmentStoreOp::eDontCare;
        attachments[1].initialLayout = vk::ImageLayout::eUndefined;
        attachments[1].finalLayout = vk::ImageLayout::eUndefined;

        std::vector<vk::SubpassDependency> subpassDependencies;
        {
            vk::SubpassDependency dependency;
            dependency.srcSubpass = 0;
            dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
            subpassDependencies.push_back(dependency);
        }


        std::vector<vk::SubpassDescription> subpasses;
        {
            vk::SubpassDescription subpass;
            vk::AttachmentReference& depthReference = attachmentReferences[0];
            depthReference.attachment = 1;
            depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            vk::AttachmentReference& colorReference = attachmentReferences[1];
            colorReference.attachment = 0;
            colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.pDepthStencilAttachment = attachmentReferences.data();
            subpass.colorAttachmentCount = attachmentReferences.size() - 1;
            subpass.pColorAttachments = attachmentReferences.data() + 1;
            subpasses.push_back(subpass);
        }

        if (offscreenRenderPass) {
            device.destroyRenderPass(offscreenRenderPass);
        }

        vk::RenderPassCreateInfo renderPassInfo;
        renderPassInfo.attachmentCount = attachments.size();
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = subpasses.size();
        renderPassInfo.pSubpasses = subpasses.data();
        renderPassInfo.dependencyCount = subpassDependencies.size();
        renderPassInfo.pDependencies = subpassDependencies.data();
        offscreenRenderPass = device.createRenderPass(renderPassInfo);
    }

    // The command buffer to copy for rendering 
    // the offscreen scene and blitting it into
    // the texture target is only build once
    // and gets resubmitted 
    void buildOffscreenCommandBuffer() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = vkx::clearColor({ 0, 0, 0, 0 });
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.framebuffer = offscreenFramebuffer.frameBuffer;
        renderPassBeginInfo.renderArea.extent.width = offscreenFramebuffer.size.x;
        renderPassBeginInfo.renderArea.extent.height = offscreenFramebuffer.size.y;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        offscreenCmdBuffer.begin(cmdBufInfo);
        offscreenCmdBuffer.setViewport(0, vkx::viewport(offscreenFramebuffer.size));
        offscreenCmdBuffer.setScissor(0, vkx::rect2D(offscreenFramebuffer.size));
        offscreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        offscreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);
        offscreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.colorPass);

        vk::DeviceSize offsets = 0;
        offscreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buffer, offsets);
        offscreenCmdBuffer.bindIndexBuffer(meshes.example.indices.buffer, 0, vk::IndexType::eUint32);
        offscreenCmdBuffer.drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);
        offscreenCmdBuffer.endRenderPass();

        offscreenCmdBuffer.end();
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) {
        vk::Viewport viewport = vkx::viewport((float)width, (float)height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);

        vk::Rect2D scissor = vkx::rect2D(width, height, 0, 0);
        cmdBuffer.setScissor(0, scissor);

        vk::DeviceSize offsets = 0;

        // 3D scene
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.scene, 0, descriptorSets.scene, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.phongPass);

        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(meshes.example.indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);

        // Fullscreen quad with radial blur
        if (blur) {
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.radialBlur, 0, descriptorSets.quad, nullptr);
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, (displayTexture) ? pipelines.fullScreenOnly : pipelines.radialBlur);
            cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, offsets);
            cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
            cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
        }
    }

    void loadMeshes() {
        meshes.example = loadMesh(getAssetPath() + "models/glowsphere.dae", vertexLayout, 0.05f);
    }

    // Setup vertices for a single uv-mapped quad
    void generateQuad() {
        struct Vertex {
            float pos[3];
            float uv[2];
            float col[3];
            float normal[3];
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

        meshes.quad.vertices = stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);

        std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
        meshes.quad.indexCount = indexBuffer.size();
        meshes.quad.indices = stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vkx::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vkx::vertexSize(vertexLayout), vk::VertexInputRate::eVertex);

        // Attribute descriptions
        vertices.attributeDescriptions.resize(4);
        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0,  vk::Format::eR32G32B32Sfloat, 0);
        // Location 1 : Texture coordinates
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1,  vk::Format::eR32G32Sfloat, sizeof(float) * 3);
        // Location 2 : Color
        vertices.attributeDescriptions[2] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2,  vk::Format::eR32G32B32Sfloat, sizeof(float) * 5);
        // Location 3 : Normal
        vertices.attributeDescriptions[3] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3,  vk::Format::eR32G32B32Sfloat, sizeof(float) * 8);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // Example uses three ubos and one image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 4),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

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
            // Binding 2 : Fragment shader uniform buffer
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eFragment,
                2)
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
        // Textured quad descriptor set
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSets.quad = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the color map texture
        vk::DescriptorImageInfo texDescriptor =
            vkx::descriptorImageInfo(offscreenFramebuffer.color.sampler, offscreenFramebuffer.color.view, vk::ImageLayout::eShaderReadOnlyOptimal);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.quad,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsScene.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.quad,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor),
            // Binding 2 : Fragment shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.quad,
                vk::DescriptorType::eUniformBuffer,
                2,
                &uniformData.fsQuad.descriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Offscreen 3D scene descriptor set
        descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.scene,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsQuad.descriptor)
        };
        device.updateDescriptorSets(offScreenWriteDescriptorSets.size(), offScreenWriteDescriptorSets.data(), 0, NULL);
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

        // Radial blur pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/radialblur/radialblur.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/radialblur/radialblur.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

        pipelines.radialBlur = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // No blending (for debug display)
        blendAttachmentState.blendEnable = VK_FALSE;
        pipelines.fullScreenOnly = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Phong pass
        shaderStages[0] = loadShader(getAssetPath() + "shaders/radialblur/phongpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/radialblur/phongpass.frag.spv", vk::ShaderStageFlagBits::eFragment);

        pipelineCreateInfo.layout = pipelineLayouts.scene;
        blendAttachmentState.blendEnable = VK_FALSE;
        depthStencilState.depthWriteEnable = VK_TRUE;

        pipelines.phongPass = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Color only pass (offscreen blur base)
        shaderStages[0] = loadShader(getAssetPath() + "shaders/radialblur/colorpass.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/radialblur/colorpass.frag.spv", vk::ShaderStageFlagBits::eFragment);

        pipelines.colorPass = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Phong and color pass vertex shader uniform buffer
        uniformData.vsScene = createUniformBuffer(uboVS);
        uniformData.vsScene.map();

        // Fullscreen quad vertex shader uniform buffer
        uniformData.vsQuad = createUniformBuffer(uboVS);
        uniformData.vsQuad.map();

        // Fullscreen quad fragment shader uniform buffer
        uniformData.fsQuad = createUniformBuffer(uboVS);
        uniformData.fsQuad.map();

        updateUniformBuffersScene();
        updateUniformBuffersScreen();
    }

    // Update uniform buffers for rendering the 3D scene
    void updateUniformBuffersScene() {
        uboQuadVS.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 1.0f, 256.0f);
        glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));

        uboQuadVS.model = glm::mat4();
        uboQuadVS.model = viewMatrix * glm::translate(uboQuadVS.model, glm::vec3(0, 0, 0));
        uboQuadVS.model = glm::rotate(uboQuadVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboQuadVS.model = glm::rotate(uboQuadVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboQuadVS.model = glm::rotate(uboQuadVS.model, glm::radians(timer * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        uboQuadVS.model = glm::rotate(uboQuadVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        uniformData.vsQuad.copy(uboQuadVS);
    }

    // Update uniform buffers for the fullscreen quad
    void updateUniformBuffersScreen() {
        // Vertex shader
        uboVS.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
        uboVS.model = glm::mat4();
        uniformData.vsScene.copy(uboVS);

        // Fragment shader
        uniformData.fsQuad.copy(uboQuadFS);
    }

    vk::Semaphore offscreenSemaphore;

    void draw() override {

        prepareFrame();
        {
            vk::SubmitInfo submitInfo;
            submitInfo.pWaitDstStageMask = this->submitInfo.pWaitDstStageMask;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &offscreenCmdBuffer;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &semaphores.presentComplete;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &offscreenSemaphore;
            queue.submit(submitInfo, VK_NULL_HANDLE);
        }
        drawCurrentCommandBuffer(offscreenSemaphore);;
        submitFrame();
    }

    void prepare() {
        ExampleBase::prepare();

        offscreenSemaphore = device.createSemaphore(vk::SemaphoreCreateInfo());
        buildOffscreenRenderPass();
        prepareOffscreenFramebuffer();
        prepareSampler();

        generateQuad();
        loadMeshes();
        setupVertexDescriptions();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        createOffscreenCommandBuffer();
        buildOffscreenCommandBuffer();
        updateDrawCommandBuffers();
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
        case GLFW_KEY_B:
        case GAMEPAD_BUTTON_A:
            toggleBlur();
            break;
        case GLFW_KEY_T:
        case GAMEPAD_BUTTON_X:
            toggleTextureDisplay();
            break;
        }
    }

    virtual void getOverlayText(vkx::TextOverlay *textOverlay) {
#if defined(__ANDROID__)
        textOverlay->addText("Press \"Button A\" to toggle blur", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
        textOverlay->addText("Press \"Button X\" to display offscreen texture", 5.0f, 105.0f, vkx::TextOverlay::alignLeft);
#else
        textOverlay->addText("Press \"B\" to toggle blur", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
        textOverlay->addText("Press \"T\" to display offscreen texture", 5.0f, 105.0f, vkx::TextOverlay::alignLeft);
#endif
    }

    void toggleBlur() {
        blur = !blur;
        updateUniformBuffersScene();
        updateDrawCommandBuffers();
    }

    void toggleTextureDisplay() {
        displayTexture = !displayTexture;
        updateDrawCommandBuffers();
    }

};

RUN_EXAMPLE(VulkanExample)
