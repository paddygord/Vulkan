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
    // Selected a suitable supported depth format starting with 32 bit down to 16 bit
    // Returns false if none of the depth formats in the list is supported by the device
    vk::Format getSupportedDepthFormat(vk::PhysicalDevice physicalDevice);

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
