#pragma once

#include "context.hpp"
#include "model.hpp"
#include "shaders.hpp"

namespace vks {
    namespace pipelines {
        struct PipelineRasterizationStateCreateInfo : public vk::PipelineRasterizationStateCreateInfo {
            using Parent = vk::PipelineRasterizationStateCreateInfo;
            PipelineRasterizationStateCreateInfo() {
                lineWidth = 1.0f;
                cullMode = vk::CullModeFlagBits::eBack;
            }
        };


        struct PipelineInputAssemblyStateCreateInfo : public vk::PipelineInputAssemblyStateCreateInfo {
            PipelineInputAssemblyStateCreateInfo() {
                topology = vk::PrimitiveTopology::eTriangleList;
            }
        };

        struct PipelineColorBlendAttachmentState : public vk::PipelineColorBlendAttachmentState {
            PipelineColorBlendAttachmentState() {
                colorWriteMask = vks::util::fullColorWriteMask();
            }
            operator const vk::PipelineColorBlendAttachmentState&() const { return *this; };
        };

        struct PipelineColorBlendStateCreateInfo : public vk::PipelineColorBlendStateCreateInfo {
            // Default to a single color attachment state with no blending
            std::vector<PipelineColorBlendAttachmentState> blendAttachmentStates{ PipelineColorBlendAttachmentState() };
            
            operator const vk::PipelineColorBlendStateCreateInfo&() {
                update();
                return *this;
            };

            void update() {
                this->attachmentCount = (uint32_t)blendAttachmentStates.size();
                this->pAttachments = blendAttachmentStates.data();
            }
        };

        struct PipelineDynamicStateCreateInfo : public vk::PipelineDynamicStateCreateInfo {
            std::vector<vk::DynamicState> dynamicStateEnables;

            PipelineDynamicStateCreateInfo() {
                dynamicStateEnables = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            }

            void update() {
                this->dynamicStateCount = (uint32_t)dynamicStateEnables.size();
                this->pDynamicStates = dynamicStateEnables.data();
            }
        };

        struct PipelineVertexInputStateCreateInfo : public vk::PipelineVertexInputStateCreateInfo {
            std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
            std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;

            void appendVertexLayout(const vks::model::VertexLayout& vertexLayout, uint32_t binding = 0, vk::VertexInputRate rate = vk::VertexInputRate::eVertex) {
                bindingDescriptions.push_back({binding, vertexLayout.stride(), rate });
                auto componentsSize = vertexLayout.components.size();
                attributeDescriptions.reserve(attributeDescriptions.size() + componentsSize);
                auto attributeIndexOffset = (uint32_t)attributeDescriptions.size();
                for (uint32_t i = 0; i < componentsSize; ++i) {
                    const auto& component = vertexLayout.components[i];
                    const auto format = vertexLayout.componentFormat(component);
                    const auto offset = vertexLayout.offset(i);
                    attributeDescriptions.push_back(vk::VertexInputAttributeDescription{ attributeIndexOffset + i, binding, format, offset });
                }
            }

            void update() {
                vertexAttributeDescriptionCount = (uint32_t)attributeDescriptions.size();
                vertexBindingDescriptionCount = (uint32_t)bindingDescriptions.size();
                pVertexBindingDescriptions = bindingDescriptions.data();
                pVertexAttributeDescriptions = attributeDescriptions.data();
            }
        };

        struct PipelineViewportStateCreateInfo : public vk::PipelineViewportStateCreateInfo {
            std::vector<vk::Viewport> viewports;
            std::vector<vk::Rect2D> scissors;

            void update() {
                if (viewports.empty()) {
                    viewportCount = 1;
                    pViewports = nullptr;
                } else {
                    viewportCount = (uint32_t)viewports.size();
                    pViewports = viewports.data();
                }

                if (scissors.empty()) {
                    scissorCount = 1;
                    pScissors = 0;
                } else {
                    scissorCount = (uint32_t)scissors.size();
                    pScissors = scissors.data();
                }
            }
        };

        struct PipelineDepthStencilStateCreateInfo : public vk::PipelineDepthStencilStateCreateInfo {
            PipelineDepthStencilStateCreateInfo(bool depthEnable = true) {
                if (depthEnable) {
                    depthTestEnable = VK_TRUE;
                    depthWriteEnable = VK_TRUE;
                    depthCompareOp = vk::CompareOp::eLessOrEqual;
                }
            }
        };
        struct GraphicsPipelineBuilder {
            GraphicsPipelineBuilder(const vk::Device& device, const vk::PipelineLayout layout, const vk::RenderPass& renderPass) :
                device(device) {
                pipelineCreateInfo.layout = layout;
                pipelineCreateInfo.renderPass = renderPass;
                pipelineCreateInfo.pRasterizationState = &rasterizationState;
                pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
                pipelineCreateInfo.pColorBlendState = &colorBlendState;
                pipelineCreateInfo.pMultisampleState = &multisampleState;
                pipelineCreateInfo.pViewportState = &viewportState;
                pipelineCreateInfo.pDepthStencilState = &depthStencilState;
                pipelineCreateInfo.pDynamicState = &dynamicState;
                pipelineCreateInfo.pVertexInputState = &vertexInputState;
            }

            ~GraphicsPipelineBuilder() {
                destroyShaderModules();
            }

            const vk::Device& device;
            vk::RenderPass& renderPass { pipelineCreateInfo.renderPass };
            vk::PipelineLayout& layout { pipelineCreateInfo.layout };
            PipelineInputAssemblyStateCreateInfo inputAssemblyState;
            PipelineRasterizationStateCreateInfo rasterizationState;
            vk::PipelineMultisampleStateCreateInfo multisampleState;
            PipelineDepthStencilStateCreateInfo depthStencilState;
            PipelineViewportStateCreateInfo viewportState;
            PipelineDynamicStateCreateInfo dynamicState;
            PipelineColorBlendStateCreateInfo colorBlendState;
            PipelineVertexInputStateCreateInfo vertexInputState;
            std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

            vk::GraphicsPipelineCreateInfo pipelineCreateInfo;

            void update() {
                pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
                pipelineCreateInfo.pStages = shaderStages.data();
                dynamicState.update();
                colorBlendState.update();
                vertexInputState.update();
                viewportState.update();
            }

            void destroyShaderModules() {
                for (const auto shaderStage : shaderStages) {
                    device.destroyShaderModule(shaderStage.module);
                }
                shaderStages.clear();
            }

            // Load a SPIR-V shader
            vk::PipelineShaderStageCreateInfo& loadShader(const std::string& fileName, vk::ShaderStageFlagBits stage, const char* entryPoint = "main") {
                vk::PipelineShaderStageCreateInfo shaderStage = vks::shaders::loadShader(device, fileName, stage, entryPoint);
                shaderStages.push_back(shaderStage);
                return shaderStages.back();
            }

            vk::Pipeline create(const vk::PipelineCache& cache) {
                update();
                return device.createGraphicsPipeline(cache, pipelineCreateInfo);
            }
        };
    }
} // namespace vks::pipelines
