/*
* Vulkan Example - Offscreen rendering using a separate framebuffer
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


// Texture properties
#define TEX_DIM 512
#define TEX_FORMAT  vk::Format::eR8G8B8A8Unorm
#define TEX_FILTER vk::Filter::eLinear

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
    struct {
        vkx::Texture colorMap;
    } textures;

    struct {
        vkx::MeshBuffer example;
        vkx::MeshBuffer plane;
    } meshes;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::UniformData vsShared;
        vkx::UniformData vsMirror;
        vkx::UniformData vsOffScreen;
    } uniformData;

    struct UBO {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    };

    struct {
        UBO vsShared;
    } ubos;

    struct {
        vk::Pipeline shaded;
        vk::Pipeline mirror;
    } pipelines;

    struct {
        vk::PipelineLayout quad;
        vk::PipelineLayout offscreen;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet mirror;
        vk::DescriptorSet model;
        vk::DescriptorSet offscreen;
    } descriptorSets;

    vk::DescriptorSetLayout descriptorSetLayout;

    glm::vec3 meshPos = glm::vec3(0.0f, -1.5f, 0.0f);

    vk::RenderPass offscreenRenderPass;
    vk::CommandBuffer offScreenCmdBuffer;
    vk::Semaphore offscreenRenderComplete;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        zoom = -6.5f;
        rotation = { -11.25f, 45.0f, 0.0f };
        timerSpeed *= 0.25f;
        title = "Vulkan Example - Offscreen rendering";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        // Textures
        //textureTarget.destroy();
        textures.colorMap.destroy();

#if OFFSCREEN
        // Frame buffer
        offscreenFramebuffer.destroy();
        device.freeCommandBuffers(cmdPool, offScreenCmdBuffer);
        device.destroyPipeline(pipelines.mirror);
        device.destroyPipelineLayout(pipelineLayouts.offscreen);
#endif


        device.destroyPipeline(pipelines.shaded);
        device.destroyPipelineLayout(pipelineLayouts.quad);

        device.destroyDescriptorSetLayout(descriptorSetLayout);

        // Meshes
        meshes.example.destroy();
        meshes.plane.destroy();

        // Uniform buffers
        uniformData.vsShared.destroy();
        uniformData.vsMirror.destroy();
        uniformData.vsOffScreen.destroy();
    }

    vk::Sampler sampler;
    vkx::Framebuffer offscreenFramebuffer;
    // Preapre an empty texture as the blit target from 
    // the offscreen framebuffer
    void prepareOffscreenSampler(uint32_t width, uint32_t height, vk::Format format) {
        // Get device properites for the requested texture format
        vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(format);

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
        this->sampler = device.createSampler(sampler);
    }

    // Prepare a new framebuffer for offscreen rendering
    // The contents of this framebuffer are then
    // blitted to our render target
    void prepareOffscreenFramebuffer() {
        offscreenFramebuffer.size = glm::uvec2(FB_DIM);
        offscreenFramebuffer.colorFormat = FB_COLOR_FORMAT;
        offscreenFramebuffer.depthFormat = vkx::getSupportedDepthFormat(physicalDevice);

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

        vk::AttachmentReference& depthReference = attachmentReferences[0];
        depthReference.attachment = 1;
        depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference& colorReference = attachmentReferences[1];
        colorReference.attachment = 0;
        colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

        std::vector<vk::SubpassDescription> subpasses;
        std::vector<vk::SubpassDependency> subpassDependencies;

        vk::SubpassDependency dependency;
        dependency.srcSubpass = 0;
        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        subpassDependencies.push_back(dependency);

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.pDepthStencilAttachment = attachmentReferences.data();
        subpass.colorAttachmentCount = attachmentReferences.size() - 1;
        subpass.pColorAttachments = attachmentReferences.data() + 1;
        subpasses.push_back(subpass);

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
        offscreenFramebuffer.create(*this, offscreenRenderPass);
    }

    void createOffscreenCommandBuffer() {
        vk::CommandBufferAllocateInfo cmd = vkx::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
        offScreenCmdBuffer = device.allocateCommandBuffers(cmd)[0];
    }

    // The command buffer to copy for rendering 
    // the offscreen scene and blitting it into
    // the texture target is only build once
    // and gets resubmitted 
    void buildOffscreenCommandBuffer() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = vkx::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = offscreenRenderPass;
        renderPassBeginInfo.framebuffer = offscreenFramebuffer.frameBuffer;
        renderPassBeginInfo.renderArea.extent.width = offscreenFramebuffer.size.x;
        renderPassBeginInfo.renderArea.extent.height = offscreenFramebuffer.size.y;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vk::DeviceSize offsets = 0;
        offScreenCmdBuffer.begin(cmdBufInfo);
        offScreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        offScreenCmdBuffer.setViewport(0, vkx::viewport(offscreenFramebuffer.size));
        offScreenCmdBuffer.setScissor(0, vkx::rect2D(offscreenFramebuffer.size));
        offScreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.offscreen, nullptr);
        offScreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.shaded);
        offScreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buffer, offsets);
        offScreenCmdBuffer.bindIndexBuffer(meshes.example.indices.buffer, 0, vk::IndexType::eUint32);
        offScreenCmdBuffer.drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);
        offScreenCmdBuffer.endRenderPass();
        offScreenCmdBuffer.end();
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

        vk::DeviceSize offsets = 0;
        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = framebuffers[i];

            drawCmdBuffers[i].begin(cmdBufInfo);
            drawCmdBuffers[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            drawCmdBuffers[i].setViewport(0, vkx::viewport(glm::uvec2(width, height)));
            drawCmdBuffers[i].setScissor(0, vkx::rect2D(glm::uvec2(width, height)));

            // Reflection plane
            drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.mirror, nullptr);
            drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.mirror);
            drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.plane.vertices.buffer, offsets);
            drawCmdBuffers[i].bindIndexBuffer(meshes.plane.indices.buffer, 0, vk::IndexType::eUint32);
            drawCmdBuffers[i].drawIndexed(meshes.plane.indexCount, 1, 0, 0, 0);

            // Model
            drawCmdBuffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.model, nullptr);
            drawCmdBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.shaded);
            drawCmdBuffers[i].bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buffer, offsets);
            drawCmdBuffers[i].bindIndexBuffer(meshes.example.indices.buffer, 0, vk::IndexType::eUint32);
            drawCmdBuffers[i].drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);
            drawCmdBuffers[i].endRenderPass();
            drawCmdBuffers[i].end();
        }
    }

    void draw() override {
        prepareFrame();

        vk::PipelineStageFlags stageFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submitInfo.commandBufferCount = 1;
        submitInfo.pWaitDstStageMask = &stageFlags;
        submitInfo.pSignalSemaphores = &offscreenRenderComplete;
        submitInfo.pCommandBuffers = &offScreenCmdBuffer;
        queue.submit(submitInfo, VK_NULL_HANDLE);

        submitInfo.pWaitSemaphores = &offscreenRenderComplete;
        submitInfo.pSignalSemaphores = &semaphores.renderComplete;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        queue.submit(submitInfo, VK_NULL_HANDLE);

        submitInfo.pWaitDstStageMask = &submitPipelineStages;
        submitInfo.pWaitSemaphores = &semaphores.presentComplete;
        submitInfo.pSignalSemaphores = &semaphores.renderComplete;

        submitFrame();
    }

    void loadMeshes() {
        meshes.plane = loadMesh(getAssetPath() + "models/plane.obj", vertexLayout, 0.4f);
        meshes.example = loadMesh(getAssetPath() + "models/chinesedragon.dae", vertexLayout, 0.3f);
    }

    void loadTextures() {
        textures.colorMap = textureLoader->loadTexture(
            getAssetPath() + "textures/darkmetal_bc3.ktx",
            vk::Format::eBc3UnormBlock);
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
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 6),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 8)
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
            // Binding 2 : Fragment shader image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                2)
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

        pipelineLayouts.quad = device.createPipelineLayout(pPipelineLayoutCreateInfo);


        // Offscreen pipeline layout
        pipelineLayouts.offscreen = device.createPipelineLayout(pPipelineLayoutCreateInfo);
    }

    void setupDescriptorSet() {
        // Mirror plane descriptor set
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSets.mirror = device.allocateDescriptorSets(allocInfo)[0];
        
        // vk::Image descriptor for the offscreen mirror texture
        vk::DescriptorImageInfo texDescriptorMirror =
            vkx::descriptorImageInfo(sampler, offscreenFramebuffer.color.view, vk::ImageLayout::eShaderReadOnlyOptimal);
        // vk::Image descriptor for the color map
        vk::DescriptorImageInfo texDescriptorColorMap =
            vkx::descriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.mirror,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsMirror.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.mirror,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptorMirror),
            // Binding 2 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.mirror,
                vk::DescriptorType::eCombinedImageSampler,
                2,
                &texDescriptorColorMap)
        };
        device.updateDescriptorSets(writeDescriptorSets, {});

        // Model
        // No texture
        descriptorSets.model = device.allocateDescriptorSets(allocInfo)[0];
        std::vector<vk::WriteDescriptorSet> modelWriteDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.model,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsShared.descriptor)
        };
        device.updateDescriptorSets(modelWriteDescriptorSets, {});

        // Offscreen
        descriptorSets.offscreen = device.allocateDescriptorSets(allocInfo)[0];
        std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.offscreen,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsOffScreen.descriptor)
        };
        device.updateDescriptorSets(offScreenWriteDescriptorSets, {});
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise);

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

        // Solid rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/quad.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/quad.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
            vkx::pipelineCreateInfo(pipelineLayouts.quad, renderPass);

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

        // Mirror
        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/mirror.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/mirror.frag.spv", vk::ShaderStageFlagBits::eFragment);

        pipelines.mirror = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Solid shading pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/offscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/offscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelineCreateInfo.layout = pipelineLayouts.offscreen;
        pipelines.shaded = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Mesh vertex shader uniform buffer block
        uniformData.vsShared = createUniformBuffer(ubos.vsShared);
        uniformData.vsShared.map();

        // Mirror plane vertex shader uniform buffer block
        uniformData.vsMirror = createUniformBuffer(ubos.vsShared);
        uniformData.vsMirror.map();

        // Offscreen vertex shader uniform buffer block
        uniformData.vsOffScreen = createUniformBuffer(ubos.vsShared);
        uniformData.vsOffScreen.map();

        updateUniformBuffers();
        updateUniformBufferOffscreen();
    }

    void updateUniformBuffers() {
        // Mesh
        ubos.vsShared.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
        glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));

        ubos.vsShared.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(0, 0, 0));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        ubos.vsShared.model = glm::translate(ubos.vsShared.model, meshPos);
        uniformData.vsShared.copy(ubos.vsShared);

        // Mirror
        ubos.vsShared.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(0, 0, 0));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        uniformData.vsMirror.copy(ubos.vsShared);
    }

    void updateUniformBufferOffscreen() {
        ubos.vsShared.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
        glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));

        ubos.vsShared.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(0, 0, 0));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        ubos.vsShared.model = glm::rotate(ubos.vsShared.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        ubos.vsShared.model = glm::scale(ubos.vsShared.model, glm::vec3(1.0f, -1.0f, 1.0f));
        ubos.vsShared.model = glm::translate(ubos.vsShared.model, meshPos);
        uniformData.vsOffScreen.copy(ubos.vsShared);
    }

    void prepare() {
        ExampleBase::prepare();
        loadTextures();
        loadMeshes();
        setupVertexDescriptions();
        prepareUniformBuffers();
        setupDescriptorSetLayout();

        offscreenRenderComplete = device.createSemaphore(vk::SemaphoreCreateInfo());
        prepareOffscreenSampler(TEX_DIM, TEX_DIM, TEX_FORMAT);
        prepareOffscreenFramebuffer();
        createOffscreenCommandBuffer();

        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildOffscreenCommandBuffer();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        vkDeviceWaitIdle(device);
        draw();
        vkDeviceWaitIdle(device);
        if (!paused) {
            updateUniformBuffers();
            updateUniformBufferOffscreen();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffers();
        updateUniformBufferOffscreen();
    }
};

RUN_EXAMPLE(VulkanExample)
