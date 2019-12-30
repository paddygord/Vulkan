/*
* Vulkan Example - Basic indexed triangle rendering
*
* Note:
*	This is a "pedal to the metal" example to show off how to get Vulkan up an displaying something
*	Contrary to the other examples, this one won't make use of helper functions or initializers
*	Except in a few cases (swap chain setup e.g.)
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vkx/vulkanExampleBase.hpp>

// Set to "true" to use staging buffers for uploading vertex and index data to device local memory
// See "prepareVertices" for details on what's staging and on why to use it
#define USE_STAGING true

class VulkanExample : public vkx::ExampleBase {
    using Parent = vkx::ExampleBase;

public:
    // Vertex layout used in this example
    struct Vertex {
        float position[3];
        float color[3];
    };

    // Vertex buffer and attributes
    struct {
        vk::DeviceMemory memory;  // Handle to the device memory for this buffer
        vk::Buffer buffer;        // Handle to the Vulkan buffer object that the memory is bound to
    } vertices;

    // Index buffer
    struct {
        vk::DeviceMemory memory;
        vk::Buffer buffer;
        uint32_t count;
    } indices;

    // Uniform buffer block object
    struct {
        vk::DeviceMemory memory;
        vk::Buffer buffer;
        vk::DescriptorBufferInfo descriptor;
    } uniformBufferVS;

    // For simplicity we use the same uniform block layout as in the shader:
    //
    //	layout(set = 0, binding = 0) uniform UBO
    //	{
    //		mat4 projectionMatrix;
    //		mat4 modelMatrix;
    //		mat4 viewMatrix;
    //	} ubo;
    //
    // This way we can just memcopy the ubo data to the ubo
    // Note: You should use data types that align with the GPU in order to avoid manual padding (vec4, mat4)
    struct {
        glm::mat4 projectionMatrix;
        glm::mat4 modelMatrix;
        glm::mat4 viewMatrix;
    } uboVS;

    // The pipeline layout is used by a pipline to access the descriptor sets
    // It defines interface (without binding any actual data) between the shader stages used by the pipeline and the shader resources
    // A pipeline layout can be shared among multiple pipelines as long as their interfaces match
    vk::PipelineLayout pipelineLayout;

    // Pipelines (often called "pipeline state objects") are used to bake all states that affect a pipeline
    // While in OpenGL every state can be changed at (almost) any time, Vulkan requires to layout the graphics (and compute) pipeline states upfront
    // So for each combination of non-dynamic pipeline states you need a new pipeline (there are a few exceptions to this not discussed here)
    // Even though this adds a new dimension of planing ahead, it's a great opportunity for performance optimizations by the driver
    vk::Pipeline pipeline;

    // The descriptor set layout describes the shader binding layout (without actually referencing descriptor)
    // Like the pipeline layout it's pretty much a blueprint and can be used with different descriptor sets as long as their layout matches
    vk::DescriptorSetLayout descriptorSetLayout;

    // The descriptor set stores the resources bound to the binding points in a shader
    // It connects the binding points of the different shaders with the buffers and images used for those bindings
    vk::DescriptorSet descriptorSet;

    // Synchronization primitives
    // Synchronization is an important concept of Vulkan that OpenGL mostly hid away. Getting this right is crucial to using Vulkan.

    // Semaphores
    // Used to coordinate operations within the graphics queue and ensure correct command ordering
    vk::Semaphore presentCompleteSemaphore;
    vk::Semaphore renderCompleteSemaphore;

    // Fences
    // Used to check the completion of queue operations (e.g. command buffer execution)
    std::vector<vk::Fence> waitFences;

    VulkanExample() {
        zoom = -2.5f;
        title = "Vulkan Example - Basic indexed triangle";
        // Values not set here are initialized in the base class constructor
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note: Inherited destructor cleans up resources stored in base class
        device.destroy(pipeline);
        device.destroy(pipelineLayout);
        device.destroy(descriptorSetLayout);

        device.destroy(vertices.buffer);
        device.freeMemory(vertices.memory);

        device.destroy(indices.buffer);
        device.freeMemory(indices.memory);

        device.destroy(uniformBufferVS.buffer);
        device.freeMemory(uniformBufferVS.memory);

        device.destroy(presentCompleteSemaphore);
        device.destroy(renderCompleteSemaphore);

        for (auto& fence : waitFences) {
            device.destroy(fence);
        }
    }

    // This function is used to request a device memory type that supports all the property flags we request (e.g. device local, host visibile)
    // Upon success it will return the index of the memory type that fits our requestes memory properties
    // This is necessary as implementations can offer an arbitrary number of memory types with different
    // memory properties.
    // You can check http://vulkan.gpuinfo.org/ for details on different memory configurations
    uint32_t getMemoryTypeIndex(uint32_t typeBits, vk::MemoryPropertyFlags properties) {
        // Iterate over all memory types available for the device used in this example
        for (uint32_t i = 0; i < context.deviceMemoryProperties.memoryTypeCount; i++) {
            if ((typeBits & 1) == 1) {
                if ((context.deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            typeBits >>= 1;
        }

        throw "Could not find a suitable memory type!";
    }

    // Create the Vulkan synchronization primitives used in this example
    void prepareSynchronizationPrimitives() {
        // Semaphores (Used for correct command ordering)

        // Semaphore used to ensures that image presentation is complete before starting to submit again
        presentCompleteSemaphore = device.createSemaphore({});

        // Semaphore used to ensures that all commands submitted have been finished before submitting the image to the queue
        renderCompleteSemaphore = device.createSemaphore({});

        // Fences (Used to check draw command buffer completion)
        waitFences.resize(swapChain.imageCount);
        for (auto& fence : waitFences) {
            // Create in signaled state so we don't wait on first render of each command buffer
            fence = device.createFence({ vk::FenceCreateFlagBits::eSignaled });
        }
    }

    void withCommanbBuffer(const std::function<void(const vk::CommandBuffer&)>& f) {
        auto cmdBuffer = getCommandBuffer(true);
        f(cmdBuffer);
        flushCommandBuffer(cmdBuffer);
    }

    // Get a new command buffer from the command pool
    // If begin is true, the command buffer is also started so we can start adding commands
    vk::CommandBuffer getCommandBuffer(bool begin) {
        vk::CommandBuffer cmdBuffer = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ cmdPool, vk::CommandBufferLevel::ePrimary, 1 })[0];

        // If requested, also start the new command buffer
        if (begin) {
            cmdBuffer.begin(vk::CommandBufferBeginInfo{});
        }

        return cmdBuffer;
    }

    const uint64_t DEFAULT_FENCE_TIMEOUT{ UINT64_MAX };

    // End the command buffer and submit it to the queue
    // Uses a fence to ensure command buffer has finished executing before deleting it
    void flushCommandBuffer(const vk::CommandBuffer& commandBuffer) {
        assert(commandBuffer);
        commandBuffer.end();

        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // Create fence to ensure that the command buffer has finished executing
        vk::Fence fence = device.createFence({});

        // Submit to the queue
        queue.submit(submitInfo, fence);
        // Wait for the fence to signal that command buffer has finished executing
        device.waitForFences(fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT);
        device.destroy(fence);
        device.freeCommandBuffers(cmdPool, commandBuffer);
    }

    // Build separate command buffers for every framebuffer image
    // Unlike in OpenGL all rendering commands are recorded once into command buffers that are then resubmitted to the queue
    // This allows to generate work upfront and from multiple threads, one of the biggest advantages of Vulkan
    void updateDrawCommandBuffer(const vk::CommandBuffer& drawCmdBuffer) {
        // Update dynamic viewport state
        drawCmdBuffer.setViewport(0, vk::Viewport{ 0, 0, (float)width, (float)height, 0, 1 });
        // Update dynamic scissor state
        drawCmdBuffer.setScissor(0, vk::Rect2D{ vk::Offset2D{}, vk::Extent2D{ width, height } });
        // Bind descriptor sets describing shader binding points
        drawCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        // Bind the rendering pipeline
        // The pipeline (state object) contains all states of the rendering pipeline, binding it will set all the states specified at pipeline creation time
        drawCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        // Bind triangle vertex buffer (contains position and colors)
        drawCmdBuffer.bindVertexBuffers(0, vertices.buffer, { 0 });

        // Bind triangle index buffer
        drawCmdBuffer.bindIndexBuffer(indices.buffer, 0, vk::IndexType::eUint32);

        // Draw indexed triangle
        drawCmdBuffer.drawIndexed(indices.count, 1, 0, 0, 1);
    }

    void draw() {
        // Get next image in the swap chain (back/front buffer)
        currentBuffer = swapChain.acquireNextImage(presentCompleteSemaphore).value;
        const auto& fence = waitFences[currentBuffer];

        // Use a fence to wait until the command buffer has finished execution before using it again
        device.waitForFences(fence, VK_TRUE, UINT64_MAX);
        device.resetFences(fence);

        // Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
        vk::PipelineStageFlags waitStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        // The submit info structure specifices a command buffer queue submission batch
        vk::SubmitInfo submitInfo;
        submitInfo.pWaitDstStageMask = &waitStageMask;                // Pointer to the list of pipeline stages that the semaphore waits will occur at
        submitInfo.pWaitSemaphores = &presentCompleteSemaphore;       // Semaphore(s) to wait upon before the submitted command buffer starts executing
        submitInfo.waitSemaphoreCount = 1;                            // One wait semaphore
        submitInfo.pSignalSemaphores = &renderCompleteSemaphore;      // Semaphore(s) to be signaled when command buffers have completed
        submitInfo.signalSemaphoreCount = 1;                          // One signal semaphore
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];  // Command buffers(s) to execute in this batch (submission)
        submitInfo.commandBufferCount = 1;                            // One command buffer

        // Submit to the graphics queue passing a wait fence
        queue.submit(submitInfo, fence);

        // Present the current buffer to the swap chain
        // Pass the semaphore signaled by the command buffer submission from the submit info as the wait semaphore for swap chain presentation
        // This ensures that the image is not presented to the windowing system until all commands have been submitted
        swapChain.queuePresent(renderCompleteSemaphore);
    }

    // Prepare vertex and index buffers for an indexed triangle
    // Also uploads them to device local memory using staging and initializes vertex input and attribute binding to match the vertex shader
    void prepareVertices(bool useStagingBuffers) {
        constexpr uint32_t VERTEX_COUNT{ 3 };
        // A note on memory management in Vulkan in general:
        //	This is a very complex topic and while it's fine for an example application to to small individual memory allocations that is not
        //	what should be done a real-world application, where you should allocate large chunkgs of memory at once isntead.

        // Setup vertices
        std::array<Vertex, VERTEX_COUNT> vertexBuffer{ {
            {
                { 1.0f, 1.0f, 0.0f },
                { 1.0f, 0.0f, 0.0f },
            },
            {
                { -1.0f, 1.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
            },
            {
                { 0.0f, -1.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f },
            },
        } };

        constexpr uint32_t vertexBufferSize = VERTEX_COUNT * sizeof(Vertex);

        // Setup indices
        std::array<uint32_t, VERTEX_COUNT> indexBuffer = { 0, 1, 2 };
        indices.count = VERTEX_COUNT;
        constexpr uint32_t indexBufferSize = VERTEX_COUNT * sizeof(uint32_t);

        vk::MemoryAllocateInfo memAlloc;

        vk::MemoryRequirements memReqs;

        void* data;

        if (useStagingBuffers) {
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
            const auto stagingMemoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

            struct StagingBuffer {
                vk::DeviceMemory memory;
                vk::Buffer buffer;
            };

            struct {
                StagingBuffer vertices;
                StagingBuffer indices;
            } stagingBuffers;

            // Vertex buffer
            vk::BufferCreateInfo vertexBufferInfo;
            vertexBufferInfo.size = vertexBufferSize;
            // Buffer is used as the copy source
            vertexBufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
            // Create a host-visible buffer to copy the vertex data to (staging buffer)
            stagingBuffers.vertices.buffer = device.createBuffer(vertexBufferInfo);
            memReqs = device.getBufferMemoryRequirements(stagingBuffers.vertices.buffer);

            memAlloc.allocationSize = memReqs.size;
            // Request a host visible memory type that can be used to copy our data do
            // Also request it to be coherent, so that writes are visible to the GPU right after unmapping the buffer
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, stagingMemoryPropertyFlags);
            stagingBuffers.vertices.memory = device.allocateMemory(memAlloc);
            // Map and copy
            data = device.mapMemory(stagingBuffers.vertices.memory, 0, VK_WHOLE_SIZE);
            memcpy(data, vertexBuffer.data(), vertexBufferSize);
            device.unmapMemory(stagingBuffers.vertices.memory);
            device.bindBufferMemory(stagingBuffers.vertices.buffer, stagingBuffers.vertices.memory, 0);

            // Create a device local buffer to which the (host local) vertex data will be copied and which will be used for rendering
            vertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            vertices.buffer = device.createBuffer(vertexBufferInfo);
            memReqs = device.getBufferMemoryRequirements(vertices.buffer);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            vertices.memory = device.allocateMemory(memAlloc);
            device.bindBufferMemory(vertices.buffer, vertices.memory, 0);

            // Index buffer
            vk::BufferCreateInfo indexbufferInfo;
            indexbufferInfo.size = indexBufferSize;
            indexbufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
            // Copy index data to a buffer visible to the host (staging buffer)
            stagingBuffers.indices.buffer = device.createBuffer(indexbufferInfo);
            memReqs = device.getBufferMemoryRequirements(stagingBuffers.indices.buffer);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, stagingMemoryPropertyFlags);
            stagingBuffers.indices.memory = device.allocateMemory(memAlloc);
            data = device.mapMemory(stagingBuffers.indices.memory, 0, VK_WHOLE_SIZE);
            memcpy(data, indexBuffer.data(), indexBufferSize);
            device.unmapMemory(stagingBuffers.indices.memory);
            device.bindBufferMemory(stagingBuffers.indices.buffer, stagingBuffers.indices.memory, 0);

            // Create destination buffer with device only visibility
            indexbufferInfo.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            indices.buffer = device.createBuffer(indexbufferInfo);
            memReqs = device.getBufferMemoryRequirements(indices.buffer);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            indices.memory = device.allocateMemory(memAlloc);
            device.bindBufferMemory(indices.buffer, indices.memory, 0);

            // Buffer copies have to be submitted to a queue, so we need a command buffer for them
            // Note: Some devices offer a dedicated transfer queue (with only the transfer bit set) that may be faster when doing lots of copies
            withCommanbBuffer([&](const vk::CommandBuffer& copyCmd) {
                // Put buffer region copies into command buffer
                vk::BufferCopy copyRegion;
                // Vertex buffer
                copyRegion.size = vertexBufferSize;
                copyCmd.copyBuffer(stagingBuffers.vertices.buffer, vertices.buffer, copyRegion);
                // Index buffer
                copyRegion.size = indexBufferSize;
                copyCmd.copyBuffer(stagingBuffers.indices.buffer, indices.buffer, copyRegion);
            });
            // Flushing the command buffer will also submit it to the queue and uses a fence to ensure that all commands have been executed before returning

            // Destroy staging buffers
            // Note: Staging buffer must not be deleted before the copies have been submitted and executed
            device.destroy(stagingBuffers.vertices.buffer);
            device.freeMemory(stagingBuffers.vertices.memory);
            device.destroy(stagingBuffers.indices.buffer);
            device.freeMemory(stagingBuffers.indices.memory);
        } else {
            const auto memoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            // Don't use staging
            // Create host-visible buffers only and use these for rendering. This is not advised and will usually result in lower rendering performance

            // Vertex buffer
            vk::BufferCreateInfo vertexBufferInfo;
            vertexBufferInfo.size = vertexBufferSize;
            vertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;

            // Copy vertex data to a buffer visible to the host
            vertices.buffer = device.createBuffer(vertexBufferInfo);
            memReqs = device.getBufferMemoryRequirements(vertices.buffer);
            memAlloc.allocationSize = memReqs.size;
            // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT is host visible memory, and VK_MEMORY_PROPERTY_HOST_COHERENT_BIT makes sure writes are directly visible
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, memoryPropertyFlags);
            vertices.memory = device.allocateMemory(memAlloc);
            data = device.mapMemory(vertices.memory, 0, VK_WHOLE_SIZE);
            memcpy(data, vertexBuffer.data(), vertexBufferSize);
            device.unmapMemory(vertices.memory);
            device.bindBufferMemory(vertices.buffer, vertices.memory, 0);

            // Index buffer
            vk::BufferCreateInfo indexbufferInfo;
            indexbufferInfo.size = indexBufferSize;
            indexbufferInfo.usage = vk::BufferUsageFlagBits::eIndexBuffer;

            // Copy index data to a buffer visible to the host
            indices.buffer = device.createBuffer(indexbufferInfo);
            memReqs = device.getBufferMemoryRequirements(indices.buffer);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, memoryPropertyFlags);
            indices.memory = device.allocateMemory(memAlloc);
            data = device.mapMemory(indices.memory, 0, VK_WHOLE_SIZE);
            memcpy(data, indexBuffer.data(), indexBufferSize);
            device.unmapMemory(indices.memory);
            device.bindBufferMemory(indices.buffer, indices.memory, 0);
        }
    }

    void setupDescriptorPool() {
        // We need to tell the API the number of max. requested descriptors per type
        vk::DescriptorPoolSize typeCounts[1];
        // This example only uses one descriptor type (uniform buffer) and only requests one descriptor of this type
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
        // Set the max. number of descriptor sets that can be requested from this pool (requesting beyond this limit will result in an error)
        descriptorPoolInfo.maxSets = 1;

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        // Setup layout of descriptors used in this example
        // Basically connects the different shader stages to descriptors for binding uniform buffers, image samplers, etc.
        // So every shader binding should map to one descriptor set layout binding

        // Binding 0: Uniform buffer (Vertex shader)
        vk::DescriptorSetLayoutBinding layoutBinding;
        layoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutCreateInfo descriptorLayout = {};
        descriptorLayout.bindingCount = 1;
        descriptorLayout.pBindings = &layoutBinding;

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);

        // Create the pipeline layout that is used to generate the rendering pipelines that are based on this descriptor set layout
        // In a more complex scenario you would have different pipeline layouts for different descriptor set layouts that could be reused
        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
        pPipelineLayoutCreateInfo.setLayoutCount = 1;
        pPipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;

        pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);
    }

    void setupDescriptorSet() {
        // Allocate a new descriptor set from the global descriptor pool
        vk::DescriptorSetAllocateInfo allocInfo = {};
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
        writeDescriptorSet.pBufferInfo = &uniformBufferVS.descriptor;
        // Binds this uniform buffer to binding point 0
        writeDescriptorSet.dstBinding = 0;

        device.updateDescriptorSets(writeDescriptorSet, nullptr);
    }

    // Create the depth (and stencil) buffer attachments used by our framebuffers
    // Note: Override of virtual function in the base class and called from within VulkanExampleBase::prepare
    void setupDepthStencil() {
        // Create an optimal image used as the depth stencil attachment
        vk::ImageCreateInfo image;
        image.imageType = vk::ImageType::e2D;
        image.format = depthFormat;
        // Use example's height and width
        image.extent = { width, height, 1 };
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc;
        depthStencil.image = device.createImage(image);

        // Allocate memory for the image (device local) and bind it to our image
        vk::MemoryRequirements memReqs = device.getImageMemoryRequirements(depthStencil.image);
        vk::MemoryAllocateInfo memAlloc;
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        depthStencil.memory = device.allocateMemory(memAlloc);
        device.bindImageMemory(depthStencil.image, depthStencil.memory, 0);

        // Create a view for the depth stencil image
        // Images aren't directly accessed in Vulkan, but rather through views described by a subresource range
        // This allows for multiple views of one image with differing ranges (e.g. for different layers)
        vk::ImageViewCreateInfo depthStencilView;
        depthStencilView.viewType = vk::ImageViewType::e2D;
        depthStencilView.format = depthFormat;
        depthStencilView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        depthStencilView.subresourceRange.levelCount = 1;
        depthStencilView.subresourceRange.layerCount = 1;
        depthStencilView.image = depthStencil.image;
        depthStencil.view = device.createImageView(depthStencilView);
    }

    // Create a frame buffer for each swap chain image
    // Note: Override of virtual function in the base class and called from within VulkanExampleBase::prepare
    void setupFrameBuffer() {
        // Create a frame buffer for every image in the swapchain
        frameBuffers.resize(swapChain.imageCount);
        for (size_t i = 0; i < frameBuffers.size(); i++) {
            std::array<vk::ImageView, 2> attachments;
            attachments[0] = swapChain.images[i].view;  // Color attachment is the view of the swapchain image
            attachments[1] = depthStencil.view;         // Depth/Stencil attachment is the same for all frame buffers

            vk::FramebufferCreateInfo frameBufferCreateInfo;
            // All frame buffers use the same renderpass setup
            frameBufferCreateInfo.renderPass = renderPass;
            frameBufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            frameBufferCreateInfo.pAttachments = attachments.data();
            frameBufferCreateInfo.width = width;
            frameBufferCreateInfo.height = height;
            frameBufferCreateInfo.layers = 1;
            // Create the framebuffer
            frameBuffers[i] = device.createFramebuffer(frameBufferCreateInfo);
        }
    }

    // Render pass setup
    // Render passes are a new concept in Vulkan. They describe the attachments used during rendering and may contain multiple subpasses with attachment dependencies
    // This allows the driver to know up-front what the rendering will look like and is a good opportunity to optimize especially on tile-based renderers (with multiple subpasses)
    // Using sub pass dependencies also adds implicit layout transitions for the attachment used, so we don't need to add explicit image memory barriers to transform them
    // Note: Override of virtual function in the base class and called from within VulkanExampleBase::prepare
    void setupRenderPass() {
        // This example will use a single render pass with one subpass

        // Descriptors for the attachments used by this renderpass
        std::array<vk::AttachmentDescription, 2> attachments;

        // Color attachment
        attachments[0].format = swapChain.colorFormat;                     // Use the color format selected by the swapchain
        attachments[0].loadOp = vk::AttachmentLoadOp::eClear;              // Clear this attachment at the start of the render pass
        attachments[0].storeOp = vk::AttachmentStoreOp::eStore;            // Keep it's contents after the render pass is finished (for displaying it)
        attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;    // We don't use stencil, so don't care for load
        attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;  // Same for store
        attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;      // Layout to which the attachment is transitioned when the render pass is finished

        // Depth attachment
        attachments[1].format = depthFormat;                   // A proper depth format is selected in the example base
        attachments[1].loadOp = vk::AttachmentLoadOp::eClear;  // Clear depth at start of first subpass
        attachments[1].storeOp =
            vk::AttachmentStoreOp::eDontCare;  // We don't need depth after render pass has finished (DONT_CARE may result in better performance)
        attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;                // No stencil
        attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;              // No Stencil
        attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;  // Transition to depth/stencil attachment

        // Setup attachment references
        vk::AttachmentReference colorReference;
        colorReference.attachment = 0;                                     // Attachment 0 is color
        colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;  // Attachment layout used as color during the subpass

        vk::AttachmentReference depthReference;
        depthReference.attachment = 1;                                            // Attachment 1 is color
        depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;  // Attachment used as depth/stemcil used during the subpass

        // Setup a single subpass reference
        vk::SubpassDescription subpassDescription;
        subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpassDescription.colorAttachmentCount = 1;                   // Subpass uses one color attachment
        subpassDescription.pColorAttachments = &colorReference;        // Reference to the color attachment in slot 0
        subpassDescription.pDepthStencilAttachment = &depthReference;  // Reference to the depth attachment in slot 1
        subpassDescription.inputAttachmentCount = 0;                   // Input attachments can be used to sample from contents of a previous subpass
        subpassDescription.pInputAttachments = nullptr;                // (Input attachments not used by this example)
        subpassDescription.preserveAttachmentCount = 0;                // Preserved attachments can be used to loop (and preserve) attachments through subpasses
        subpassDescription.pPreserveAttachments = nullptr;             // (Preserve attachments not used by this example)
        subpassDescription.pResolveAttachments = nullptr;  // Resolve attachments are resolved at the end of a sub pass and can be used for e.g. multi sampling

        // Setup subpass dependencies
        // These will add the implicit ttachment layout transitionss specified by the attachment descriptions
        // The actual usage layout is preserved through the layout specified in the attachment reference
        // Each subpass dependency will introduce a memory and execution dependency between the source and dest subpass described by
        // srcStageMask, dstStageMask, srcAccessMask, dstAccessMask (and dependencyFlags is set)
        // Note: VK_SUBPASS_EXTERNAL is a special constant that refers to all commands executed outside of the actual renderpass)
        std::array<vk::SubpassDependency, 2> dependencies;

        // First dependency at the start of the renderpass
        // Does the transition from final to initial layout
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;  // Producer of the dependency
        dependencies[0].dstSubpass = 0;                    // Consumer is our single subpass that will wait for the execution depdendency
        dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
        dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        // Second dependency at the end the renderpass
        // Does the transition from the initial to the final layout
        dependencies[1].srcSubpass = 0;                    // Producer of the dependency is our single subpass
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;  // Consumer are all commands outside of the renderpass
        dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
        dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        // Create the actual renderpass
        vk::RenderPassCreateInfo renderPassInfo;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());   // Number of attachments used by this render pass
        renderPassInfo.pAttachments = attachments.data();                             // Descriptions of the attachments used by the render pass
        renderPassInfo.subpassCount = 1;                                              // We only use one subpass in this example
        renderPassInfo.pSubpasses = &subpassDescription;                              // Description of that subpass
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());  // Number of subpass dependencies
        renderPassInfo.pDependencies = dependencies.data();                           // Subpass dependencies used by the render pass

        renderPass = device.createRenderPass(renderPassInfo);
    }

    // Vulkan loads it's shaders from an immediate binary representation called SPIR-V
    // Shaders are compiled offline from e.g. GLSL using the reference glslang compiler
    // This function loads such a shader from a binary file and returns a shader module structure
    vk::ShaderModule loadSPIRVShader(std::string filename) {
        size_t shaderSize;
        char* shaderCode = NULL;

#if defined(__ANDROID__)
        // Load shader from compressed asset
        AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
        assert(asset);
        shaderSize = AAsset_getLength(asset);
        assert(shaderSize > 0);

        shaderCode = new char[shaderSize];
        AAsset_read(asset, shaderCode, shaderSize);
        AAsset_close(asset);
#else
        std::ifstream is(filename, std::ios::binary | std::ios::in | std::ios::ate);

        if (is.is_open()) {
            shaderSize = is.tellg();
            is.seekg(0, std::ios::beg);
            // Copy file contents into a buffer
            shaderCode = new char[shaderSize];
            is.read(shaderCode, shaderSize);
            is.close();
            assert(shaderSize > 0);
        }
#endif
        if (shaderCode) {
            // Create a new shader module that will be used for pipeline creation
            vk::ShaderModuleCreateInfo moduleCreateInfo;
            moduleCreateInfo.codeSize = shaderSize;
            moduleCreateInfo.pCode = (uint32_t*)shaderCode;
            vk::ShaderModule shaderModule = device.createShaderModule(moduleCreateInfo);
            delete[] shaderCode;

            return shaderModule;
        } else {
            std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
            return VK_NULL_HANDLE;
        }
    }

    void preparePipelines() {
        // Create the graphics pipeline used in this example
        // Vulkan uses the concept of rendering pipelines to encapsulate fixed states, replacing OpenGL's complex state machine
        // A pipeline is then stored and hashed on the GPU making pipeline changes very fast
        // Note: There are still a few dynamic states that are not directly part of the pipeline (but the info that they are used is)

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
        // The layout used for this pipeline (can be shared among multiple pipelines using the same layout)
        pipelineCreateInfo.layout = pipelineLayout;
        // Renderpass this pipeline is attached to
        pipelineCreateInfo.renderPass = renderPass;

        // Construct the differnent states making up the pipeline

        // Input assembly state describes how primitives are assembled
        // This pipeline will assemble vertex data as a triangle lists (though we only use one triangle)
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
        inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;

        // Rasterization state
        vk::PipelineRasterizationStateCreateInfo rasterizationState;
        rasterizationState.lineWidth = 1.0f;

        // Color blend state describes how blend factors are calculated (if used)
        // We need one blend attachment state per color attachment (even if blending is not used
        vk::PipelineColorBlendAttachmentState blendAttachmentState[1];

        blendAttachmentState[0].colorWriteMask =
            vk::ColorComponentFlagBits::eA | vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB;
        vk::PipelineColorBlendStateCreateInfo colorBlendState;
        colorBlendState.attachmentCount = 1;
        colorBlendState.pAttachments = blendAttachmentState;

        // Viewport state sets the number of viewports and scissor used in this pipeline
        // Note: This is actually overriden by the dynamic states (see below)
        vk::PipelineViewportStateCreateInfo viewportState = {};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Enable dynamic states
        // Most states are baked into the pipeline, but there are still a few dynamic states that can be changed within a command buffer
        // To be able to change these we need do specify which dynamic states will be changed using this pipeline. Their actual states are set later on in the command buffer.
        // For this example we will set the viewport and scissor using dynamic states
        std::vector<vk::DynamicState> dynamicStateEnables;
        dynamicStateEnables.push_back(vk::DynamicState::eViewport);
        dynamicStateEnables.push_back(vk::DynamicState::eScissor);
        vk::PipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.pDynamicStates = dynamicStateEnables.data();
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

        // Depth and stencil state containing depth and stencil compare and test operations
        // We only use depth tests and want depth tests and writes to be enabled and compare with less or equal
        vk::PipelineDepthStencilStateCreateInfo depthStencilState = {};
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = VK_TRUE;
        depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthStencilState.depthBoundsTestEnable = VK_FALSE;
        depthStencilState.back.failOp = vk::StencilOp::eKeep;
        depthStencilState.back.passOp = vk::StencilOp::eKeep;
        depthStencilState.back.compareOp = vk::CompareOp::eAlways;
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front = depthStencilState.back;

        // Multi sampling state
        // This example does not make use fo multi sampling (for anti-aliasing), the state must still be set and passed to the pipeline
        vk::PipelineMultisampleStateCreateInfo multisampleState;

        // Vertex input descriptions
        // Specifies the vertex input parameters for a pipeline

        // Vertex input binding
        // This example uses a single vertex input binding at binding point 0 (see vkCmdBindVertexBuffers)
        vk::VertexInputBindingDescription vertexInputBinding;
        vertexInputBinding.binding = 0;
        vertexInputBinding.stride = sizeof(Vertex);
        vertexInputBinding.inputRate = vk::VertexInputRate::eVertex;

        // Inpute attribute bindings describe shader attribute locations and memory layouts
        std::array<vk::VertexInputAttributeDescription, 2> vertexInputAttributs;
        // These match the following shader layout (see triangle.vert):
        //	layout (location = 0) in vec3 inPos;
        //	layout (location = 1) in vec3 inColor;
        // Attribute location 0: Position
        vertexInputAttributs[0].binding = 0;
        vertexInputAttributs[0].location = 0;
        // Position attribute is three 32 bit signed (SFLOAT) floats (R32 G32 B32)
        vertexInputAttributs[0].format = vk::Format::eR32G32B32Sfloat;
        vertexInputAttributs[0].offset = offsetof(Vertex, position);
        // Attribute location 1: Color
        vertexInputAttributs[1].binding = 0;
        vertexInputAttributs[1].location = 1;
        // Color attribute is three 32 bit signed (SFLOAT) floats (R32 G32 B32)
        vertexInputAttributs[1].format = vk::Format::eR32G32B32Sfloat;
        vertexInputAttributs[1].offset = offsetof(Vertex, color);

        // Vertex input state used for pipeline creation
        vk::PipelineVertexInputStateCreateInfo vertexInputState = {};
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputState.vertexAttributeDescriptionCount = 2;
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributs.data();

        // Shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{};

        // Vertex shader
        // Set pipeline stage for this shader
        shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
        // Load binary SPIR-V shader
        shaderStages[0].module = loadSPIRVShader(getAssetPath() + "shaders/triangle/triangle.vert.spv");
        // Main entry point for the shader
        shaderStages[0].pName = "main";
        assert(shaderStages[0].module);

        // Fragment shader
        // Set pipeline stage for this shader
        shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
        // Load binary SPIR-V shader
        shaderStages[1].module = loadSPIRVShader(getAssetPath() + "shaders/triangle/triangle.frag.spv");
        // Main entry point for the shader
        shaderStages[1].pName = "main";
        assert(shaderStages[1].module);

        // Set pipeline shader stage info
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        // Assign the pipeline states to the pipeline creation info structure
        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.renderPass = renderPass;
        pipelineCreateInfo.pDynamicState = &dynamicState;

        // Create rendering pipeline using the specified states
        pipeline = device.createGraphicsPipeline(pipelineCache, pipelineCreateInfo);

        // Shader modules are no longer needed once the graphics pipeline has been created
        device.destroy(shaderStages[0].module);
        device.destroy(shaderStages[1].module);
    }

    void prepareUniformBuffers() {
        // Prepare and initialize a uniform buffer block containing shader uniforms
        // Single uniforms like in OpenGL are no longer present in Vulkan. All Shader uniforms are passed via uniform buffer blocks
        vk::MemoryRequirements memReqs;

        // Vertex shader uniform buffer block
        vk::BufferCreateInfo bufferInfo = {};
        vk::MemoryAllocateInfo allocInfo = {};

        bufferInfo.size = sizeof(uboVS);
        // This buffer will be used as a uniform buffer
        bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;

        // Create a new buffer
        uniformBufferVS.buffer = device.createBuffer(bufferInfo);
        // Get memory requirements including size, alignment and memory type
        memReqs = device.getBufferMemoryRequirements(uniformBufferVS.buffer);

        allocInfo.allocationSize = memReqs.size;
        // Get the memory type index that supports host visibile memory access
        // Most implementations offer multiple memory types and selecting the correct one to allocate memory from is crucial
        // We also want the buffer to be host coherent so we don't have to flush (or sync after every update.
        // Note: This may affect performance so you might not want to do this in a real world application that updates buffers on a regular base
        allocInfo.memoryTypeIndex =
            getMemoryTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        // Allocate memory for the uniform buffer
        uniformBufferVS.memory = device.allocateMemory(allocInfo);
        // Bind memory to buffer
        device.bindBufferMemory(uniformBufferVS.buffer, uniformBufferVS.memory, 0);

        // Store information in the uniform's descriptor that is used by the descriptor set
        uniformBufferVS.descriptor.buffer = uniformBufferVS.buffer;
        uniformBufferVS.descriptor.offset = 0;
        uniformBufferVS.descriptor.range = sizeof(uboVS);

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Update matrices
        uboVS.projectionMatrix = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);

        uboVS.viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

        uboVS.modelMatrix = glm::mat4(1.0f);
        uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        // Map uniform buffer and update it
        uint8_t* pData = (uint8_t*)device.mapMemory(uniformBufferVS.memory, 0, VK_WHOLE_SIZE);
        memcpy(pData, &uboVS, sizeof(uboVS));
        // Unmap after data has been copied
        // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
        device.unmapMemory(uniformBufferVS.memory);
    }

    void prepare() {
        Parent::prepare();
        // Set clear values for all framebuffer attachments with loadOp set to clear
        // We use two attachments (color and depth) that are cleared at the start of the subpass and as such we need to set clear values for both
        clearValues[0].color = vks::util::clearColor({ 0.0f, 0.0f, 0.2f, 1.0f });
        clearValues[1].depthStencil = { 1.0f, 0 };
        prepareSynchronizationPrimitives();
        prepareVertices(USE_STAGING);
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
    }

    virtual void viewChanged() {
        // This function is called by the base example class each time the view is changed by user input
        updateUniformBuffers();
    }
};

VULKAN_EXAMPLE_MAIN()