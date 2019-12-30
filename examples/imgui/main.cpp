/*
* Vulkan Example - imGui (https://github.com/ocornut/imgui)
*
* Copyright (C) 2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <array>
#include <algorithm>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanModel.hpp"

#define ENABLE_VALIDATION false

// Options and values to display/toggle from the UI
struct UISettings {
    bool displayModels = true;
    bool displayLogos = true;
    bool displayBackground = true;
    bool animateLight = false;
    float lightSpeed = 0.25f;
    std::array<float, 50> frameTimes{};
    float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
    float lightTimer = 0.0f;
} uiSettings;

// ----------------------------------------------------------------------------
// ImGUI class
// ----------------------------------------------------------------------------
class ImGUI {
private:
    // Vulkan resources for rendering the UI
    vk::Sampler sampler;
    vks::Buffer vertexBuffer;
    vks::Buffer indexBuffer;
    int32_t vertexCount = 0;
    int32_t indexCount = 0;
    vk::DeviceMemory fontMemory = VK_NULL_HANDLE;
    vk::Image fontImage = VK_NULL_HANDLE;
    vk::ImageView fontView = VK_NULL_HANDLE;
    vk::PipelineCache pipelineCache;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;
    vk::DescriptorPool descriptorPool;
    vk::DescriptorSetLayout descriptorSetLayout;
    vk::DescriptorSet descriptorSet;
    vks::VulkanDevice* device;
    VulkanExampleBase* example;

public:
    // UI params are set via push constants
    struct PushConstBlock {
        glm::vec2 scale;
        glm::vec2 translate;
    } pushConstBlock;

    ImGUI(VulkanExampleBase* example)
        : example(example) {
        device = example->vulkanDevice;
        ImGui::CreateContext();
    };

    ~ImGUI() {
        ImGui::DestroyContext();
        // Release all Vulkan resources required for rendering imGui
        vertexBuffer.destroy();
        indexBuffer.destroy();
        vkDestroyImage(device->logicalDevice, fontImage, nullptr);
        vkDestroyImageView(device->logicalDevice, fontView, nullptr);
        vkFreeMemory(device->logicalDevice, fontMemory, nullptr);
        vkDestroySampler(device->logicalDevice, sampler, nullptr);
        vkDestroyPipelineCache(device->logicalDevice, pipelineCache, nullptr);
        vkDestroyPipeline(device->logicalDevice, pipeline, nullptr);
        vkDestroyPipelineLayout(device->logicalDevice, pipelineLayout, nullptr);
        vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayout, nullptr);
    }

    // Initialize styles, keys, etc.
    void init(float width, float height) {
        // Color scheme
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
        style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        // Dimensions
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(width, height);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }

    // Initialize all Vulkan resources used by the ui
    void initResources(vk::RenderPass renderPass, vk::Queue copyQueue) {
        ImGuiIO& io = ImGui::GetIO();

        // Create font texture
        unsigned char* fontData;
        int texWidth, texHeight;
        io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
        vk::DeviceSize uploadSize = texWidth * texHeight * 4 * sizeof(char);

        // Create target image for copy
        vk::ImageCreateInfo imageInfo;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = vk::Format::eR8G8B8A8Unorm;
        imageInfo.extent.width = texWidth;
        imageInfo.extent.height = texHeight;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageInfo, nullptr, &fontImage));
        vk::MemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device->logicalDevice, fontImage, &memReqs);
        vk::MemoryAllocateInfo memAllocInfo;
        memAllocInfo.allocationSize = memReqs.size;
        memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &fontMemory));
        VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, fontImage, fontMemory, 0));

        // Image view
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = fontImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = vk::Format::eR8G8B8A8Unorm;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &fontView));

        // Staging buffers for font data upload
        vks::Buffer stagingBuffer;

        VK_CHECK_RESULT(device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                             &stagingBuffer, uploadSize));

        stagingBuffer.map();
        memcpy(stagingBuffer.mapped, fontData, uploadSize);
        stagingBuffer.unmap();

        // Copy buffer data to font image
        vk::CommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        // Prepare for transfer
        vks::tools::setImageLayout(copyCmd, fontImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        // Copy
        vk::BufferImageCopy bufferCopyRegion = {};
        bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bufferCopyRegion.imageSubresource.layerCount = 1;
        bufferCopyRegion.imageExtent.width = texWidth;
        bufferCopyRegion.imageExtent.height = texHeight;
        bufferCopyRegion.imageExtent.depth = 1;

        vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

        // Prepare for shader read
        vks::tools::setImageLayout(copyCmd, fontImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        device->flushCommandBuffer(copyCmd, copyQueue, true);

        stagingBuffer.destroy();

        // Font texture Sampler
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

        // Descriptor pool
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eCombinedImageSampler, 1 } };
        vk::DescriptorPoolCreateInfo descriptorPoolInfo{ poolSizes, 2 };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));

        // Descriptor set layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            { vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0 },
        };
        vk::DescriptorSetLayoutCreateInfo descriptorLayout{ setLayoutBindings };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayout, nullptr, &descriptorSetLayout));

        // Descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, &descriptorSetLayout, 1 };
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &allocInfo, &descriptorSet));
        vk::DescriptorImageInfo fontDescriptor = vk::descriptorImageInfo{sampler, fontView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = { { descriptorSet, vk::DescriptorType::eCombinedImageSampler, 0, &fontDescriptor } };
        vkUpdateDescriptorSets(device->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

        // Pipeline cache
        vk::PipelineCacheCreateInfo pipelineCacheCreateInfo = {};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK_RESULT(vkCreatePipelineCache(device->logicalDevice, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

        // Pipeline layout
        // Push constants for UI rendering parameters
        vk::PushConstantRange pushConstantRange{ vk::ShaderStageFlagBits::eVertex, sizeof(PushConstBlock), 0 };
        vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{ &descriptorSetLayout, 1 };
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device->logicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

        // Setup graphics pipeline for UI rendering
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };

        vk::PipelineRasterizationStateCreateInfo rasterizationState = { VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE };

        // Enable blending
        vk::PipelineColorBlendAttachmentState blendAttachmentState{};
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

        vk::PipelineColorBlendStateCreateInfo colorBlendState = { 1, &blendAttachmentState };

        vk::PipelineDepthStencilStateCreateInfo depthStencilState = { VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL };

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = { VK_SAMPLE_COUNT_1_BIT };

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState = { dynamicStateEnables };

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{};

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo{ pipelineLayout, renderPass };

        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        // Vertex bindings an attributes based on ImGui vertex definition
        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex },
        };
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32sFloat, offsetof(ImDrawVert, pos) },   // Location 0: Position
            { 0, 1, vk::Format::eR32G32sFloat, offsetof(ImDrawVert, uv) },    // Location 1: UV
            { 0, 2, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col) },  // Location 0: Color
        };
        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        pipelineCreateInfo.pVertexInputState = &vertexInputState;

        shaderStages[0] = example->loadShader(ASSET_PATH "shaders/imgui/ui.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = example->loadShader(ASSET_PATH "shaders/imgui/ui.frag.spv", vk::ShaderStageFlagBits::eFragment);

        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device->logicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
    }

    // Starts a new imGui frame and sets up windows and ui elements
    void newFrame(VulkanExampleBase* example, bool updateFrameGraph) {
        ImGui::NewFrame();

        // Init imGui windows and elements

        ImVec4 clear_color = ImColor(114, 144, 154);
        static float f = 0.0f;
        ImGui::TextUnformatted(example->title.c_str());
        ImGui::TextUnformatted(device->properties.deviceName);

        // Update frame time display
        if (updateFrameGraph) {
            std::rotate(uiSettings.frameTimes.begin(), uiSettings.frameTimes.begin() + 1, uiSettings.frameTimes.end());
            float frameTime = 1000.0f / (example->frameTimer * 1000.0f);
            uiSettings.frameTimes.back() = frameTime;
            if (frameTime < uiSettings.frameTimeMin) {
                uiSettings.frameTimeMin = frameTime;
            }
            if (frameTime > uiSettings.frameTimeMax) {
                uiSettings.frameTimeMax = frameTime;
            }
        }

        ImGui::PlotLines("Frame Times", &uiSettings.frameTimes[0], 50, 0, "", uiSettings.frameTimeMin, uiSettings.frameTimeMax, ImVec2(0, 80));

        ImGui::Text("Camera");
        ImGui::InputFloat3("position", &example->camera.position.x, 2);
        ImGui::InputFloat3("rotation", &example->camera.rotation.x, 2);

        ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiSetCond_FirstUseEver);
        ImGui::Begin("Example settings");
        ImGui::Checkbox("Render models", &uiSettings.displayModels);
        ImGui::Checkbox("Display logos", &uiSettings.displayLogos);
        ImGui::Checkbox("Display background", &uiSettings.displayBackground);
        ImGui::Checkbox("Animate light", &uiSettings.animateLight);
        ImGui::SliderFloat("Light speed", &uiSettings.lightSpeed, 0.1f, 1.0f);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiSetCond_FirstUseEver);
        ImGui::ShowDemoWindow();

        // Render to generate draw buffers
        ImGui::Render();
    }

    // Update vertex and index buffer containing the imGui elements when required
    void updateBuffers() {
        ImDrawData* imDrawData = ImGui::GetDrawData();

        // Note: Alignment is done inside buffer creation
        vk::DeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
        vk::DeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

        if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
            return;
        }

        // Update buffers only if vertex or index count has been changed compared to current buffer size

        // Vertex buffer
        if ((vertexBuffer.buffer == VK_NULL_HANDLE) || (vertexCount != imDrawData->TotalVtxCount)) {
            vertexBuffer.unmap();
            vertexBuffer.destroy();
            VK_CHECK_RESULT(device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &vertexBuffer, vertexBufferSize));
            vertexCount = imDrawData->TotalVtxCount;
            vertexBuffer.unmap();
            vertexBuffer.map();
        }

        // Index buffer
        vk::DeviceSize indexSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);
        if ((indexBuffer.buffer == VK_NULL_HANDLE) || (indexCount < imDrawData->TotalIdxCount)) {
            indexBuffer.unmap();
            indexBuffer.destroy();
            VK_CHECK_RESULT(device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &indexBuffer, indexBufferSize));
            indexCount = imDrawData->TotalIdxCount;
            indexBuffer.map();
        }

        // Upload data
        ImDrawVert* vtxDst = (ImDrawVert*)vertexBuffer.mapped;
        ImDrawIdx* idxDst = (ImDrawIdx*)indexBuffer.mapped;

        for (int n = 0; n < imDrawData->CmdListsCount; n++) {
            const ImDrawList* cmd_list = imDrawData->CmdLists[n];
            memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtxDst += cmd_list->VtxBuffer.Size;
            idxDst += cmd_list->IdxBuffer.Size;
        }

        // Flush to make writes visible to GPU
        vertexBuffer.flush();
        indexBuffer.flush();
    }

    // Draw current imGui frame into a command buffer
    void drawFrame(vk::CommandBuffer commandBuffer) {
        ImGuiIO& io = ImGui::GetIO();

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        vk::Viewport viewport{ ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 0.0f, 1.0f };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        // UI scale and translate via push constants
        pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
        pushConstBlock.translate = glm::vec2(-1.0f);
        vkCmdPushConstants(commandBuffer, pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushConstBlock), &pushConstBlock);

        // Render commands
        ImDrawData* imDrawData = ImGui::GetDrawData();
        int32_t vertexOffset = 0;
        int32_t indexOffset = 0;

        if (imDrawData->CmdListsCount > 0) {
            vk::DeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer, offsets);
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

            for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
                const ImDrawList* cmd_list = imDrawData->CmdLists[i];
                for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
                    const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
                    vk::Rect2D scissorRect;
                    scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
                    scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
                    scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
                    scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
                    vkCmdSetScissor(commandBuffer, 0, 1, &scissorRect);
                    vkCmdDrawIndexed(commandBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
                    indexOffset += pcmd->ElemCount;
                }
                vertexOffset += cmd_list->VtxBuffer.Size;
            }
        }
    }
};

// ----------------------------------------------------------------------------
// VulkanExample
// ----------------------------------------------------------------------------

class VulkanExample : public VulkanExampleBase {
public:
    ImGUI* imGui = nullptr;

    // Vertex layout for the models
    vkx::vertex::Layout vertexLayout{ {
        vkx::vertex::VERTEX_COMPONENT_POSITION,
        vkx::vertex::VERTEX_COMPONENT_NORMAL,
        vkx::vertex::VERTEX_COMPONENT_COLOR,
    } };

    struct Models {
        vkx::model::Model models;
        vkx::model::Model logos;
        vkx::model::Model background;
    } models;

    vks::Buffer uniformBufferVS;

    struct UBOVS {
        glm::mat4 projection;
        glm::mat4 modelview;
        glm::vec4 lightPos;
    } uboVS;

    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;
    vk::DescriptorSetLayout descriptorSetLayout;
    vk::DescriptorSet descriptorSet;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Vulkan Example - ImGui";
        camera.type = Camera::CameraType::lookat;
        camera.setPosition(glm::vec3(0.0f, 1.4f, -4.8f));
        camera.setRotation(glm::vec3(4.5f, -380.0f, 0.0f));
        camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
    }

    ~VulkanExample() {
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        models.models.destroy();
        models.background.destroy();
        models.logos.destroy();

        uniformBufferVS.destroy();

        delete imGui;
    }

    void buildCommandBuffers() {
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValues[2];
        clearValues[0].color = { { 0.2f, 0.2f, 0.2f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        imGui->newFrame(this, (frameCounter == 0));

        imGui->updateBuffers();

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vk::Viewport viewport{ (float)width, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            vk::Rect2D scissor{ width, height, 0, 0 };
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            // Render scene
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            vk::DeviceSize offsets[1] = { 0 };
            if (uiSettings.displayBackground) {
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.background.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.background.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.background.indexCount, 1, 0, 0, 0);
            }

            if (uiSettings.displayModels) {
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.models.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.models.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.models.indexCount, 1, 0, 0, 0);
            }

            if (uiSettings.displayLogos) {
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.logos.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], models.logos.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(drawCmdBuffers[i], models.logos.indexCount, 1, 0, 0, 0);
            }

            // Render imGui
            imGui->drawFrame(drawCmdBuffers[i]);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void setupLayoutsAndDescriptors() {
        // descriptor pool
        std::vector<vk::DescriptorPoolSize> poolSizes = { { vk::DescriptorType::eUniformBuffer, 2 }, { vk::DescriptorType::eCombinedImageSampler, 1 } };
        vk::DescriptorPoolCreateInfo descriptorPoolInfo{ poolSizes, 2 };
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

        // Set layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            { vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0 },
        };
        vk::DescriptorSetLayoutCreateInfo descriptorLayout = { setLayoutBindings };
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        // Pipeline layout
        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo{ &descriptorSetLayout, 1 };
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));

        // Descriptor set
        vk::DescriptorSetAllocateInfo allocInfo = { descriptorPool, &descriptorSetLayout, 1 };
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            { descriptorSet, vk::DescriptorType::eUniformBuffer, 0, &uniformBufferVS.descriptor },
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    }

    void preparePipelines() {
        // Rendering
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE };

        vk::PipelineRasterizationStateCreateInfo rasterizationState = { VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE };

        vk::PipelineColorBlendAttachmentState blendAttachmentState = { 0xf, VK_FALSE };

        vk::PipelineColorBlendStateCreateInfo colorBlendState = { 1, &blendAttachmentState };

        vk::PipelineDepthStencilStateCreateInfo depthStencilState = { VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL };

        vk::PipelineViewportStateCreateInfo viewportState = { 1, 1, 0 };

        vk::PipelineMultisampleStateCreateInfo multisampleState = { VK_SAMPLE_COUNT_1_BIT };

        std::vector<vk::DynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        vk::PipelineDynamicStateCreateInfo dynamicState = { dynamicStateEnables };

        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo{ pipelineLayout, renderPass };

        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        std::vector<vk::VertexInputBindingDescription> vertexInputBindings = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes = {
            { 0, 0, vk::Format::eR32G32B32sFloat, 0 },                  // Location 0: Position
            { 0, 1, vk::Format::eR32G32B32sFloat, sizeof(float) * 3 },  // Location 1: Normal
            { 0, 2, vk::Format::eR32G32B32sFloat, sizeof(float) * 6 },  // Location 2: Color
        };
        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
        vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        pipelineCreateInfo.pVertexInputState = &vertexInputState;

        shaderStages[0] = loadShader(ASSET_PATH "shaders/imgui/scene.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(ASSET_PATH "shaders/imgui/scene.frag.spv", vk::ShaderStageFlagBits::eFragment);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBufferVS, sizeof(uboVS),
                                                   &uboVS));

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader
        uboVS.projection = camera.matrices.perspective;
        uboVS.modelview = camera.matrices.view * glm::mat4(1.0f);

        // Light source
        if (uiSettings.animateLight) {
            uiSettings.lightTimer += frameTimer * uiSettings.lightSpeed;
            uboVS.lightPos.x = sin(glm::radians(uiSettings.lightTimer * 360.0f)) * 15.0f;
            uboVS.lightPos.z = cos(glm::radians(uiSettings.lightTimer * 360.0f)) * 15.0f;
        };

        VK_CHECK_RESULT(uniformBufferVS.map());
        memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
        uniformBufferVS.unmap();
    }

    void draw() {
        VulkanExampleBase::prepareFrame();
        buildCommandBuffers();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        VulkanExampleBase::submitFrame();
    }

    void loadAssets() {
        models.models.loadFromFile(ASSET_PATH "models/vulkanscenemodels.dae", vertexLayout, 1.0f, vulkanDevice, queue);
        models.background.loadFromFile(ASSET_PATH "models/vulkanscenebackground.dae", vertexLayout, 1.0f, vulkanDevice, queue);
        models.logos.loadFromFile(ASSET_PATH "models/vulkanscenelogos.dae", vertexLayout, 1.0f, vulkanDevice, queue);
    }

    void prepareImGui() {
        imGui = new ImGUI(this);
        imGui->init((float)width, (float)height);
        imGui->initResources(renderPass, queue);
    }

    void prepare() {
        VulkanExampleBase::prepare();

        prepareUniformBuffers();
        setupLayoutsAndDescriptors();
        preparePipelines();
        prepareImGui();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;

        // Update imGui
        ImGuiIO& io = ImGui::GetIO();

        io.DisplaySize = ImVec2((float)width, (float)height);
        io.DeltaTime = frameTimer;

        io.MousePos = ImVec2(mousePos.x, mousePos.y);
        io.MouseDown[0] = mouseButtons.left;
        io.MouseDown[1] = mouseButtons.right;

        draw();

        if (uiSettings.animateLight)
            updateUniformBuffers();
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }

    virtual void mouseMoved(double x, double y, bool& handled) {
        ImGuiIO& io = ImGui::GetIO();
        handled = io.WantCaptureMouse;
    }
};

VULKAN_EXAMPLE_MAIN()
