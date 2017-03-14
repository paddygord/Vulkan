/*
* Vulkan Example - Compute shader image processing
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
};

class VulkanExample : public vkx::ExampleBase {
private:
    vkx::Texture textureColorMap;
    vkx::Texture textureComputeTarget;
public:
    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::MeshBuffer quad;
    } meshes;

    vkx::UniformData uniformDataVS;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVS;

    struct Pipelines {
        vk::Pipeline postCompute;
        // Compute pipelines are separated from 
        // graphics pipelines in Vulkan
        std::vector<vk::Pipeline> compute;
        uint32_t computeIndex{ 0 };
    } pipelines;

    vk::Queue computeQueue;
    vk::CommandBuffer computeCmdBuffer;
    vk::PipelineLayout computePipelineLayout;
    vk::DescriptorSet computeDescriptorSet;
    vk::DescriptorSetLayout computeDescriptorSetLayout;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSetPostCompute, descriptorSetBaseImage;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        camera.setZoom(-2.0f);
        enableTextOverlay = true;
        title = "Vulkan Example - Compute shader image processing";
    }

    ~VulkanExample() {
        queue.waitIdle();
        if (computeQueue != queue) {
            computeQueue.waitIdle();
        }

        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class
        device.destroyPipelineLayout(computePipelineLayout);
        device.destroyDescriptorSetLayout(computeDescriptorSetLayout);
        device.freeCommandBuffers(cmdPool, computeCmdBuffer);
        //device.freeDescriptorSets(descriptorPool, computeDescriptorSet);


        device.destroyPipeline(pipelines.postCompute);
        for (auto& pipeline : pipelines.compute) {
            device.destroyPipeline(pipeline);
        }

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);
        meshes.quad.destroy();
        uniformDataVS.destroy();
        textureColorMap.destroy();
        textureComputeTarget.destroy();
    }

    // Prepare a texture target that is used to store compute shader calculations
    void prepareTextureTarget(vkx::Texture &tex, uint32_t width, uint32_t height, vk::Format format) {
        vk::FormatProperties formatProperties;

        // Get device properties for the requested texture format
        formatProperties = physicalDevice.getFormatProperties(format);
        // Check if requested image format supports image storage operations
        assert(formatProperties.optimalTilingFeatures &  vk::FormatFeatureFlagBits::eStorageImage);

        // Prepare blit target texture
        tex.extent.width = width;
        tex.extent.height = height;

        vk::ImageCreateInfo imageCreateInfo;
        imageCreateInfo.imageType = vk::ImageType::e2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = vk::Extent3D{ width, height, 1 };
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
        imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
        // vk::Image will be sampled in the fragment shader and used as storage target in the compute shader
        imageCreateInfo.usage =
            vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eStorage;

        tex = createImage(imageCreateInfo, vk::MemoryPropertyFlagBits::eDeviceLocal);
        tex.imageLayout = vk::ImageLayout::eGeneral;
        withPrimaryCommandBuffer([&](const vk::CommandBuffer& layoutCmd) {
            tex.imageLayout = vk::ImageLayout::eGeneral;
            vkx::setImageLayout(
                layoutCmd, tex.image,
                vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::eUndefined,
                tex.imageLayout);
        });

        // Create sampler
        vk::SamplerCreateInfo sampler;
        sampler.magFilter = vk::Filter::eLinear;
        sampler.minFilter = vk::Filter::eLinear;
        sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
        sampler.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        sampler.addressModeV = sampler.addressModeU;
        sampler.addressModeW = sampler.addressModeU;
        sampler.mipLodBias = 0.0f;
        sampler.maxAnisotropy = 0;
        sampler.compareOp = vk::CompareOp::eNever;
        sampler.minLod = 0.0f;
        sampler.maxLod = 0.0f;
        sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        tex.sampler = device.createSampler(sampler);

        // Create image view
        vk::ImageViewCreateInfo view;
        view.viewType = vk::ImageViewType::e2D;
        view.format = format;
        view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        view.image = tex.image;
        tex.view = device.createImageView(view);
    }

    void loadTextures() {
        textureColorMap = textureLoader->loadTexture(
            getAssetPath() + "textures/het_kanonschot_rgba8.ktx",
            vk::Format::eR8G8B8A8Unorm);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setScissor(0, vkx::rect2D(size));

        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, { 0 });

        cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
        // Left (pre compute)
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSetBaseImage, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.postCompute);

        vk::Viewport viewport = vkx::viewport((float)size.width / 2, (float)size.height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);

        // vk::Image memory barrier to make sure that compute
        // shader writes are finished before sampling
        // from the texture
        vk::ImageMemoryBarrier imageMemoryBarrier;
        imageMemoryBarrier.oldLayout = vk::ImageLayout::eGeneral;
        imageMemoryBarrier.newLayout = vk::ImageLayout::eGeneral;
        imageMemoryBarrier.image = textureComputeTarget.image;
        imageMemoryBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead;

        // todo : use different pipeline stage bits
        cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), nullptr, nullptr, imageMemoryBarrier);

        // Right (post compute)
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSetPostCompute, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.postCompute);

        viewport.x = viewport.width;
        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
    }

    void buildComputeCommandBuffer() {
        // FIXME find a better way to block on re-using the compute command, or build multiple command buffers
        queue.waitIdle();
        vk::CommandBufferBeginInfo cmdBufInfo;
        cmdBufInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
        computeCmdBuffer.begin(cmdBufInfo);
        computeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines.compute[pipelines.computeIndex]);
        computeCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout, 0, computeDescriptorSet, nullptr);
        computeCmdBuffer.dispatch(textureComputeTarget.extent.width / 16, textureComputeTarget.extent.height / 16, 1);
        computeCmdBuffer.end();
    }

    // Setup vertices for a single uv-mapped quad
    void generateQuad() {
#define dim 1.0f
        std::vector<Vertex> vertexBuffer =
        {
            { {  dim,  dim, 0.0f }, { 1.0f, 1.0f } },
            { { -dim,  dim, 0.0f }, { 0.0f, 1.0f } },
            { { -dim, -dim, 0.0f }, { 0.0f, 0.0f } },
            { {  dim, -dim, 0.0f }, { 1.0f, 0.0f } }
        };
#undef dim
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
            vkx::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex);

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(2);
        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);
        // Location 1 : Texture coordinates
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32Sfloat, sizeof(float) * 3);

        // Assign to vertex buffer
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
            // Graphics pipeline uses image samplers for display
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4),
            // Compute pipeline uses a sampled image for reading
            vkx::descriptorPoolSize(vk::DescriptorType::eSampledImage, 1),
            // Compute pipelines uses a storage image to write result
            vkx::descriptorPoolSize(vk::DescriptorType::eStorageImage, 1),
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 3);

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

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSetPostCompute = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the color map texture
        vk::DescriptorImageInfo texDescriptor =
            vkx::descriptorImageInfo(textureComputeTarget.sampler, textureComputeTarget.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSetPostCompute,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformDataVS.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSetPostCompute,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Base image (before compute post process)
        allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSetBaseImage = device.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo texDescriptorBaseImage =
            vkx::descriptorImageInfo(textureColorMap.sampler, textureColorMap.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> baseImageWriteDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSetBaseImage,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformDataVS.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSetBaseImage,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptorBaseImage)
        };

        device.updateDescriptorSets(baseImageWriteDescriptorSets.size(), baseImageWriteDescriptorSets.data(), 0, NULL);
    }

    // Create a separate command buffer for compute commands
    void createComputeCommandBuffer() {
        vk::CommandBufferAllocateInfo cmdBufAllocateInfo =
            vkx::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
        computeCmdBuffer = device.allocateCommandBuffers(cmdBufAllocateInfo)[0];
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

        // Rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/computeshader/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/computeshader/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);

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
        pipelineCreateInfo.renderPass = renderPass;

        pipelines.postCompute = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    void prepareCompute() {
        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines
        // even if they use the same queue

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Sampled image (read)
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eSampledImage,
                vk::ShaderStageFlagBits::eCompute,
                0),
            // Binding 1 : Sampled image (write)
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eStorageImage,
                vk::ShaderStageFlagBits::eCompute,
                1),
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        computeDescriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&computeDescriptorSetLayout, 1);

        computePipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);

        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &computeDescriptorSetLayout, 1);

        computeDescriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::DescriptorImageInfo> computeTexDescriptors =
        {
            vkx::descriptorImageInfo(
                vk::Sampler(),
                textureColorMap.view,
                vk::ImageLayout::eGeneral),

            vkx::descriptorImageInfo(
                vk::Sampler(),
                textureComputeTarget.view,
                vk::ImageLayout::eGeneral)
        };

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets =
        {
            // Binding 0 : Sampled image (read)
            vkx::writeDescriptorSet(
                computeDescriptorSet,
                vk::DescriptorType::eSampledImage,
                0,
                &computeTexDescriptors[0]),
            // Binding 1 : Sampled image (write)
            vkx::writeDescriptorSet(
                computeDescriptorSet,
                vk::DescriptorType::eStorageImage,
                1,
                &computeTexDescriptors[1])
        };

        device.updateDescriptorSets(computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, NULL);


        // Create compute shader pipelines
        vk::ComputePipelineCreateInfo computePipelineCreateInfo =
            vkx::computePipelineCreateInfo(computePipelineLayout);

        // One pipeline for each effect
        std::vector<std::string> shaderNames = { "sharpen", "edgedetect", "emboss" };
        for (auto& shaderName : shaderNames) {
            std::string fileName = getAssetPath() + "shaders/computeshader/" + shaderName + ".comp.spv";
            computePipelineCreateInfo.stage = loadShader(fileName.c_str(), vk::ShaderStageFlagBits::eCompute);
            vk::Pipeline pipeline;
            pipeline = device.createComputePipelines(pipelineCache, computePipelineCreateInfo, nullptr)[0];

            pipelines.compute.push_back(pipeline);
        }
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformDataVS = createBuffer(vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, uboVS);
        uniformDataVS.map();
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader uniform buffer block
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)(size.width/ 2) / size.height, 0.1f, 256.0f);
        uboVS.model = camera.matrices.view;
        uniformDataVS.copy(uboVS);
    }

    // Find and create a compute capable device queue
    void getComputeQueue() {
        uint32_t queueIndex = 0;
        std::vector<vk::QueueFamilyProperties> queueProps = physicalDevice.getQueueFamilyProperties();
        uint32_t queueCount = queueProps.size();

        for (queueIndex = 0; queueIndex < queueCount; queueIndex++) {
            if (queueProps[queueIndex].queueFlags & vk::QueueFlagBits::eCompute)
                break;
        }
        assert(queueIndex < queueCount);

        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = queueIndex;
        queueCreateInfo.queueCount = 1;
        computeQueue = device.getQueue(queueIndex, 0);
    }

    void compute() {
        // Submit compute
        vk::SubmitInfo computeSubmitInfo = vk::SubmitInfo();
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &computeCmdBuffer;

        computeQueue.submit(computeSubmitInfo, nullptr);
    }

    void prepare() {
        ExampleBase::prepare();
        loadTextures();
        generateQuad();
        getComputeQueue();
        createComputeCommandBuffer();
        setupVertexDescriptions();
        prepareUniformBuffers();
        prepareTextureTarget(textureComputeTarget, textureColorMap.extent.width, textureColorMap.extent.height, vk::Format::eR8G8B8A8Unorm);
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        prepareCompute();
        updateDrawCommandBuffers();
        buildComputeCommandBuffer();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        compute();
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }

    virtual void keyPressed(uint32_t keyCode) {
        switch (keyCode) {
        case GLFW_KEY_KP_ADD:
        case GAMEPAD_BUTTON_R1:
            switchComputePipeline(1);
            break;
        case GLFW_KEY_KP_SUBTRACT:
        case GAMEPAD_BUTTON_L1:
            switchComputePipeline(-1);
            break;
        }
    }

    virtual void getOverlayText(vkx::TextOverlay *textOverlay) {
#if defined(__ANDROID__)
        textOverlay->addText("Press \"L1/R1\" to change shaders", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
#else
        textOverlay->addText("Press \"NUMPAD +/-\" to change shaders", 5.0f, 85.0f, vkx::TextOverlay::alignLeft);
#endif
    }

    virtual void switchComputePipeline(int32_t dir) {
        if ((dir < 0) && (pipelines.computeIndex > 0)) {
            pipelines.computeIndex--;
            buildComputeCommandBuffer();
        }
        if ((dir > 0) && (pipelines.computeIndex < pipelines.compute.size() - 1)) {
            pipelines.computeIndex++;
            buildComputeCommandBuffer();
        }
    }
};

RUN_EXAMPLE(VulkanExample)
