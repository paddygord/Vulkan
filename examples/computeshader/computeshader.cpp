/*
* Vulkan Example - Compute shader image processing
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>

// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
};

class VulkanExample : public vkx::ExampleBase {
private:
    vks::texture::Texture2D textureColorMap;
    vks::Image textureComputeTarget;

public:
    struct {
        vks::model::Model quad;
    } meshes;

    vks::Buffer uniformDataVS;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVS;

    struct Graphics {
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSet descriptorSetPreCompute;
        vk::DescriptorSet descriptorSetPostCompute;
        vk::Pipeline pipeline;
        vk::DescriptorSetLayout descriptorSetLayout;
    } graphics;

    struct Compute {
        vk::Queue queue;
        vk::CommandPool commandPool;
        vk::CommandBuffer commandBuffer;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorSet descriptorSet;
        std::vector<vk::Pipeline> pipelines;
        int32_t pipelineIndex{ 0 };
    } compute;

    const std::vector<std::string> shaderNames { "sharpen", "edgedetect", "emboss" };

    VulkanExample() {
        camera.dolly(-2.0f);
        title = "Vulkan Example - Compute shader image processing";
    }

    ~VulkanExample() {
        queue.waitIdle();
        if (compute.queue != queue) {
            compute.queue.waitIdle();
        }

        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        device.destroyPipelineLayout(compute.pipelineLayout);
        device.destroyDescriptorSetLayout(compute.descriptorSetLayout);
        device.freeCommandBuffers(cmdPool, compute.commandBuffer);
        //device.freeDescriptorSets(descriptorPool, computeDescriptorSet);

        device.destroyPipeline(graphics.pipeline);
        for (auto& pipeline : compute.pipelines) {
            device.destroyPipeline(pipeline);
        }

        device.destroyPipelineLayout(graphics.pipelineLayout);
        device.destroyDescriptorSetLayout(graphics.descriptorSetLayout);
        meshes.quad.destroy();
        uniformDataVS.destroy();
        textureColorMap.destroy();
        textureComputeTarget.destroy();
    }

    // Prepare a texture target that is used to store compute shader calculations
    vks::Image prepareTextureTarget(vk::ImageLayout targetLayout, const vk::Extent3D& extent, vk::Format format) {
        vk::FormatProperties formatProperties;

        // Get device properties for the requested texture format
        formatProperties = context.physicalDevice.getFormatProperties(format);
        // Check if requested image format supports image storage operations
        assert(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage);

        // Prepare blit target texture

        vk::ImageCreateInfo imageCreateInfo;
        imageCreateInfo.imageType = vk::ImageType::e2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = extent;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        // vk::Image will be sampled in the fragment shader and used as storage target in the compute shader
        imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;

        vks::Image result = context.createImage(imageCreateInfo, vk::MemoryPropertyFlagBits::eDeviceLocal);
        //        result.extent = extent;
        context.setImageLayout(result.image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eUndefined, targetLayout);

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
        result.sampler = device.createSampler(sampler);

        // Create image view
        vk::ImageViewCreateInfo view;
        view.viewType = vk::ImageViewType::e2D;
        view.format = format;
        view.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        view.image = result.image;
        result.view = device.createImageView(view);
        return result;
    }

    void loadTextures() { textureColorMap.loadFromFile(context, getAssetPath() + "textures/het_kanonschot_rgba8.ktx", vk::Format::eR8G8B8A8Unorm); }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setScissor(0, vks::util::rect2D(size));

        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, { 0 });

        cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
        // Left (pre compute)
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphics.pipelineLayout, 0, graphics.descriptorSetPreCompute, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics.pipeline);

        vk::Viewport viewport = vks::util::viewport((float)size.width / 2, (float)size.height, 0.0f, 1.0f);
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
        cmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), nullptr, nullptr,
                                  imageMemoryBarrier);

        // Right (post compute)
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphics.pipelineLayout, 0, graphics.descriptorSetPostCompute, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics.pipeline);

        viewport.x = viewport.width;
        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
    }

    void buildComputeCommandBuffer() {
        // FIXME find a better way to block on re-using the compute command, or build multiple command buffers
        queue.waitIdle();
        vk::CommandBufferBeginInfo cmdBufInfo;
        cmdBufInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
        compute.commandBuffer.begin(cmdBufInfo);
        compute.commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute.pipelines[compute.pipelineIndex]);
        compute.commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute.pipelineLayout, 0, compute.descriptorSet, nullptr);
        compute.commandBuffer.dispatch(textureComputeTarget.extent.width / 16, textureComputeTarget.extent.height / 16, 1);
        compute.commandBuffer.end();
    }

    // Setup vertices for a single uv-mapped quad
    void generateQuad() {
#define dim 1.0f
        std::vector<Vertex> vertexBuffer = { { { dim, dim, 0.0f }, { 1.0f, 1.0f } },
                                             { { -dim, dim, 0.0f }, { 0.0f, 1.0f } },
                                             { { -dim, -dim, 0.0f }, { 0.0f, 0.0f } },
                                             { { dim, -dim, 0.0f }, { 1.0f, 0.0f } } };
#undef dim
        meshes.quad.vertices = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0, 1, 2, 2, 3, 0 };
        meshes.quad.indexCount = (uint32_t)indexBuffer.size();
        meshes.quad.indices = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes{
            { vk::DescriptorType::eUniformBuffer, 2 },
            // Graphics pipeline uses image samplers for display
            { vk::DescriptorType::eCombinedImageSampler, 4 },
            // Compute pipeline uses a sampled image for reading
            { vk::DescriptorType::eSampledImage, 1 },
            // Compute pipelines uses a storage image to write result
            { vk::DescriptorType::eStorageImage, 1 },
        };
        descriptorPool = device.createDescriptorPool({ {}, 3, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Vertex shader uniform buffer
            { 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader image sampler
            { 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        graphics.descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        graphics.pipelineLayout = device.createPipelineLayout({ {}, 1, &graphics.descriptorSetLayout });
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, 1, &graphics.descriptorSetLayout };
        graphics.descriptorSetPostCompute = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the color map texture
        vk::DescriptorImageInfo texDescriptor{ textureComputeTarget.sampler, textureComputeTarget.view, vk::ImageLayout::eGeneral };

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            // Binding 0 : Vertex shader uniform buffer
            { graphics.descriptorSetPostCompute, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformDataVS.descriptor },
            // Binding 1 : Fragment shader texture sampler
            { graphics.descriptorSetPostCompute, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptor },
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);

        // Base image (before compute post process)
        graphics.descriptorSetPreCompute = device.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo texDescriptorBaseImage{ textureColorMap.sampler, textureColorMap.view, vk::ImageLayout::eGeneral };

        writeDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            { graphics.descriptorSetPreCompute, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformDataVS.descriptor },
            // Binding 1 : Fragment shader texture sampler
            { graphics.descriptorSetPreCompute, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptorBaseImage, &uniformDataVS.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    // Create a separate command buffer for compute commands
    void createComputeCommandBuffer() { compute.commandBuffer = device.allocateCommandBuffers({ cmdPool, vk::CommandBufferLevel::ePrimary, 1 })[0]; }

    void preparePipelines() {
        // Rendering pipeline
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, graphics.pipelineLayout, renderPass };
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelineBuilder.depthStencilState = { false };
        // Load shaders
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computeshader/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/computeshader/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);

        // Binding description
        pipelineBuilder.vertexInputState.bindingDescriptions = { { VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex } };

        // Attribute descriptions
        // Describes memory layout and shader positions
        pipelineBuilder.vertexInputState.attributeDescriptions = {
            // Location 0 : Position
            { 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, 0 },
            // Location 1 : Texture coordinates
            { 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv) },
        };

        graphics.pipeline = pipelineBuilder.create(context.pipelineCache);
    }

    void prepareCompute() {
        // Create compute pipeline
        // Compute pipelines are created separate from graphics pipelines
        // even if they use the same queue

        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Sampled image (read)
            { 0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute },
            // Binding 1 : Sampled image (write)
            { 1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute },
        };

        compute.descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        compute.pipelineLayout = device.createPipelineLayout({ {}, 1, &compute.descriptorSetLayout });

        vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, 1, &compute.descriptorSetLayout };

        compute.descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::DescriptorImageInfo> computeTexDescriptors{
            { {}, textureColorMap.view, vk::ImageLayout::eGeneral },
            { {}, textureComputeTarget.view, vk::ImageLayout::eGeneral },
        };

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets{
            // Binding 0 : Sampled image (read)
            { compute.descriptorSet, 0, 0, 1, vk::DescriptorType::eSampledImage, &computeTexDescriptors[0] },
            // Binding 1 : Sampled image (write)
            { compute.descriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage, &computeTexDescriptors[1] },
        };

        device.updateDescriptorSets(computeWriteDescriptorSets, nullptr);

        // Create compute shader pipelines
        vk::ComputePipelineCreateInfo computePipelineCreateInfo{ {}, {}, compute.pipelineLayout };
        // One pipeline for each effect
        for (auto& shaderName : shaderNames) {
            std::string fileName = getAssetPath() + "shaders/computeshader/" + shaderName + ".comp.spv";
            computePipelineCreateInfo.stage = vks::shaders::loadShader(device, fileName.c_str(), vk::ShaderStageFlagBits::eCompute);
            compute.pipelines.push_back(device.createComputePipelines(context.pipelineCache, computePipelineCreateInfo, nullptr)[0]);
        }
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformDataVS = context.createBuffer(vk::BufferUsageFlagBits::eUniformBuffer,
                                             vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, uboVS);
        uniformDataVS.map();
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader uniform buffer block
        uboVS.projection = glm::perspective(glm::radians(60.0f), (float)(size.width / 2) / size.height, 0.1f, 256.0f);
        uboVS.model = camera.matrices.view;
        uniformDataVS.copy(uboVS);
    }

    // Find and create a compute capable device queue
    void getComputeQueue() {
        uint32_t queueIndex = context.queueIndices.compute;
        assert(queueIndex != VK_QUEUE_FAMILY_IGNORED);

        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.queueFamilyIndex = queueIndex;
        queueCreateInfo.queueCount = 1;
        compute.queue = device.getQueue(queueIndex, 0);
    }

    void prepare() override {
        ExampleBase::prepare();
        loadTextures();
        generateQuad();
        getComputeQueue();
        createComputeCommandBuffer();
        prepareUniformBuffers();
        textureComputeTarget = prepareTextureTarget(vk::ImageLayout::eGeneral, textureColorMap.extent, vk::Format::eR8G8B8A8Unorm);
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        prepareCompute();
        buildCommandBuffers();
        buildComputeCommandBuffer();
        prepared = true;
    }

    void draw() override {
        ExampleBase::draw();

        // Submit compute
        vk::SubmitInfo computeSubmitInfo = vk::SubmitInfo();
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;

        compute.queue.submit(computeSubmitInfo, nullptr);
    }

    void viewChanged() override { 
        updateUniformBuffers(); 
    }

    void keyPressed(uint32_t keyCode) override {
        switch (keyCode) {
            case KEY_KPADD:
            case GAMEPAD_BUTTON_R1:
                switchComputePipeline(1);
                break;
            case KEY_KPSUB:
            case GAMEPAD_BUTTON_L1:
                switchComputePipeline(-1);
                break;
        }
    }

    void switchComputePipeline(int32_t dir) {
        if ((dir < 0) && (compute.pipelineIndex > 0)) {
            compute.pipelineIndex--;
            buildComputeCommandBuffer();
        }
        if ((dir > 0) && (compute.pipelineIndex < compute.pipelines.size() - 1)) {
            compute.pipelineIndex++;
            buildComputeCommandBuffer();
        }
    }

    void OnUpdateUIOverlay() override {
        if (ui.header("Settings")) {
            if (ui.comboBox("Shader", &compute.pipelineIndex, shaderNames)) {
                buildComputeCommandBuffer();
            }
        }
    }
};

RUN_EXAMPLE(VulkanExample)
