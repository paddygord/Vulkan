/*
* Vulkan Example - Compute shader ray tracing
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


#define TEX_DIM 2048

// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
};

class VulkanExample : public vkx::ExampleBase {
private:
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

    vkx::UniformData uniformDataCompute;

    struct UboCompute {
        glm::vec3 lightPos;
        // Aspect ratio of the viewport
        float aspectRatio;
        glm::vec4 fogColor = glm::vec4(0.0f);
        struct Camera {
            glm::vec3 pos = glm::vec3(0.0f, 1.5f, 4.0f);
            glm::vec3 lookat = glm::vec3(0.0f, 0.5f, 0.0f);
            float fov = 10.0f;
        } camera;
    } uboCompute;

    struct {
        vk::Pipeline display;
        vk::Pipeline compute;
    } pipelines;

    int vertexBufferSize;

    vk::Queue computeQueue;
    vk::CommandBuffer computeCmdBuffer;
    vk::PipelineLayout computePipelineLayout;
    vk::DescriptorSet computeDescriptorSet;
    vk::DescriptorSetLayout computeDescriptorSetLayout;
    vk::DescriptorPool computeDescriptorPool;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSetPostCompute;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() : vkx::ExampleBase(ENABLE_VALIDATION) {
        zoom = -2.0f;
        title = "Vulkan Example - Compute shader ray tracing";
        uboCompute.aspectRatio = (float)width / (float)height;
        paused = true;
        timerSpeed *= 0.5f;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        device.destroyPipeline(pipelines.display);
        device.destroyPipeline(pipelines.compute);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        meshes.quad.destroy();
        uniformDataCompute.destroy();

        device.freeCommandBuffers(cmdPool, computeCmdBuffer);

        textureComputeTarget.destroy();
    }

    // Prepare a texture target that is used to store compute shader calculations
    void prepareTextureTarget(vkx::Texture& tex, uint32_t width, uint32_t height, vk::Format format) {
        withPrimaryCommandBuffer([&](const vk::CommandBuffer& setupCmdBuffer) {
            // Get device properties for the requested texture format
            vk::FormatProperties formatProperties;
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
            imageCreateInfo.initialLayout = vk::ImageLayout::ePreinitialized;
            // vk::Image will be sampled in the fragment shader and used as storage target in the compute shader
            imageCreateInfo.usage =
                vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eStorage;

            tex = createImage(imageCreateInfo, vk::MemoryPropertyFlagBits::eDeviceLocal);

            tex.imageLayout = vk::ImageLayout::eGeneral;
            vkx::setImageLayout(
                setupCmdBuffer, tex.image,
                vk::ImageAspectFlagBits::eColor,
                vk::ImageLayout::ePreinitialized,
                tex.imageLayout);

            // Create sampler
            vk::SamplerCreateInfo sampler;
            sampler.magFilter = vk::Filter::eLinear;
            sampler.minFilter = vk::Filter::eLinear;
            sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
            sampler.addressModeU = vk::SamplerAddressMode::eRepeat;
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
        });
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) {

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
        cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), nullptr, nullptr, imageMemoryBarrier);

        vk::Viewport viewport = vkx::viewport((float)width, (float)height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);

        vk::Rect2D scissor = vkx::rect2D(width, height, 0, 0);
        cmdBuffer.setScissor(0, scissor);

        vk::DeviceSize offsets = 0;
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);

        // Display ray traced image generated by compute shader as a full screen quad

        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSetPostCompute, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.display);

        cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
    }

    void buildComputeCommandBuffer() {
        vk::CommandBufferBeginInfo cmdBufInfo;
        computeCmdBuffer.begin(cmdBufInfo);
        computeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines.compute);
        computeCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout, 0, computeDescriptorSet, nullptr);
        computeCmdBuffer.dispatch(textureComputeTarget.extent.width / 16, textureComputeTarget.extent.height / 16, 1);
        computeCmdBuffer.end();
    }

    void compute() {
        // Compute
        vk::SubmitInfo computeSubmitInfo;
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &computeCmdBuffer;
        computeQueue.submit(computeSubmitInfo, VK_NULL_HANDLE);
        computeQueue.waitIdle();
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
            // Compute pipeline uses storage images image loads and stores
            vkx::descriptorPoolSize(vk::DescriptorType::eStorageImage, 1),
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 3);

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
        {
            // Binding 0 : Fragment shader image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                0)
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
            // Binding 0 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSetPostCompute,
                vk::DescriptorType::eCombinedImageSampler,
                0,
                &texDescriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
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

        // Display pipeline
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/raytracing/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/raytracing/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

        pipelines.display = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

    }

    // Prepare the compute pipeline that generates the ray traced image
    void prepareCompute() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Sampled image (write)
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eStorageImage,
                vk::ShaderStageFlagBits::eCompute,
                0),
            // Binding 1 : Uniform buffer block
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eCompute,
                1)
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
                VK_NULL_HANDLE,
                textureComputeTarget.view,
                vk::ImageLayout::eGeneral)
        };

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets =
        {
            // Binding 0 : Output storage image
            vkx::writeDescriptorSet(
                computeDescriptorSet,
                vk::DescriptorType::eStorageImage,
                0,
                &computeTexDescriptors[0]),
            // Binding 1 : Uniform buffer block
            vkx::writeDescriptorSet(
                computeDescriptorSet,
                vk::DescriptorType::eUniformBuffer,
                1,
                &uniformDataCompute.descriptor)
        };

        device.updateDescriptorSets(computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, NULL);


        // Create compute shader pipelines
        vk::ComputePipelineCreateInfo computePipelineCreateInfo =
            vkx::computePipelineCreateInfo(computePipelineLayout);

        computePipelineCreateInfo.stage = loadShader(getAssetPath() + "shaders/raytracing/raytracing.comp.spv", vk::ShaderStageFlagBits::eCompute);
        pipelines.compute = device.createComputePipelines(pipelineCache, computePipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformDataCompute = createUniformBuffer(uboCompute);
        uniformDataCompute.map();
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboCompute.lightPos.x = 0.0f + sin(glm::radians(timer * 360.0f)) * 2.0f;
        uboCompute.lightPos.y = 5.0f;
        uboCompute.lightPos.z = 1.0f;
        uboCompute.lightPos.z = 0.0f + cos(glm::radians(timer * 360.0f)) * 2.0f;
        uniformDataCompute.copy(uboCompute);
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

    void prepare() {
        ExampleBase::prepare();
        generateQuad();
        getComputeQueue();
        createComputeCommandBuffer();
        setupVertexDescriptions();
        prepareUniformBuffers();
        prepareTextureTarget(
            textureComputeTarget,
            TEX_DIM,
            TEX_DIM,
            vk::Format::eR8G8B8A8Unorm);
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
        if (!paused) {
            updateUniformBuffers();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }
};

RUN_EXAMPLE(VulkanExample)
