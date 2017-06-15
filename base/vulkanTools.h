/*
* Assorted commonly used Vulkan helper functions
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "common.hpp"
#include "vulkanVersion.hpp"

// Default fence timeout in nanoseconds
#define DEFAULT_FENCE_TIMEOUT 100000000000

namespace vkx {

    // Check if extension is globally available
    vk::Bool32 checkGlobalExtensionPresent(const char* extensionName);
    // Check if extension is present on the given device
    vk::Bool32 checkDeviceExtensionPresent(vk::PhysicalDevice physicalDevice, const char* extensionName);
    // Selected a suitable supported depth format starting with 32 bit down to 16 bit
    // Returns false if none of the depth formats in the list is supported by the device
    vk::Format getSupportedDepthFormat(vk::PhysicalDevice physicalDevice);
    vk::AccessFlags accessFlagsForLayout(vk::ImageLayout layout);

    vk::ImageMemoryBarrier getImageMemoryBarrier(
        const vk::Image& image,
        const vk::ImageLayout& newImageLayout,
        const vk::ImageLayout& oldImageLayout,
        const vk::ImageSubresourceRange& subresourceRange);

    vk::ImageMemoryBarrier getImageMemoryBarrier(
        const vk::Image& image,
        const vk::ImageLayout& newImageLayout,
        const vk::ImageLayout& oldImageLayout,
        const vk::ImageAspectFlags& aspect = vk::ImageAspectFlagBits::eColor);

    // Put an image memory barrier for setting an image layout on the sub resource into the given command buffer
    void setImageLayout(
        const vk::CommandBuffer& cmdbuffer,
        const vk::Image& image,
        const vk::ImageLayout& newImageLayout,
        const vk::ImageLayout& oldImageLayout,
        const vk::ImageSubresourceRange& subresourceRange);
    // Uses a fixed sub resource layout with first mip level and layer
    void setImageLayout(
        const vk::CommandBuffer& cmdbuffer,
        const vk::Image& image,
        const vk::ImageLayout& newImageLayout,
        const vk::ImageLayout& oldImageLayout = vk::ImageLayout::eUndefined,
        const vk::ImageAspectFlags& aspectMask = vk::ImageAspectFlagBits::eColor,
        uint32_t levelCount = 1);

    // Load a text file (e.g. GLGL shader) into a std::string
    std::string readTextFile(const std::string& filename);

    // Load a binary file into a buffer (e.g. SPIR-V)
    std::vector<uint8_t> readBinaryFile(const std::string& filename);

    // Load a SPIR-V shader
    vk::ShaderModule loadShader(const std::string& filename, vk::Device device, vk::ShaderStageFlagBits stage);

    // Load a GLSL shader
    // Note : Only for testing purposes, support for directly feeding GLSL shaders into Vulkan
    // may be dropped at some point    
    vk::ShaderModule loadShaderGLSL(const std::string& filename, vk::Device device, vk::ShaderStageFlagBits stage);

    // A wrapper class for an allocation, either an Image or Buffer.  Not intended to be used used directly
    // but only as a base class providing common functionality for the classes below.
    //
    // Provides easy to use mechanisms for mapping, unmapping and copying host data to the device memory
    struct AllocatedResult {
        vk::Device device;
        vk::DeviceMemory memory;
        vk::DeviceSize size{ 0 };
        vk::DeviceSize alignment{ 0 };
        vk::DeviceSize allocSize{ 0 };
        void* mapped{ nullptr };

        template <typename T = void>
        inline T* map(size_t offset = 0, size_t size = VK_WHOLE_SIZE) {
            mapped = device.mapMemory(memory, offset, size, vk::MemoryMapFlags());
            return (T*)mapped;
        }

        inline void unmap() {
            device.unmapMemory(memory);
            mapped = nullptr;
        }

        inline void copy(size_t size, const void* data, size_t offset = 0) const {
            memcpy((uint8_t*)mapped + offset, data, size);
        }

        template<typename T>
        inline void copy(const T& data, size_t offset = 0) const {
            copy(sizeof(T), &data, offset);
        }

        template<typename T>
        inline void copy(const std::vector<T>& data, size_t offset = 0) const {
            copy(sizeof(T) * data.size(), data.data(), offset);
        }

        virtual void destroy() {
            if (mapped) {
                unmap();
            }
            if (memory) {
                device.freeMemory(memory);
                memory = vk::DeviceMemory();
            }
        }
    };

    // Encaspulates an image, the memory for that image, a view of the image,
    // as well as a sampler and the image format.
    //
    // The sampler is not populated by the allocation code, but is provided
    // for convenience and easy cleanup if it is populated.
    struct CreateImageResult : public AllocatedResult {
    private:
        using Parent = AllocatedResult;
    public:
        vk::Extent3D extent;
        vk::Image image;
        vk::ImageView view;
        vk::Sampler sampler;
        vk::Format format{ vk::Format::eUndefined };

        void destroy() override {
            Parent::destroy();
            if (mapped) {
                unmap();
            }
            if (sampler) {
                device.destroySampler(sampler);
                sampler = vk::Sampler();
            }
            if (view) {
                device.destroyImageView(view);
                view = vk::ImageView();
            }
            if (image) {
                device.destroyImage(image);
                image = vk::Image();
            }
            Parent::destroy();
        }
    };

    struct CreateBufferResult : public AllocatedResult {
    private:
        using Parent = AllocatedResult;
    public:
        vk::Buffer buffer;
        vk::DescriptorBufferInfo descriptor;

        void destroy() override {
            if (mapped) {
                unmap();
            }
            if (buffer) {
                device.destroyBuffer(buffer);
                buffer = vk::Buffer();
            }
            Parent::destroy();
        }
    };

    template <typename T>
    void updateVectorBindings(const T*& pointer, uint32_t& size, const std::vector<T>& vector) {
        if (vector.empty()) {
            pointer = nullptr;
            size = 0;
        } else {
            pointer = vector.data();
            size = (uint32_t)vector.size();
        }
    }

    struct GraphicsPipelineCreateInfo : public vk::GraphicsPipelineCreateInfo {
        GraphicsPipelineCreateInfo() {
            pVertexInputState = &inputState.createInfo;
            pDynamicState = &dynamicState.createInfo;
            pColorBlendState = &colorBlendState.createInfo;
            pViewportState = &viewportState.createInfo;
            pInputAssemblyState = &inputAssemblyState;
            pRasterizationState = &rasterizationState;
            pMultisampleState = &multisampleState;
            pDepthStencilState = &depthStencilState;
        }

        operator vk::GraphicsPipelineCreateInfo&() {
            update();
            return *this;
        }


        void update() {
            inputState.update();
            colorBlendState.update();
            dynamicState.update();
            viewportState.update();
            updateVectorBindings(pStages, stageCount, shaderStages);
        }

        struct VertexInputState {
            vk::PipelineVertexInputStateCreateInfo createInfo;
            std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
            std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
            void update() {
                // Assign to vertex input state
                updateVectorBindings(createInfo.pVertexBindingDescriptions, createInfo.vertexBindingDescriptionCount, bindingDescriptions);
                updateVectorBindings(createInfo.pVertexAttributeDescriptions, createInfo.vertexAttributeDescriptionCount, attributeDescriptions);
            }
        } inputState;

        struct ColorBlendState {
            vk::PipelineColorBlendStateCreateInfo createInfo;
            std::vector<vk::PipelineColorBlendAttachmentState> blendAttachmentStates;
            void update() {
                updateVectorBindings(createInfo.pAttachments, createInfo.attachmentCount, blendAttachmentStates);
            }
        } colorBlendState;

        struct DynamicState {
            vk::PipelineDynamicStateCreateInfo createInfo;
            std::vector<vk::DynamicState> dynamicStateEnables;

            void update() {
                updateVectorBindings(createInfo.pDynamicStates, createInfo.dynamicStateCount, dynamicStateEnables);
            }

        } dynamicState;

        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
        vk::PipelineRasterizationStateCreateInfo rasterizationState;

        struct ViewportState {
            vk::PipelineViewportStateCreateInfo createInfo;
            std::vector<vk::Viewport> viewports;
            std::vector<vk::Rect2D> scissors;
            void update() {
                //updateVectorBindings(createInfo.pViewports, createInfo.viewportCount, viewports);
                //updateVectorBindings(createInfo.pScissors, createInfo.scissorCount, scissors);
            }
        } viewportState;
        vk::PipelineDepthStencilStateCreateInfo depthStencilState;
        vk::PipelineMultisampleStateCreateInfo multisampleState;
        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    };

    struct SubmitInfo : public vk::SubmitInfo {
        std::vector<vk::CommandBuffer> commandBuffers;
        std::vector<vk::Semaphore> waitSemaphores;
        std::vector<vk::PipelineStageFlags> waitStages;
        std::vector<vk::Semaphore> signalSemaphores;

        SubmitInfo() {
        }

        SubmitInfo(
            vk::ArrayProxy<const vk::CommandBuffer> commandBuffers_, 
            vk::ArrayProxy<const std::pair<const vk::Semaphore, const vk::PipelineStageFlags>> waits_ = {}, 
            vk::ArrayProxy<const vk::Semaphore> signalsSemaphores_ = {}) {
            waitSemaphores.reserve(waits_.size());
            waitStages.reserve(waits_.size());
            std::for_each(waits_.begin(), waits_.end(), [this](const std::pair<vk::Semaphore, vk::PipelineStageFlags>& pair) {
                waitSemaphores.push_back(pair.first);
                waitStages.push_back(pair.second);
            });
            signalSemaphores.reserve(signalsSemaphores_.size());
            signalSemaphores.insert(signalSemaphores.end(), signalsSemaphores_.begin(), signalsSemaphores_.end());
            commandBuffers.reserve(commandBuffers_.size());
            commandBuffers.insert(commandBuffers.end(), commandBuffers_.begin(), commandBuffers_.end());
        }

        void addWait(const vk::Semaphore& semaphore, const vk::PipelineStageFlags& stageFlags) {
            waitStages.push_back(stageFlags);
            waitSemaphores.push_back(semaphore);
        }

        SubmitInfo& update() {
            updateVectorBindings(pCommandBuffers, commandBufferCount, commandBuffers);
            if (waitStages.size() != waitSemaphores.size()) {
                throw std::runtime_error("Mismatched wait semaphores and stages");
            }
            updateVectorBindings(pWaitSemaphores, waitSemaphoreCount, waitSemaphores);
            updateVectorBindings(pWaitDstStageMask, waitSemaphoreCount, waitStages);
            updateVectorBindings(pSignalSemaphores, signalSemaphoreCount, signalSemaphores);
            return *this;
        }
    };
    
    // Contains all vulkan objects
    // required for a uniform data object
    using UniformData = vkx::CreateBufferResult;

    //////////////////////////////////////////////////////////////////////////////
    //
    // Helper functions to create commonly used types while taking 
    // only a subset of the total possible number of structure members
    // (leaving the remaining at reasonable defaults)
    //

    // Contains often used vulkan object initializers
    // Save lot of VK_STRUCTURE_TYPE assignments
    // Some initializers are parameterized for convenience
    vk::ClearColorValue clearColor(const glm::vec4& v);

    vk::CommandBufferAllocateInfo commandBufferAllocateInfo(vk::CommandPool commandPool, vk::CommandBufferLevel level, uint32_t bufferCount);

    vk::FenceCreateInfo fenceCreateInfo(vk::FenceCreateFlags flags);

    vk::Viewport viewport(
        float width,
        float height,
        float minDepth = 0,
        float maxDepth = 1);

    vk::Viewport viewport(
        const glm::uvec2& size,
        float minDepth = 0,
        float maxDepth = 1);

    vk::Viewport viewport(
        const vk::Extent2D& size,
        float minDepth = 0,
        float maxDepth = 1);

    vk::Rect2D rect2D(
        uint32_t width,
        uint32_t height,
        int32_t offsetX = 0,
        int32_t offsetY = 0);

    vk::Rect2D rect2D(
        const glm::uvec2& size,
        const glm::ivec2& offset = glm::ivec2(0));

    vk::Rect2D rect2D(
        const vk::Extent2D& size,
        const vk::Offset2D& offset = vk::Offset2D());

    vk::BufferCreateInfo bufferCreateInfo(
        vk::BufferUsageFlags usage,
        vk::DeviceSize size);

    vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo(
        uint32_t poolSizeCount,
        vk::DescriptorPoolSize* pPoolSizes,
        uint32_t maxSets);

    vk::DescriptorPoolSize descriptorPoolSize(
        vk::DescriptorType type,
        uint32_t descriptorCount);

    vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding(
        vk::DescriptorType type,
        vk::ShaderStageFlags stageFlags,
        uint32_t binding);

    vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo(
        const vk::DescriptorSetLayoutBinding* pBindings,
        uint32_t bindingCount);

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo(
        const vk::DescriptorSetLayout* pSetLayouts,
        uint32_t setLayoutCount);

    vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(
        vk::DescriptorPool descriptorPool,
        const vk::DescriptorSetLayout* pSetLayouts,
        uint32_t descriptorSetCount);

    vk::DescriptorImageInfo descriptorImageInfo(
        vk::Sampler sampler,
        vk::ImageView imageView,
        vk::ImageLayout imageLayout);

    vk::WriteDescriptorSet writeDescriptorSet(
        vk::DescriptorSet dstSet,
        vk::DescriptorType type,
        uint32_t binding,
        vk::DescriptorBufferInfo* bufferInfo);

    vk::WriteDescriptorSet writeDescriptorSet(
        vk::DescriptorSet dstSet,
        vk::DescriptorType type,
        uint32_t binding,
        vk::DescriptorImageInfo* imageInfo);

    vk::VertexInputBindingDescription vertexInputBindingDescription(
        uint32_t binding,
        uint32_t stride,
        vk::VertexInputRate inputRate);

    vk::VertexInputAttributeDescription vertexInputAttributeDescription(
        uint32_t binding,
        uint32_t location,
        vk::Format format,
        uint32_t offset);

    vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo(
        vk::PrimitiveTopology topology,
        vk::PipelineInputAssemblyStateCreateFlags flags = vk::PipelineInputAssemblyStateCreateFlags(),
        vk::Bool32 primitiveRestartEnable = VK_FALSE);

    vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo(
        vk::PolygonMode polygonMode,
        vk::CullModeFlags cullMode,
        vk::FrontFace frontFace,
        vk::PipelineRasterizationStateCreateFlags flags = vk::PipelineRasterizationStateCreateFlags());

    vk::ColorComponentFlags fullColorWriteMask();

    vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState(
        vk::ColorComponentFlags colorWriteMask = fullColorWriteMask(),
        vk::Bool32 blendEnable = VK_FALSE);

    vk::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo(
        uint32_t attachmentCount,
        const vk::PipelineColorBlendAttachmentState* pAttachments);

    vk::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo(
        vk::Bool32 depthTestEnable,
        vk::Bool32 depthWriteEnable,
        vk::CompareOp depthCompareOp);

    vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo(
        uint32_t viewportCount,
        uint32_t scissorCount,
        vk::PipelineViewportStateCreateFlags flags = vk::PipelineViewportStateCreateFlags());

    vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo(
        vk::SampleCountFlagBits rasterizationSamples,
        vk::PipelineMultisampleStateCreateFlags flags = vk::PipelineMultisampleStateCreateFlags());

    vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo(
        const vk::DynamicState *pDynamicStates,
        uint32_t dynamicStateCount,
        vk::PipelineDynamicStateCreateFlags flags = vk::PipelineDynamicStateCreateFlags());

    vk::PipelineTessellationStateCreateInfo pipelineTessellationStateCreateInfo(
        uint32_t patchControlPoints);

    vk::GraphicsPipelineCreateInfo pipelineCreateInfo(
        vk::PipelineLayout layout,
        vk::RenderPass renderPass,
        vk::PipelineCreateFlags flags = vk::PipelineCreateFlags());

    vk::ComputePipelineCreateInfo computePipelineCreateInfo(
        vk::PipelineLayout layout,
        vk::PipelineCreateFlags flags = vk::PipelineCreateFlags());

    vk::PushConstantRange pushConstantRange(
        vk::ShaderStageFlags stageFlags,
        uint32_t size,
        uint32_t offset);

    const std::string& getAssetPath();
}
