/*
* UI overlay class using ImGui
*
* Copyright (C) 2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "common.hpp"
#include "VulkanBuffer.hpp"

namespace vks 
{
    struct UIOverlayCreateInfo 
    {
        const vkx::Context& context;
        vk::Queue copyQueue;
        vk::RenderPass renderPass;
        std::vector<vk::Framebuffer> framebuffers;
        vk::Format colorformat;
        vk::Format depthformat;
        uvec2 size;
        std::vector<vk::PipelineShaderStageCreateInfo> shaders;
        vk::SampleCountFlagBits rasterizationSamples{ vk::SampleCountFlagBits::e1 };
        uint32_t subpassCount{ 1 };
        std::vector<vk::ClearValue> clearValues = {};
        uint32_t attachmentCount = 1;
    };

    class UIOverlay 
    {
    private:
        UIOverlayCreateInfo createInfo;
        const vkx::Context& context{ createInfo.context };
        vks::Buffer vertexBuffer;
        vks::Buffer indexBuffer;
        int32_t vertexCount = 0;
        int32_t indexCount = 0;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorSet descriptorSet;
        vk::PipelineLayout pipelineLayout;
        const vk::PipelineCache& pipelineCache{ context.pipelineCache };
        vk::Pipeline pipeline;
        vk::RenderPass renderPass;
        vk::CommandPool commandPool;
        vk::Fence fence;

        vkx::CreateImageResult font;

        struct PushConstBlock {
            glm::vec2 scale;
            glm::vec2 translate;
        } pushConstBlock;


        void prepareResources();
        void preparePipeline();
        void prepareRenderPass();
        void updateCommandBuffers();
    public:
        bool visible = true;
        float scale = 1.0f;

        std::vector<vk::CommandBuffer> cmdBuffers;

        UIOverlay(const vks::UIOverlayCreateInfo& createInfo);
        ~UIOverlay();

        void update();
        void resize(const uvec2& newSize, const std::vector<VkFramebuffer>& framebuffers);

        void submit(vk::Queue queue, uint32_t bufferindex, vk::SubmitInfo submitInfo);

        bool header(const char* caption);
        bool checkBox(const char* caption, bool* value);
        bool checkBox(const char* caption, int32_t* value);
        bool inputFloat(const char* caption, float* value, float step, uint32_t precision);
        bool sliderFloat(const char* caption, float* value, float min, float max);
        bool sliderInt(const char* caption, int32_t* value, int32_t min, int32_t max);
        bool comboBox(const char* caption, int32_t* itemindex, std::vector<std::string> items);
        bool button(const char* caption);
        void text(const char* formatstr, ...);
    };
}