#pragma once

#include "vulkanExampleBase.h"

namespace vkx {

    class OffscreenExampleBase : public vkx::ExampleBase {
    protected:
        OffscreenExampleBase(bool enableValidation) : vkx::ExampleBase(enableValidation), offscreen(*this) {}
        ~OffscreenExampleBase()  {
            offscreen.destroy();
        }

        struct Offscreen {
            const vkx::Context& context;
            bool active{ true };
            vk::RenderPass renderPass;
            vk::CommandBuffer cmdBuffer;
            vk::Semaphore renderComplete;
            glm::uvec2 size;
            std::vector<vk::Format> colorFormats{ { vk::Format::eB8G8R8A8Unorm } };
            // This value is chosen as an invalid default that signals that the code should pick a specific depth buffer
            // Alternative, you can set this to undefined to explicitly declare you want no depth buffer.
            vk::Format depthFormat = vk::Format::eR8Uscaled;
            std::vector<vkx::Framebuffer> framebuffers{ 1 };
            vk::ImageUsageFlags attachmentUsage{ vk::ImageUsageFlagBits::eSampled };
            vk::ImageLayout colorFinalLayout{ vk::ImageLayout::eShaderReadOnlyOptimal };
            vk::ImageLayout depthFinalLayout{ vk::ImageLayout::eUndefined };

            Offscreen(const vkx::Context& context) : context(context) {}

            void prepare() {
                assert(!colorFormats.empty());
                assert(size != glm::uvec2());

                if (depthFormat == vk::Format::eR8Uscaled) {
                    depthFormat = vkx::getSupportedDepthFormat(context.physicalDevice);
                }

                cmdBuffer = context.device.allocateCommandBuffers(vkx::commandBufferAllocateInfo(context.getCommandPool(), vk::CommandBufferLevel::ePrimary, 1))[0];
                renderComplete = context.device.createSemaphore(vk::SemaphoreCreateInfo());
                prepareRenderPass();

                for (auto& framebuffer : framebuffers) {
                    framebuffer.create(context, size, colorFormats, depthFormat, renderPass, attachmentUsage);
                }
                prepareSampler();
            }

            void destroy() {
                for (auto& framebuffer : framebuffers) {
                    framebuffer.destroy();
                }
                framebuffers.clear();
                context.device.freeCommandBuffers(context.getCommandPool(), cmdBuffer);
                context.device.destroyRenderPass(renderPass);
                context.device.destroySemaphore(renderComplete);
            }

        protected:
            void prepareSampler() {
                // Create sampler
                vk::SamplerCreateInfo sampler;
                sampler.magFilter = vk::Filter::eLinear;
                sampler.minFilter = vk::Filter::eLinear;
                sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
                sampler.addressModeU = vk::SamplerAddressMode::eClampToEdge;
                sampler.addressModeV = sampler.addressModeU;
                sampler.addressModeW = sampler.addressModeU;
                sampler.mipLodBias = 0.0f;
                sampler.maxAnisotropy = 0;
                sampler.compareOp = vk::CompareOp::eNever;
                sampler.minLod = 0.0f;
                sampler.maxLod = 0.0f;
                sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
                for (auto& framebuffer : framebuffers) {
                    for (auto& color : framebuffer.colors) {
                        color.sampler = context.device.createSampler(sampler);
                    }
                }
            }
            virtual void prepareRenderPass() {
                std::vector<vk::AttachmentDescription> attachments;
                std::vector<vk::AttachmentReference> colorAttachmentReferences;
                attachments.resize(colorFormats.size());
                colorAttachmentReferences.resize(attachments.size());
                // Color attachment
                for (size_t i = 0; i < attachments.size(); ++i) {
                    attachments[i].format = colorFormats[i];
                    attachments[i].loadOp = vk::AttachmentLoadOp::eClear;
                    attachments[i].storeOp = vk::AttachmentStoreOp::eStore;
                    attachments[i].initialLayout = vk::ImageLayout::eUndefined;
                    attachments[i].finalLayout = colorFinalLayout;
                    vk::AttachmentReference& attachmentReference = colorAttachmentReferences[i];
                    attachmentReference.attachment = i;
                    attachmentReference.layout = vk::ImageLayout::eColorAttachmentOptimal;
                }

                // Depth attachment
                vk::AttachmentReference depthAttachmentReference;
                {
                    vk::AttachmentDescription depthAttachment;
                    depthAttachment.format = depthFormat;
                    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
                    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
                    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
                    depthAttachment.finalLayout = depthFinalLayout;
                    attachments.push_back(depthAttachment);
                    depthAttachmentReference.attachment = attachments.size() - 1;
                    depthAttachmentReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                }

                std::vector<vk::SubpassDescription> subpasses;
                {
                    vk::SubpassDescription subpass;
                    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
                    subpass.pDepthStencilAttachment = &depthAttachmentReference;
                    subpass.colorAttachmentCount = colorAttachmentReferences.size();
                    subpass.pColorAttachments = colorAttachmentReferences.data();
                    subpasses.push_back(subpass);
                }


                std::vector<vk::SubpassDependency> subpassDependencies;
                {
                    vk::SubpassDependency dependency;
                    dependency.srcSubpass = 0;
                    dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
                    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;

                    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
                    switch (colorFinalLayout) {
                    case vk::ImageLayout::eShaderReadOnlyOptimal:
                        dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                        break;
                    case vk::ImageLayout::eTransferSrcOptimal:
                        dependency.dstAccessMask = vk::AccessFlagBits::eTransferRead;
                        break;
                    default:
                        throw std::runtime_error("Unhandled color final layout");
                    }
                    dependency.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
                    subpassDependencies.push_back(dependency);
                }

                if (renderPass) {
                    context.device.destroyRenderPass(renderPass);
                }

                vk::RenderPassCreateInfo renderPassInfo;
                renderPassInfo.attachmentCount = attachments.size();
                renderPassInfo.pAttachments = attachments.data();
                renderPassInfo.subpassCount = subpasses.size();
                renderPassInfo.pSubpasses = subpasses.data();
                renderPassInfo.dependencyCount = subpassDependencies.size();
                renderPassInfo.pDependencies = subpassDependencies.data();
                renderPass = context.device.createRenderPass(renderPassInfo);
            }
        } offscreen;

        virtual void buildOffscreenCommandBuffer() = 0;

        void draw() override {
            prepareFrame();
            if (offscreen.active) {
                submit(offscreen.cmdBuffer, { { semaphores.acquireComplete, vk::PipelineStageFlagBits::eBottomOfPipe } }, offscreen.renderComplete);
            }
            drawCurrentCommandBuffer(offscreen.active ? offscreen.renderComplete : vk::Semaphore());
            submitFrame();
        }

        void prepare() override {
            ExampleBase::prepare();
            offscreen.prepare();
        }
    };
}

