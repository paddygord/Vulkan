/*
* Vulkan Example - Offscreen rendering using a separate framebuffer
*
*    p - Toggle light source animation
*    l - Toggle between scene and light's POV
*    s - Toggle shadowmap display
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


// 16 bits of depth is enough for such a small scene
#define DEPTH_FORMAT  vk::Format::eD16Unorm

// Texture properties
#define TEX_DIM 2048
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
    bool displayShadowMap = false;
    bool lightPOV = false;

    // Keep depth range as small as possible
    // for better shadow map precision
    float zNear = 1.0f;
    float zFar = 96.0f;

    // Constant depth bias factor (always applied)
    float depthBiasConstant = 1.25f;
    // Slope depth bias factor, applied depending on polygon's slope
    float depthBiasSlope = 1.75f;

    glm::vec3 lightPos = glm::vec3();
    float lightFOV = 45.0f;

    struct {
        vkx::MeshBuffer scene;
        vkx::MeshBuffer quad;
    } meshes;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    vkx::UniformData uniformDataVS, uniformDataOffscreenVS;

    struct {
        vkx::UniformData scene;
    } uniformData;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVSquad;

    struct {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model;
        glm::mat4 depthBiasMVP;
        glm::vec3 lightPos;
    } uboVSscene;

    struct {
        glm::mat4 depthMVP;
    } uboOffscreenVS;

    struct {
        vk::Pipeline quad;
        vk::Pipeline offscreen;
        vk::Pipeline scene;
    } pipelines;

    struct {
        vk::PipelineLayout quad;
        vk::PipelineLayout offscreen;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet offscreen;
        vk::DescriptorSet scene;
    } descriptorSets;

    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    // vk::Framebuffer for offscreen rendering
    using FrameBufferAttachment = CreateImageResult;

    struct FrameBuffer {
        int32_t width, height;
        vk::Framebuffer frameBuffer;
        FrameBufferAttachment color, depth;
        vk::RenderPass renderPass;
        vkx::Texture textureTarget;
    } offscreenFrameBuf;

    vk::CommandBuffer offscreenCmdBuffer;

    // vk::Semaphore used to synchronize offscreen rendering before using it's texture target for sampling
    vk::Semaphore offscreenSemaphore;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        zoom = -20.0f;
        rotation = { -15.0f, -390.0f, 0.0f };
        title = "Vulkan Example - Projected shadow mapping";
        timerSpeed *= 0.5f;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        // Texture target
        offscreenFrameBuf.textureTarget.destroy();

        // Frame buffer
        device.destroyFramebuffer(offscreenFrameBuf.frameBuffer);

        // Color attachment
        offscreenFrameBuf.color.destroy();

        // Depth attachment
        offscreenFrameBuf.depth.destroy();

        device.destroyRenderPass(offscreenFrameBuf.renderPass);

        device.destroyPipeline(pipelines.quad);
        device.destroyPipeline(pipelines.offscreen);
        device.destroyPipeline(pipelines.scene);

        device.destroyPipelineLayout(pipelineLayouts.quad);
        device.destroyPipelineLayout(pipelineLayouts.offscreen);

        device.destroyDescriptorSetLayout(descriptorSetLayout);

        // Meshes
        meshes.scene.destroy();
        meshes.quad.destroy();

        // Uniform buffers
        uniformDataVS.destroy();
        uniformDataOffscreenVS.destroy();

        device.freeCommandBuffers(cmdPool, offscreenCmdBuffer);
        device.destroySemaphore(offscreenSemaphore);
    }

    // Preapre an empty texture as the blit target from 
    // the offscreen framebuffer
    void prepareTextureTarget(uint32_t width, uint32_t height, vk::Format format) {
        // Get device properites for the requested texture format
        vk::FormatProperties formatProperties;
        formatProperties = physicalDevice.getFormatProperties(format);
        // Check if format is supported for optimal tiling
        assert(formatProperties.optimalTilingFeatures &  vk::FormatFeatureFlagBits::eDepthStencilAttachment);

        // Prepare blit target texture
        offscreenFrameBuf.textureTarget.extent.width = width;
        offscreenFrameBuf.textureTarget.extent.height = height;

        vk::ImageCreateInfo imageCreateInfo;
        imageCreateInfo.imageType = vk::ImageType::e2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = vk::Extent3D{ width, height, 1 };
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
        imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
        imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imageCreateInfo.initialLayout = vk::ImageLayout::eTransferDstOptimal;
        imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;

        offscreenFrameBuf.textureTarget = createImage(imageCreateInfo, vk::MemoryPropertyFlagBits::eDeviceLocal);

        offscreenFrameBuf.textureTarget.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        withPrimaryCommandBuffer([&](const vk::CommandBuffer& layoutCmd) {
            vkx::setImageLayout(
                layoutCmd,
                offscreenFrameBuf.textureTarget.image,
                vk::ImageAspectFlagBits::eDepth,
                vk::ImageLayout::ePreinitialized,
                offscreenFrameBuf.textureTarget.imageLayout);
        });

        // Create sampler
        {

            vk::SamplerCreateInfo sampler;
            sampler.magFilter = TEX_FILTER;
            sampler.minFilter = TEX_FILTER;
            sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
            sampler.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            sampler.addressModeV = sampler.addressModeU;
            sampler.addressModeW = sampler.addressModeU;
            sampler.mipLodBias = 0.0f;
            sampler.maxAnisotropy = 0;
            sampler.minLod = 0.0f;
            sampler.maxLod = 1.0f;
            sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
            offscreenFrameBuf.textureTarget.sampler = device.createSampler(sampler);
        }

        // Create image view
        {
            vk::ImageViewCreateInfo view;
            view.viewType = vk::ImageViewType::e2D;
            view.format = format;
            view.components = { vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
            view.subresourceRange = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 };
            view.image = offscreenFrameBuf.textureTarget.image;
            offscreenFrameBuf.textureTarget.view = device.createImageView(view);
        }
    }

    // Set up a separate render pass for the offscreen frame buffer
    // This is necessary as the offscreen frame buffer attachments
    // use formats different to the ones from the visible frame buffer
    // and at least the depth one may not be compatible
    void setupOffScreenRenderPass() {
        vk::AttachmentDescription attDesc[2];
        attDesc[0].format = FB_COLOR_FORMAT;
        attDesc[0].samples = vk::SampleCountFlagBits::e1;
        attDesc[0].loadOp = vk::AttachmentLoadOp::eClear;
        attDesc[0].storeOp = vk::AttachmentStoreOp::eStore;
        attDesc[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attDesc[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attDesc[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attDesc[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        attDesc[1].format = DEPTH_FORMAT;
        attDesc[1].samples = vk::SampleCountFlagBits::e1;
        attDesc[1].loadOp = vk::AttachmentLoadOp::eClear;
        // Since we need to copy the depth attachment contents to our texture
        // used for shadow mapping we must use STORE_OP_STORE to make sure that
        // the depth attachment contents are preserved after rendering to it 
        // has finished
        attDesc[1].storeOp = vk::AttachmentStoreOp::eStore;
        attDesc[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attDesc[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attDesc[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        attDesc[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference colorReference;
        colorReference.attachment = 0;
        colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference depthReference;
        depthReference.attachment = 1;
        depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        vk::RenderPassCreateInfo renderPassCreateInfo;
        renderPassCreateInfo.attachmentCount = 2;
        renderPassCreateInfo.pAttachments = attDesc;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;

        offscreenFrameBuf.renderPass = device.createRenderPass(renderPassCreateInfo);
    }

    void prepareOffscreenFramebuffer() {
        offscreenFrameBuf.width = FB_DIM;
        offscreenFrameBuf.height = FB_DIM;

        vk::Format fbColorFormat = FB_COLOR_FORMAT;

        // Color attachment
        vk::ImageCreateInfo image;
        image.imageType = vk::ImageType::e2D;
        image.format = fbColorFormat;
        image.extent.width = offscreenFrameBuf.width;
        image.extent.height = offscreenFrameBuf.height;
        image.extent.depth = 1;
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = vk::SampleCountFlagBits::e1;
        image.tiling = vk::ImageTiling::eOptimal;
        // vk::Image of the framebuffer is blit source
        image.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;

        offscreenFrameBuf.color = createImage(image, vk::MemoryPropertyFlagBits::eDeviceLocal);
        // Depth stencil attachment
        image.format = DEPTH_FORMAT;
        image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc;
        offscreenFrameBuf.depth = createImage(image, vk::MemoryPropertyFlagBits::eDeviceLocal);



        withPrimaryCommandBuffer([&](const vk::CommandBuffer& layoutCmd) {
            vkx::setImageLayout(
                layoutCmd,
                offscreenFrameBuf.color.image,
                vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal);
            vkx::setImageLayout(
                layoutCmd,
                offscreenFrameBuf.depth.image,
                vk::ImageAspectFlagBits::eDepth,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eDepthStencilAttachmentOptimal);
        });
        {
            vk::ImageViewCreateInfo colorImageView;
            colorImageView.viewType = vk::ImageViewType::e2D;
            colorImageView.format = fbColorFormat;
            colorImageView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            colorImageView.subresourceRange.levelCount = 1;
            colorImageView.subresourceRange.layerCount = 1;
            colorImageView.image = offscreenFrameBuf.color.image;
            offscreenFrameBuf.color.view = device.createImageView(colorImageView);
        }

        {
            vk::ImageViewCreateInfo depthStencilView;
            depthStencilView.viewType = vk::ImageViewType::e2D;
            depthStencilView.format = DEPTH_FORMAT;
            depthStencilView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            depthStencilView.subresourceRange.levelCount = 1;
            depthStencilView.subresourceRange.layerCount = 1;
            depthStencilView.image = offscreenFrameBuf.depth.image;
            offscreenFrameBuf.depth.view = device.createImageView(depthStencilView);
        }

        setupOffScreenRenderPass();

        {
            vk::ImageView attachments[2];
            attachments[0] = offscreenFrameBuf.color.view;
            attachments[1] = offscreenFrameBuf.depth.view;

            // Create frame buffer
            vk::FramebufferCreateInfo fbufCreateInfo;
            fbufCreateInfo.renderPass = offscreenFrameBuf.renderPass;
            fbufCreateInfo.attachmentCount = 2;
            fbufCreateInfo.pAttachments = attachments;
            fbufCreateInfo.width = offscreenFrameBuf.width;
            fbufCreateInfo.height = offscreenFrameBuf.height;
            fbufCreateInfo.layers = 1;

            offscreenFrameBuf.frameBuffer = device.createFramebuffer(fbufCreateInfo);
        }
    }

    void buildOffscreenCommandBuffer() {
        // Create separate command buffer for offscreen 
        // rendering
        if (!offscreenCmdBuffer) {
            vk::CommandBufferAllocateInfo cmd = vkx::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
            offscreenCmdBuffer = device.allocateCommandBuffers(cmd)[0];
        }

        // Create a semaphore used to synchronize offscreen rendering and usage
        vk::SemaphoreCreateInfo semaphoreCreateInfo;
        offscreenSemaphore = device.createSemaphore(semaphoreCreateInfo);

        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = vkx::clearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = offscreenFrameBuf.renderPass;
        renderPassBeginInfo.framebuffer = offscreenFrameBuf.frameBuffer;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = offscreenFrameBuf.width;
        renderPassBeginInfo.renderArea.extent.height = offscreenFrameBuf.height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        offscreenCmdBuffer.begin(cmdBufInfo);

        vk::Viewport viewport = vkx::viewport((float)offscreenFrameBuf.width, (float)offscreenFrameBuf.height, 0.0f, 1.0f);
        offscreenCmdBuffer.setViewport(0, viewport);

        vk::Rect2D scissor = vkx::rect2D(offscreenFrameBuf.width, offscreenFrameBuf.height, 0, 0);
        offscreenCmdBuffer.setScissor(0, scissor);

        // Set depth bias (aka "Polygon offset")
        offscreenCmdBuffer.setDepthBias(depthBiasConstant, 0.0f, depthBiasSlope);

        offscreenCmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

        offscreenCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.offscreen);
        offscreenCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.offscreen, nullptr);

        vk::DeviceSize offsets = 0;
        offscreenCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.scene.vertices.buffer, offsets);
        offscreenCmdBuffer.bindIndexBuffer(meshes.scene.indices.buffer, 0, vk::IndexType::eUint32);
        offscreenCmdBuffer.drawIndexed(meshes.scene.indexCount, 1, 0, 0, 0);

        offscreenCmdBuffer.endRenderPass();

        updateTexture();

        offscreenCmdBuffer.end();
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) {
        vk::Viewport viewport = vkx::viewport((float)width, (float)height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);

        vk::Rect2D scissor = vkx::rect2D(width, height, 0, 0);
        cmdBuffer.setScissor(0, scissor);

        vk::DeviceSize offsets = 0;

        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSet, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.quad);

        // Visualize shadow map
        if (displayShadowMap) {
            cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, offsets);
            cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
            cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
        }

        // 3D scene
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.scene, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.scene);

        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.scene.vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(meshes.scene.indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(meshes.scene.indexCount, 1, 0, 0, 0);
    }

    void draw() override {
        // Get next image in the swap chain (back/front buffer)
        prepareFrame();
        // Submit offscreen command buffer for rendering depth buffer from light's pov

        // Wait for swap chain presentation to finish
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &semaphores.presentComplete;
        // Signal ready with offscreen semaphore
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &offscreenSemaphore;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &offscreenCmdBuffer;

        queue.submit(submitInfo, VK_NULL_HANDLE);

        // Submit current render command buffer
        drawCurrentCommandBuffer(offscreenSemaphore);
        submitFrame();
    }

    void loadMeshes() {
        meshes.scene = loadMesh(getAssetPath() + "models/vulkanscene_shadow.dae", vertexLayout, 4.0f);
    }

    void generateQuad() {
        // Setup vertices for a single uv-mapped quad
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
        // Setup indices
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
        // Example uses three ubos and two image samplers
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 6),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 3);

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
                1)
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

    void setupDescriptorSets() {
        // Textured quad descriptor set
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the shadow map texture
        vk::DescriptorImageInfo texDescriptor =
            vkx::descriptorImageInfo(offscreenFrameBuf.textureTarget.sampler, offscreenFrameBuf.textureTarget.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformDataVS.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Offscreen
        descriptorSets.offscreen = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> offscreenWriteDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.offscreen,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformDataOffscreenVS.descriptor),
        };
        device.updateDescriptorSets(offscreenWriteDescriptorSets.size(), offscreenWriteDescriptorSets.data(), 0, NULL);

        // 3D scene
        descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the shadow map texture
        texDescriptor.sampler = offscreenFrameBuf.textureTarget.sampler;
        texDescriptor.imageView = offscreenFrameBuf.textureTarget.view;

        std::vector<vk::WriteDescriptorSet> sceneDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.scene,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.scene.descriptor),
            // Binding 1 : Fragment shader shadow sampler
            vkx::writeDescriptorSet(
                descriptorSets.scene,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor)
        };
        device.updateDescriptorSets(sceneDescriptorSets.size(), sceneDescriptorSets.data(), 0, NULL);

    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eFront, vk::FrontFace::eClockwise);

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

        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/quad.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/quad.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
            vkx::pipelineCreateInfo(pipelineLayouts.quad, renderPass);

        rasterizationState.cullMode = vk::CullModeFlagBits::eNone;

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

        pipelines.quad = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // 3D scene
        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/scene.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/scene.frag.spv", vk::ShaderStageFlagBits::eFragment);
        rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelines.scene = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Offscreen pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/offscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/offscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelineCreateInfo.layout = pipelineLayouts.offscreen;
        // Cull front faces
        depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
        // Enable depth bias
        rasterizationState.depthBiasEnable = VK_TRUE;
        // Add depth bias to dynamic state, so we can change it at runtime
        dynamicStateEnables.push_back(vk::DynamicState::eDepthBias);
        dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        pipelines.offscreen = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Debug quad vertex shader uniform buffer block
        uniformDataVS = createUniformBuffer(uboVSscene);
        uniformDataVS.map();

        // Offsvreen vertex shader uniform buffer block
        uniformDataOffscreenVS = createUniformBuffer(uboOffscreenVS);
        uniformDataOffscreenVS.map();

        // Scene vertex shader uniform buffer block
        uniformData.scene = createUniformBuffer(uboVSscene);
        uniformData.scene.map();

        updateLight();
        updateUniformBufferOffscreen();
        updateUniformBuffers();
    }

    void updateLight() {
        // Animate the light source
        lightPos.x = cos(glm::radians(timer * 360.0f)) * 40.0f;
        lightPos.y = -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f;
        lightPos.z = 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f;
    }

    void updateUniformBuffers() {
        // Shadow map debug quad
        float AR = (float)height / (float)width;

        uboVSquad.projection = glm::ortho(0.0f, 2.5f / AR, 0.0f, 2.5f, -1.0f, 1.0f);
        uboVSquad.model = glm::mat4();

        uniformDataVS.copy(uboVSquad);

        // 3D scene
        uboVSscene.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, zNear, zFar);

        uboVSscene.view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom));
        uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVSscene.view = glm::rotate(uboVSscene.view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        uboVSscene.model = glm::mat4();

        uboVSscene.lightPos = lightPos;

        // Render scene from light's point of view
        if (lightPOV) {
            uboVSscene.projection = glm::perspective(glm::radians(lightFOV), (float)width / (float)height, zNear, zFar);
            uboVSscene.view = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        }


        uboVSscene.depthBiasMVP = uboOffscreenVS.depthMVP;

        uniformData.scene.copy(uboVSscene);
    }

    void updateUniformBufferOffscreen() {
        // Matrix from light's point of view
        glm::mat4 depthProjectionMatrix = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
        glm::mat4 depthViewMatrix = glm::lookAt(lightPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 depthModelMatrix = glm::mat4();

        uboOffscreenVS.depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;
        uniformDataOffscreenVS.copy(uboOffscreenVS);
    }

    // Copy offscreen depth frame buffer contents to the depth texture
    void updateTexture() {
        // Make sure color writes to the framebuffer are finished before using it as transfer source
        vkx::setImageLayout(
            offscreenCmdBuffer,
            offscreenFrameBuf.depth.image,
            vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eTransferSrcOptimal);

        // Transform texture target to transfer source
        vkx::setImageLayout(
            offscreenCmdBuffer,
            offscreenFrameBuf.textureTarget.image,
            vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eTransferDstOptimal);

        vk::ImageCopy imgCopy;

        imgCopy.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        imgCopy.srcSubresource.layerCount = 1;

        imgCopy.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        imgCopy.dstSubresource.layerCount = 1;

        imgCopy.extent.width = TEX_DIM;
        imgCopy.extent.height = TEX_DIM;
        imgCopy.extent.depth = 1;

        offscreenCmdBuffer.copyImage(offscreenFrameBuf.depth.image, vk::ImageLayout::eTransferSrcOptimal, offscreenFrameBuf.textureTarget.image, vk::ImageLayout::eTransferDstOptimal, imgCopy);

        // Transform framebuffer color attachment back 
        vkx::setImageLayout(
            offscreenCmdBuffer,
            offscreenFrameBuf.depth.image,
            vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal);

        // Transform texture target back to shader read
        // Makes sure that writes to the textuer are finished before
        // it's accessed in the shader
        vkx::setImageLayout(
            offscreenCmdBuffer,
            offscreenFrameBuf.textureTarget.image,
            vk::ImageAspectFlagBits::eDepth,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    void prepare() {
        ExampleBase::prepare();
        generateQuad();
        loadMeshes();
        setupVertexDescriptions();
        prepareUniformBuffers();
        prepareTextureTarget(TEX_DIM, TEX_DIM, DEPTH_FORMAT);
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSets();
        prepareOffscreenFramebuffer();
        updateDrawCommandBuffers();
        buildOffscreenCommandBuffer();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        if (!paused) {
            vkDeviceWaitIdle(device);
            updateLight();
            updateUniformBufferOffscreen();
            updateUniformBuffers();
        }
    }

    virtual void viewChanged() {
        vkDeviceWaitIdle(device);
        updateUniformBufferOffscreen();
        updateUniformBuffers();
    }

    void toggleShadowMapDisplay() {
        displayShadowMap = !displayShadowMap;
        updateDrawCommandBuffers();
    }

    void toogleLightPOV() {
        lightPOV = !lightPOV;
        viewChanged();
    }

    void keyPressed(uint32_t key) override {
        switch (key) {
        case GLFW_KEY_S:
            toggleShadowMapDisplay();
            break;
        case GLFW_KEY_L:
            toogleLightPOV();
            break;
        }
    }
};

RUN_EXAMPLE(VulkanExample)
