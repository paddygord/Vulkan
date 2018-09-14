/*
* Vulkan Example - Basic indexed triangle rendering
*
* Note :
*    This is a "pedal to the metal" example to show off how to get Vulkan up an displaying something
*    Contrary to the other examples, this one won't make use of helper functions or initializers
*    Except in a few cases (swap chain setup e.g.)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <common.hpp>
#include <vks/context.hpp>
#include <vks/swapchain.hpp>
#include <vks/shaders.hpp>
#include <utils.hpp>

#if defined(__ANDROID__)

class TriangleExample {
public:
    void run() {}
};

#else

class TriangleExample : public glfw::Window {
public:
    std::string title{ "Vulkan Example - Basic indexed triangle" };
    vk::Extent2D size{ 1280, 720 };
    vks::Context context;
    const vk::Device& device{ context.device };
    const vk::Queue& queue{ context.queue };
    vks::SwapChain swapChain;
    uint32_t currentBuffer;
    vk::CommandPool cmdPool;
    vk::DescriptorPool descriptorPool;
    vk::RenderPass renderPass;
    // List of available frame buffers (same as number of swap chain images)
    std::vector<vk::Framebuffer> framebuffers;
    std::vector<vk::CommandBuffer> commandBuffers;

    struct Vertex {
        glm::vec3 pos;
        glm::vec3 col;
    };

    // Synchronization semaphores
    struct {
        vk::Semaphore presentComplete;
        vk::Semaphore renderComplete;
    } semaphores;

    struct Buffer {
        vk::DeviceSize size;
        vk::Buffer buffer;
        vk::DeviceMemory memory;
    };

    struct UniformBuffer : public Buffer {
        vk::DescriptorBufferInfo descriptor;
    };

    UniformBuffer uniformDataVS;

    struct {
        glm::mat4 projectionMatrix;
        glm::mat4 modelMatrix;
        glm::mat4 viewMatrix;
    } uboVS;


    Buffer vertices;
    Buffer indices;

    int indexCount;
    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    void onWindowResized(const glm::uvec2& newSize) override {
        queue.waitIdle();
        device.waitIdle();
        size.width = newSize.x;
        size.height = newSize.y;
        swapChain.create(size);
        setupFrameBuffer();
        buildDrawCommandBuffers();
    }

    void run() {
        prepare();
        runWindowLoop([&] { draw(); });
        queue.waitIdle();
        device.waitIdle();
        destroy();
    }

    void prepare() {
        glfw::Window::init();
        // We don't want OpenGL
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        createWindow({ size.width, size.height }, { 100, 100 });

        context.setValidationEnabled(true);
        context.requireExtensions(glfw::Window::getRequiredInstanceExtensions());
        context.requireDeviceExtensions({ VK_KHR_SWAPCHAIN_EXTENSION_NAME });
        context.createInstance();

        // The `surface` should be created before the Vulkan `device` because the device selection needs to pick a queue
        // that will support presentation to the surface
        auto surface = createSurface(context.instance);

        context.createDevice(surface);

        cmdPool = context.getCommandPool();

        swapChain.setup(context.physicalDevice, context.device, context.queue, context.queueIndices.graphics);
        swapChain.setSurface(surface);
        swapChain.create(size);

        setupRenderPass();
        setupFrameBuffer();

        prepareSemaphore();
        prepareVertices();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildDrawCommandBuffers();
    }

    void destroy() {
        // Clean up used Vulkan resources
        device.destroy(pipeline);
        device.destroy(pipelineLayout);
        device.destroy(descriptorSetLayout);

        device.destroySemaphore(semaphores.presentComplete);
        device.destroySemaphore(semaphores.renderComplete);

        destroyBuffer(vertices);
        destroyBuffer(indices);
        destroyBuffer(uniformDataVS);

        device.destroyRenderPass(renderPass);
        device.destroyDescriptorPool(descriptorPool);

        for (const auto& framebuffer : framebuffers) {
            device.destroyFramebuffer(framebuffer);
        }
        for (const auto& image : swapChain.images) {
            if (image.fence) {
                device.destroyFence(image.fence);
            }
        }
        swapChain.destroy();
        context.destroy();
    }

    void setupRenderPass() {
        if (renderPass) {
            device.destroyRenderPass(renderPass);
        }

        std::array<vk::AttachmentDescription, 1> attachments;
        std::array<vk::AttachmentReference, 1> attachmentReferences;

        // Color attachment
        attachments[0].format = swapChain.colorFormat;
        attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
        attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
        attachments[0].initialLayout = vk::ImageLayout::eUndefined;
        attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

        // Only one depth attachment, so put it first in the references
        vk::AttachmentReference& colorReference = attachmentReferences[0];
        colorReference.attachment = 0;
        colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

        std::array<vk::SubpassDescription, 1> subpasses;
        {
            vk::SubpassDescription& subpass = subpasses[0];
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = attachmentReferences.data();
        }

        std::array<vk::SubpassDependency, 1> subpassDependencies;
        {
            vk::SubpassDependency& dependency = subpassDependencies[0];
            dependency.srcSubpass = 0;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;

            dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        }

        vk::RenderPassCreateInfo renderPassInfo;
        renderPassInfo.attachmentCount = (uint32_t)attachments.size();
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = (uint32_t)subpasses.size();
        renderPassInfo.pSubpasses = subpasses.data();
        renderPassInfo.dependencyCount = (uint32_t)subpassDependencies.size();
        renderPassInfo.pDependencies = subpassDependencies.data();
        renderPass = device.createRenderPass(renderPassInfo);
    }

    void setupFrameBuffer() {
        if (!framebuffers.empty()) {
            for (const auto& framebuffer : framebuffers) {
                device.destroyFramebuffer(framebuffer);
            }
            framebuffers.clear();
        }

        std::array<vk::ImageView, 1> attachments;
        vk::FramebufferCreateInfo framebufferCreateInfo;
        framebufferCreateInfo.renderPass = renderPass;
        framebufferCreateInfo.attachmentCount = (uint32_t)attachments.size();
        framebufferCreateInfo.pAttachments = attachments.data();
        framebufferCreateInfo.width = size.width;
        framebufferCreateInfo.height = size.height;
        framebufferCreateInfo.layers = 1;

        // Create frame buffers for every swap chain image
        framebuffers = swapChain.createFramebuffers(framebufferCreateInfo);
    }

    void prepareSemaphore() {
        vk::SemaphoreCreateInfo semaphoreCreateInfo;

        // This semaphore ensures that the image is complete
        // before starting to submit again
        semaphores.presentComplete = device.createSemaphore(semaphoreCreateInfo);

        // This semaphore ensures that all commands submitted
        // have been finished before submitting the image to the queue
        semaphores.renderComplete = device.createSemaphore(semaphoreCreateInfo);
    }

    void withCommandBuffer(const std::function<void(const vk::CommandBuffer&)> f) {
        vk::CommandBufferAllocateInfo cmdBufInfo;
        cmdBufInfo.commandPool = cmdPool;
        cmdBufInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdBufInfo.commandBufferCount = 1;

        vk::CommandBuffer commandBuffer = device.allocateCommandBuffers(cmdBufInfo)[0];
        commandBuffer.begin(vk::CommandBufferBeginInfo{});
        f(commandBuffer);
        commandBuffer.end();

        // Submit copies to the queue
        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        queue.submit(submitInfo, {});
        queue.waitIdle();
        device.freeCommandBuffers(cmdPool, commandBuffer);
    }

    void destroyBuffer(Buffer& buffer) {
        device.destroy(buffer.buffer);
        device.freeMemory(buffer.memory);
        buffer = {};
    }

    Buffer createBuffer(size_t size,
                        const vk::BufferUsageFlags& usageFlags,
                        const vk::MemoryPropertyFlags& memoryFlags = vk::MemoryPropertyFlagBits::eDeviceLocal) {
        Buffer result;

        // Create the buffer handle.  It has no backing storage
        result.buffer = device.createBuffer({ {}, size, usageFlags });

        // Allocate memory
        const auto memReqs = device.getBufferMemoryRequirements(result.buffer);
        result.size = memReqs.size;
        auto memoryType = context.getMemoryType(memReqs.memoryTypeBits, memoryFlags);
        result.memory = device.allocateMemory({ memReqs.size, memoryType });
        device.bindBufferMemory(result.buffer, result.memory, 0);
        return result;
    }

    Buffer createStagingBuffer(size_t size, const void* srcData) {
        auto result = createBuffer(size, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);

        // Map and copy
        auto dstData = device.mapMemory(result.memory, 0, result.size, {});
        // We need to make sure we use the input size for copying from the src data
        // because the actual buffer may be larger than the input size
        memcpy(dstData, srcData, size);
        device.unmapMemory(result.memory);

        return result;
    }

    template <typename T>
    Buffer createStagingBuffer(const std::vector<T>& v) {
        return createStagingBuffer(v.size() * sizeof(T), v.data());
    }

    void prepareVertices() {
        // Setup vertices
        std::vector<Vertex> vertexData = { { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
                                           { { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                                           { { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
        size_t vertexDataSize = sizeof(Vertex) * vertexData.size();
        // Setup indices
        std::vector<uint32_t> indexData = { 0, 1, 2 };
        indexCount = (uint32_t)indexData.size();
        size_t indexDataSize = sizeof(uint32_t) * indexData.size();

        // Static data like vertex and index buffer should be stored on the device memory
        // for optimal (and fastest) access by the GPU
        //
        // To achieve this we use so-called "staging buffers" :
        // - Create a buffer that's visible to the host (and can be mapped)
        // - Copy the data to this buffer
        // - Create another buffer that's local on the device (VRAM) with the same size
        // - Copy the data from the host to the device using a command buffer
        // - Delete the host visible (staging) buffer
        // - Use the device local buffers for rendering

        // Vertex buffer

        // Create a host visible buffer that contains the passed data
        auto verticesStagingBuffer = createStagingBuffer(vertexData);
        // Creates a device local buffer, but does not populate it
        vertices = createBuffer(vertexDataSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);

        // Index buffer
        auto indicesStagingBuffer = createStagingBuffer(indexData);
        indices = createBuffer(indexDataSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst);

        // Execute the copy from the host visible buffers to the device local buffers
        // Note that the staging buffer must not be deleted before the copies
        // have been submitted and executed
        withCommandBuffer([&](const vk::CommandBuffer& copyCommandBuffer) {
            // Vertex buffer
            copyCommandBuffer.copyBuffer(verticesStagingBuffer.buffer, vertices.buffer, vk::BufferCopy{ 0, 0, vertexDataSize } );
            // Index buffer
            copyCommandBuffer.copyBuffer(indicesStagingBuffer.buffer, indices.buffer, vk::BufferCopy{ 0, 0, indexDataSize } );
        });

        // Destroy staging buffers
        destroyBuffer(verticesStagingBuffer);
        destroyBuffer(indicesStagingBuffer);
    }

    void prepareUniformBuffers() {
        // Prepare and initialize a uniform buffer block containing shader uniforms
        // In Vulkan there are no more single uniforms like in GL
        // All shader uniforms are passed as uniform buffer blocks

        // Create the uniform buffer
        (Buffer&)uniformDataVS = createBuffer(sizeof(uboVS), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);

        // Store information in the uniform's descriptor
        uniformDataVS.descriptor.buffer = uniformDataVS.buffer;
        uniformDataVS.descriptor.offset = 0;
        uniformDataVS.descriptor.range = sizeof(uboVS);

        // Update matrices
        uboVS.projectionMatrix = glm::perspective(glm::radians(60.0f), (float)size.width / (float)size.height, 0.1f, 256.0f);
        uboVS.viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, -2.5f));
        uboVS.modelMatrix = glm::mat4();

        // Map uniform buffer and update it
        // If you want to keep a handle to the memory and not unmap it after updating,
        // create the memory with the vk::MemoryPropertyFlagBits::eHostCoherent
        void* pData = device.mapMemory(uniformDataVS.memory, 0, sizeof(uboVS), {});
        memcpy(pData, &uboVS, sizeof(uboVS));
        device.unmapMemory(uniformDataVS.memory);
    }

    void setupDescriptorPool() {
        // We need to tell the API the number of max. requested descriptors per type
        vk::DescriptorPoolSize typeCounts[1];
        // This example only uses one descriptor type (uniform buffer) and only
        // requests one descriptor of this type
        typeCounts[0].type = vk::DescriptorType::eUniformBuffer;
        typeCounts[0].descriptorCount = 1;
        // For additional types you need to add new entries in the type count list
        // E.g. for two combined image samplers :
        // typeCounts[1].type = vk::DescriptorType::eCombinedImageSampler;
        // typeCounts[1].descriptorCount = 2;

        // Create the global descriptor pool
        // All descriptors used in this example are allocated from this pool
        vk::DescriptorPoolCreateInfo descriptorPoolInfo;
        descriptorPoolInfo.poolSizeCount = 1;
        descriptorPoolInfo.pPoolSizes = typeCounts;
        // Set the max. number of sets that can be requested
        // Requesting descriptors beyond maxSets will result in an error
        descriptorPoolInfo.maxSets = 1;

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        // Setup layout of descriptors used in this example
        // Basically connects the different shader stages to descriptors
        // for binding uniform buffers, image samplers, etc.
        // So every shader binding should map to one descriptor set layout
        // binding

        // Binding 0 : Uniform buffer (Vertex shader)
        vk::DescriptorSetLayoutBinding layoutBinding;
        layoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;
        layoutBinding.pImmutableSamplers = NULL;

        vk::DescriptorSetLayoutCreateInfo descriptorLayout;
        descriptorLayout.bindingCount = 1;
        descriptorLayout.pBindings = &layoutBinding;

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout, nullptr);

        // Create the pipeline layout that is used to generate the rendering pipelines that
        // are based on this descriptor set layout
        // In a more complex scenario you would have different pipeline layouts for different
        // descriptor set layouts that could be reused
        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo;
        pPipelineLayoutCreateInfo.setLayoutCount = 1;
        pPipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;

        pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);
    }

    void setupDescriptorSet() {
        // Allocate a new descriptor set from the global descriptor pool
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        // Update the descriptor set determining the shader binding points
        // For every binding point used in a shader there needs to be one
        // descriptor set matching that binding point

        vk::WriteDescriptorSet writeDescriptorSet;

        // Binding 0 : Uniform buffer
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = vk::DescriptorType::eUniformBuffer;
        writeDescriptorSet.pBufferInfo = &uniformDataVS.descriptor;
        // Binds this uniform buffer to binding point 0
        writeDescriptorSet.dstBinding = 0;

        device.updateDescriptorSets(writeDescriptorSet, nullptr);
    }

    void preparePipelines() {
        // Create our rendering pipeline used in this example
        // Vulkan uses the concept of rendering pipelines to encapsulate
        // fixed states
        // This replaces OpenGL's huge (and cumbersome) state machine
        // A pipeline is then stored and hashed on the GPU making
        // pipeline changes much faster than having to set dozens of
        // states
        // In a real world application you'd have dozens of pipelines
        // for every shader set used in a scene
        // Note that there are a few states that are not stored with
        // the pipeline. These are called dynamic states and the
        // pipeline only stores that they are used with this pipeline,
        // but not their states

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
        // The layout used for this pipeline
        pipelineCreateInfo.layout = pipelineLayout;
        // Renderpass this pipeline is attached to
        pipelineCreateInfo.renderPass = renderPass;

        // Vertex input state
        // Describes the topology used with this pipeline
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
        // This pipeline renders vertex data as triangle lists
        inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;

        // Rasterization state
        vk::PipelineRasterizationStateCreateInfo rasterizationState;
        // Solid polygon mode
        rasterizationState.polygonMode = vk::PolygonMode::eFill;
        // No culling
        rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        rasterizationState.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizationState.depthClampEnable = VK_FALSE;
        rasterizationState.rasterizerDiscardEnable = VK_FALSE;
        rasterizationState.depthBiasEnable = VK_FALSE;
        rasterizationState.lineWidth = 1.0f;

        // Color blend state
        // Describes blend modes and color masks
        vk::PipelineColorBlendStateCreateInfo colorBlendState;
        // One blend attachment state
        // Blending is not used in this example
        vk::PipelineColorBlendAttachmentState blendAttachmentState;
        blendAttachmentState.colorWriteMask = static_cast<vk::ColorComponentFlagBits>(0xf);
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = &blendAttachmentState;

        // vk::Viewport state
        vk::PipelineViewportStateCreateInfo viewportState;
        // One viewport
        viewportState.viewportCount = 1;
        // One scissor rectangle
        viewportState.scissorCount = 1;

        // Enable dynamic states
        // Describes the dynamic states to be used with this pipeline
        // Dynamic states can be set even after the pipeline has been created
        // So there is no need to create new pipelines just for changing
        // a viewport's dimensions or a scissor box
        vk::PipelineDynamicStateCreateInfo dynamicState;
        // The dynamic state properties themselves are stored in the command buffer
        std::vector<vk::DynamicState> dynamicStateEnables;
        dynamicStateEnables.push_back(vk::DynamicState::eViewport);
        dynamicStateEnables.push_back(vk::DynamicState::eScissor);
        dynamicState.dynamicStateCount = (uint32_t)dynamicStateEnables.size();
        dynamicState.pDynamicStates = dynamicStateEnables.data();

        // Depth and stencil state
        // Describes depth and stencil test and compare ops
        vk::PipelineDepthStencilStateCreateInfo depthStencilState;
        // No depth or stencil testing enabled
        depthStencilState.depthTestEnable = VK_FALSE;
        depthStencilState.depthWriteEnable = VK_FALSE;
        depthStencilState.stencilTestEnable = VK_FALSE;

        // Multi sampling state
        vk::PipelineMultisampleStateCreateInfo multisampleState;
        multisampleState.pSampleMask = NULL;
        // No multi sampling used in this example
        multisampleState.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Load shaders
        // Shaders are loaded from the SPIR-V format, which can be generated from glsl
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
        shaderStages[0] = vks::shaders::loadShader(device, vkx::getAssetPath() + "shaders/triangle/triangle.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = vks::shaders::loadShader(device, vkx::getAssetPath() + "shaders/triangle/triangle.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

        // Binding description
        bindingDescriptions = {
            { 0, sizeof(Vertex) },
        };

        // Attribute descriptions
        // Describes memory layout and shader attribute locations
        attributeDescriptions = {
            // Location 0 : Position
            { 0, 0, vk::Format::eR32G32B32Sfloat, 0 },
            // Location 1 : Color
            { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, col) },
        };

        // Assign to vertex input state
        inputState.vertexBindingDescriptionCount = (uint32_t)bindingDescriptions.size();
        inputState.pVertexBindingDescriptions = bindingDescriptions.data();
        inputState.vertexAttributeDescriptionCount = (uint32_t)attributeDescriptions.size();
        inputState.pVertexAttributeDescriptions = attributeDescriptions.data();

        // Assign states
        // Assign pipeline state create information
        pipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.pVertexInputState = &inputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.renderPass = renderPass;
        pipelineCreateInfo.pDynamicState = &dynamicState;

        // Create rendering pipeline
        pipeline = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];

        for (const auto& shaderStage : shaderStages) {
            device.destroyShaderModule(shaderStage.module);
        }
    }

    void buildDrawCommandBuffers() {
        // Create one command buffer per image in the swap chain

        // Command buffers store a reference to the
        // frame buffer inside their render pass info
        // so for static usage without having to rebuild
        // them each frame, we use one per frame buffer
        vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
        cmdBufAllocateInfo.commandPool = cmdPool;
        cmdBufAllocateInfo.commandBufferCount = swapChain.imageCount;
        commandBuffers = device.allocateCommandBuffers(cmdBufAllocateInfo);

        vk::CommandBufferBeginInfo cmdBufInfo;
        vk::ClearValue clearValues[2];
        clearValues[0].color = vks::util::clearColor(glm::vec4({ 0.025f, 0.025f, 0.025f, 1.0f }));
        ;

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.extent = size;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = clearValues;
        glm::vec2 offset;
        float minDepth = 0;
        float maxDepth = 1;
        vk::Viewport viewport = vk::Viewport{ offset.x, offset.y, (float)size.width, (float)size.height, minDepth, maxDepth };
        vk::Rect2D scissor = vk::Rect2D{ vk::Offset2D(), size };
        vk::DeviceSize offsets = 0;
        for (size_t i = 0; i < swapChain.imageCount; ++i) {
            const auto& cmdBuffer = commandBuffers[i];
            cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
            cmdBuffer.begin(cmdBufInfo);
            renderPassBeginInfo.framebuffer = framebuffers[i];
            cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            // Update dynamic viewport state
            cmdBuffer.setViewport(0, viewport);
            // Update dynamic scissor state
            cmdBuffer.setScissor(0, scissor);
            // Bind descriptor sets describing shader binding points
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
            // Bind the rendering pipeline (including the shaders)
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            // Bind triangle vertices
            cmdBuffer.bindVertexBuffers(0, vertices.buffer, offsets);
            // Bind triangle indices
            cmdBuffer.bindIndexBuffer(indices.buffer, 0, vk::IndexType::eUint32);
            // Draw indexed triangle
            cmdBuffer.drawIndexed(indexCount, 1, 0, 0, 1);
            cmdBuffer.endRenderPass();
            cmdBuffer.end();
        }
    }

    void draw() {
        // Get next image in the swap chain (back/front buffer)
        currentBuffer = swapChain.acquireNextImage(semaphores.presentComplete).value;

        // The SubmitInfo structure contains a list of
        // command buffers and semaphores to be submitted to a queue
        // If you want to submit multiple command buffers, pass an array
        vk::PipelineStageFlags pipelineStages = vk::PipelineStageFlagBits::eBottomOfPipe;
        vk::SubmitInfo submitInfo;
        submitInfo.pWaitDstStageMask = &pipelineStages;
        // The wait semaphore ensures that the image is presented
        // before we start submitting command buffers again
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &semaphores.presentComplete;
        // Submit the currently active command buffer
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentBuffer];
        // The signal semaphore is used during queue presentation
        // to ensure that the image is not rendered before all
        // commands have been submitted
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &semaphores.renderComplete;

        // Submit to the graphics queue
        // TODO explain submit fence
        queue.submit(submitInfo, swapChain.getSubmitFence(true));

        // Present the current buffer to the swap chain
        // We pass the signal semaphore from the submit info
        // to ensure that the image is not rendered until
        // all commands have been submitted
        swapChain.queuePresent(semaphores.renderComplete);
    }
};
#endif

RUN_EXAMPLE(TriangleExample)
