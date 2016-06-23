/*
* Vulkan Example - Multisampling using resolve attachments
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"

#define SAMPLE_COUNT vk::SampleCountFlagBits::e4

struct {
    struct {
        vk::Image image;
        vk::ImageView view;
        vk::DeviceMemory memory;
    } color;
    struct {
        vk::Image image;
        vk::ImageView view;
        vk::DeviceMemory memory;
    } depth;
} multisampleTarget;

// Vertex layout for this example
std::vector<vkx::VertexLayout> vertexLayout =
{
    vkx::VertexLayout::VERTEX_LAYOUT_POSITION,
    vkx::VertexLayout::VERTEX_LAYOUT_NORMAL,
    vkx::VertexLayout::VERTEX_LAYOUT_UV,
    vkx::VertexLayout::VERTEX_LAYOUT_COLOR,
};

class VulkanExample : public vkx::ExampleBase {
public:
    struct {
        vkx::Texture colorMap;
    } textures;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::MeshBuffer example;
    } meshes;

    struct {
        vkx::UniformData vsScene;
    } uniformData;

    struct UboVS {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec4 lightPos = glm::vec4(5.0f, 5.0f, 5.0f, 1.0f);
    } uboVS;

    struct {
        vk::Pipeline solid;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        zoom = -7.5f;
        zoomSpeed = 2.5f;
        rotation = { 0.0f, -90.0f, 0.0f };
        cameraPos = glm::vec3(2.5f, 2.5f, 0.0f);
        title = "Vulkan Example - Multisampling";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class
        device.destroyPipeline(pipelines.solid);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        meshes.example.destroy();

        // Destroy MSAA target
        device.destroyImage(multisampleTarget.color.image);
        device.destroyImageView(multisampleTarget.color.view);
        device.freeMemory(multisampleTarget.color.memory);
        device.destroyImage(multisampleTarget.depth.image);
        device.destroyImageView(multisampleTarget.depth.view);
        device.freeMemory(multisampleTarget.depth.memory);

        textures.colorMap.destroy();

        uniformData.vsScene.destroy();
    }

    // Creates a multi sample render target (image and view) that is used to resolve 
    // into the visible frame buffer target in the render pass
    void setupMultisampleTarget() {
        // Check if device supports requested sample count for color and depth frame buffer
        VkSampleCountFlags colorSampleCount = deviceProperties.limits.framebufferColorSampleCounts.operator VkSampleCountFlags();
        VkSampleCountFlags depthSampleCount = deviceProperties.limits.framebufferDepthSampleCounts.operator VkSampleCountFlags();
        VkSampleCountFlags requiredSamples = ((VkSampleCountFlagBits)SAMPLE_COUNT);
        assert(colorSampleCount >= requiredSamples && depthSampleCount >= requiredSamples);

        // Color target
        vk::ImageCreateInfo info;
        info.imageType = vk::ImageType::e2D;
        info.format = colorformat;
        info.extent.width = width;
        info.extent.height = height;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.sharingMode = vk::SharingMode::eExclusive;
        info.tiling = vk::ImageTiling::eOptimal;
        info.samples = SAMPLE_COUNT;
        // vk::Image will only be used as a transient target
        info.usage = vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment;
        info.initialLayout = vk::ImageLayout::eUndefined;

        multisampleTarget.color.image = device.createImage(info);

        vk::MemoryRequirements memReqs;
        memReqs = device.getImageMemoryRequirements(multisampleTarget.color.image);
        vk::MemoryAllocateInfo memAlloc;
        memAlloc.allocationSize = memReqs.size;
        // We prefer a lazily allocated memory type
        // This means that the memory get allocated when the implementation sees fit, e.g. when first using the images
        vk::Bool32 lazyMemType = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eLazilyAllocated, &memAlloc.memoryTypeIndex);
        if (!lazyMemType) {
            // If this is not available, fall back to device local memory
            getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAlloc.memoryTypeIndex);
        }
        multisampleTarget.color.memory = device.allocateMemory(memAlloc);
        device.bindImageMemory(multisampleTarget.color.image, multisampleTarget.color.memory, 0);

        // Create image view for the MSAA target
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = multisampleTarget.color.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = colorformat;
        viewInfo.components.r = vk::ComponentSwizzle::eR;
        viewInfo.components.g = vk::ComponentSwizzle::eG;
        viewInfo.components.b = vk::ComponentSwizzle::eB;
        viewInfo.components.a = vk::ComponentSwizzle::eA;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        multisampleTarget.color.view = device.createImageView(viewInfo);

        // Depth target
        info.imageType = vk::ImageType::e2D;
        info.format = depthFormat;
        info.extent.width = width;
        info.extent.height = height;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.sharingMode = vk::SharingMode::eExclusive;
        info.tiling = vk::ImageTiling::eOptimal;
        info.samples = SAMPLE_COUNT;
        // vk::Image will only be used as a transient target
        info.usage = vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment;
        info.initialLayout = vk::ImageLayout::eUndefined;

        multisampleTarget.depth.image = device.createImage(info);

        memReqs = device.getImageMemoryRequirements(multisampleTarget.depth.image);
        memAlloc;
        memAlloc.allocationSize = memReqs.size;
        lazyMemType = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eLazilyAllocated, &memAlloc.memoryTypeIndex);
        if (!lazyMemType) {
            getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal, &memAlloc.memoryTypeIndex);
        }

        multisampleTarget.depth.memory = device.allocateMemory(memAlloc);
        device.bindImageMemory(multisampleTarget.depth.image, multisampleTarget.depth.memory, 0);

        // Create image view for the MSAA target
        viewInfo.image = multisampleTarget.depth.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        multisampleTarget.depth.view = device.createImageView(viewInfo);
    }

    // Setup a render pass for using a multi sampled attachment 
    // and a resolve attachment that the msaa image is resolved 
    // to at the end of the render pass
    void setupRenderPass() {
        // Overrides the virtual function of the base class

        std::array<vk::AttachmentDescription, 4> attachments = {};

        // Multisampled attachment that we render to
        attachments[0].format = colorformat;
        attachments[0].samples = SAMPLE_COUNT;
        attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
        // No longer required after resolve, this may save some bandwidth on certain GPUs
        attachments[0].storeOp = vk::AttachmentStoreOp::eDontCare;
        attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachments[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        // This is the frame buffer attachment to where the multisampled image
        // will be resolved to and which will be presented to the swapchain
        attachments[1].format = colorformat;
        attachments[1].samples = vk::SampleCountFlagBits::e1;
        attachments[1].loadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
        attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachments[1].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachments[1].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        // Multisampled depth attachment we render to
        attachments[2].format = depthFormat;
        attachments[2].samples = SAMPLE_COUNT;
        attachments[2].loadOp = vk::AttachmentLoadOp::eClear;
        attachments[2].storeOp = vk::AttachmentStoreOp::eDontCare;
        attachments[2].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[2].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachments[2].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        attachments[2].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        // Depth resolve attachment
        attachments[3].format = depthFormat;
        attachments[3].samples = vk::SampleCountFlagBits::e1;
        attachments[3].loadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[3].storeOp = vk::AttachmentStoreOp::eStore;
        attachments[3].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachments[3].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachments[3].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        attachments[3].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference colorReference;
        colorReference.attachment = 0;
        colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference depthReference;
        depthReference.attachment = 2;
        depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        // Two resolve attachment references for color and depth
        std::array<vk::AttachmentReference, 2> resolveReferences = {};
        resolveReferences[0].attachment = 1;
        resolveReferences[0].layout = vk::ImageLayout::eColorAttachmentOptimal;
        resolveReferences[1].attachment = 3;
        resolveReferences[1].layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        // Pass our resolve attachments to the sub pass
        subpass.pResolveAttachments = resolveReferences.data();
        subpass.pDepthStencilAttachment = &depthReference;

        vk::RenderPassCreateInfo renderPassInfo;
        renderPassInfo.attachmentCount = attachments.size();
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.createRenderPass(renderPassInfo);
    }

    // Frame buffer attachments must match with render pass setup, 
    // so we need to adjust frame buffer creation to cover our 
    // multisample target
    void setupFrameBuffer() {
        // Overrides the virtual function of the base class

        std::array<vk::ImageView, 4> attachments;

        setupMultisampleTarget();

        attachments[0] = multisampleTarget.color.view;
        // attachment[1] = swapchain image
        attachments[2] = multisampleTarget.depth.view;
        attachments[3] = depthStencil.view;

        vk::FramebufferCreateInfo frameBufferCreateInfo;
        frameBufferCreateInfo.renderPass = renderPass;
        frameBufferCreateInfo.attachmentCount = attachments.size();
        frameBufferCreateInfo.pAttachments = attachments.data();
        frameBufferCreateInfo.width = width;
        frameBufferCreateInfo.height = height;
        frameBufferCreateInfo.layers = 1;

        // Create frame buffers for every swap chain image
        framebuffers.resize(swapChain.imageCount);
        for (uint32_t i = 0; i < framebuffers.size(); i++) {
            attachments[1] = swapChain.buffers[i].view;
            framebuffers[i] = device.createFramebuffer(frameBufferCreateInfo);
        }
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) {

        //// Initial image layout transitions
        //// We need to transform the MSAA target layouts before using them
        //withPrimaryCommandBuffer([&](const vk::CommandBuffer& setupCmdBuffer) {
        //    // Tansform MSAA color target
        //    vkx::setImageLayout(
        //        setupCmdBuffer,
        //        multisampleTarget.color.image,
        //        vk::ImageAspectFlagBits::eColor,
        //        vk::ImageLayout::eUndefined,
        //        vk::ImageLayout::eColorAttachmentOptimal);

        //    // Tansform MSAA depth target
        //    vkx::setImageLayout(
        //        setupCmdBuffer,
        //        multisampleTarget.depth.image,
        //        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
        //        vk::ImageLayout::eUndefined,
        //        vk::ImageLayout::eDepthStencilAttachmentOptimal);
        //});

        //vk::CommandBufferBeginInfo cmdBufInfo;

        //vk::ClearValue clearValues[3];
        //// Clear to a white background for higher contrast
        //clearValues[0].color = vkx::clearColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        //clearValues[1].color = vkx::clearColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        //clearValues[2].depthStencil = { 1.0f, 0 };

        vk::Viewport viewport = vkx::viewport((float)width, (float)height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);

        vk::Rect2D scissor = vkx::rect2D(width, height, 0, 0);
        cmdBuffer.setScissor(0, scissor);

        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);

        vk::DeviceSize offsets = 0;
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(meshes.example.indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);
    }

    void loadTextures() {
        textures.colorMap = textureLoader->loadTexture(
            getAssetPath() + "models/voyager/voyager.ktx",
             vk::Format::eBc3UnormBlock);
    }

    void loadMeshes() {
        meshes.example = loadMesh(getAssetPath() + "models/voyager/voyager.dae", vertexLayout, 1.0f);
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
        // Location 1 : Normal
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1,  vk::Format::eR32G32B32Sfloat, sizeof(float) * 3);
        // Location 2 : Texture coordinates
        vertices.attributeDescriptions[2] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2,  vk::Format::eR32G32Sfloat, sizeof(float) * 6);
        // Location 3 : Color
        vertices.attributeDescriptions[3] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3,  vk::Format::eR32G32B32Sfloat, sizeof(float) * 8);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        // Example uses one ubo and one combined image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1),
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
            // Binding 1 : Fragment shader combined sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                1),
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

        vk::DescriptorImageInfo texDescriptor =
            vkx::descriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
            descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsScene.descriptor),
            // Binding 1 : Color map 
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise);

        vk::PipelineColorBlendAttachmentState blendAttachmentState =
            vkx::pipelineColorBlendAttachmentState();

        vk::PipelineColorBlendStateCreateInfo colorBlendState =
            vkx::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vkx::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);

        vk::PipelineViewportStateCreateInfo viewportState =
            vkx::pipelineViewportStateCreateInfo(1, 1);

        vk::PipelineMultisampleStateCreateInfo multisampleState =
            vkx::pipelineMultisampleStateCreateInfo(SAMPLE_COUNT);

        std::vector<vk::DynamicState> dynamicStateEnables = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        // Solid rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/mesh/mesh.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/mesh/mesh.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

        pipelines.solid = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformData.vsScene = createUniformBuffer(uboVS);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader
        glm::mat4 viewMatrix = glm::mat4();
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
        viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, zoom));

        float offset = 0.5f;
        int uboIndex = 1;
        uboVS.model = glm::mat4();
        uboVS.model = viewMatrix * glm::translate(uboVS.model, cameraPos);
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        void *pData = device.mapMemory(uniformData.vsScene.memory, 0, sizeof(uboVS), vk::MemoryMapFlags());
        memcpy(pData, &uboVS, sizeof(uboVS));
        device.unmapMemory(uniformData.vsScene.memory);
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
