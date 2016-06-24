#pragma once

#include "vulkanExampleBase.h"

namespace vkx {

    class OffscreenExampleBase : public vkx::ExampleBase {
    protected:
        OffscreenExampleBase(bool enableValidation) : vkx::ExampleBase(enableValidation) {}

        struct Offscreen {
            bool active{ true };
            vk::RenderPass renderPass;
            vk::CommandBuffer cmdBuffer;
            vk::Semaphore renderComplete;
            vkx::Framebuffer framebuffer;
            vk::SubmitInfo submitInfo;
            vk::ImageUsageFlags attachmentUsage{ vk::ImageUsageFlagBits::eSampled };
            vk::ImageLayout colorFinalLayout{ vk::ImageLayout::eShaderReadOnlyOptimal  };
            vk::ImageLayout depthFinalLayout{ vk::ImageLayout::eUndefined };
        } offscreen;

        void destroyOffscreen() {
            offscreen.framebuffer.destroy();
            device.freeCommandBuffers(cmdPool, offscreen.cmdBuffer);
            device.destroyRenderPass(offscreen.renderPass);
            device.destroySemaphore(offscreen.renderComplete);
        }

        void prepareOffscreenSampler() {
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
            for (auto& color : offscreen.framebuffer.colors) {
                color.sampler = device.createSampler(sampler);
            }
        }

        virtual void prepareOffscreenRenderPass() {
            std::vector<vk::AttachmentDescription> attachments;
            std::vector<vk::AttachmentReference> colorAttachmentReferences;
            attachments.resize(offscreen.framebuffer.colorFormats.size());
            colorAttachmentReferences.resize(attachments.size());
            // Color attachment
            for (size_t i = 0; i < attachments.size(); ++i) {
                attachments[i].format = colorformat;
                attachments[i].loadOp = vk::AttachmentLoadOp::eClear;
                attachments[i].storeOp = vk::AttachmentStoreOp::eStore;
                attachments[i].initialLayout = vk::ImageLayout::eUndefined;
                attachments[i].finalLayout = offscreen.colorFinalLayout;
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
                depthAttachment.finalLayout = offscreen.depthFinalLayout;
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
                switch (offscreen.colorFinalLayout) {
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

            if (offscreen.renderPass) {
                device.destroyRenderPass(offscreen.renderPass);
            }

            vk::RenderPassCreateInfo renderPassInfo;
            renderPassInfo.attachmentCount = attachments.size();
            renderPassInfo.pAttachments = attachments.data();
            renderPassInfo.subpassCount = subpasses.size();
            renderPassInfo.pSubpasses = subpasses.data();
            renderPassInfo.dependencyCount = subpassDependencies.size();
            renderPassInfo.pDependencies = subpassDependencies.data();
            offscreen.renderPass = device.createRenderPass(renderPassInfo);
        }

        virtual void prepareOffscreen() {
            assert(!offscreen.framebuffer.colorFormats.empty());
            offscreen.cmdBuffer = device.allocateCommandBuffers(vkx::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1))[0];
            offscreen.renderComplete = device.createSemaphore(vk::SemaphoreCreateInfo());
            prepareOffscreenRenderPass();
            if (offscreen.framebuffer.size == glm::uvec2()) {
                throw std::runtime_error("Framebuffer size has not been set");
            }
            offscreen.framebuffer.depthFormat = vkx::getSupportedDepthFormat(physicalDevice);
            offscreen.framebuffer.create(*this, offscreen.renderPass, offscreen.attachmentUsage);
            offscreen.submitInfo = submitInfo;
            offscreen.submitInfo.commandBufferCount = 1;
            offscreen.submitInfo.pCommandBuffers = &offscreen.cmdBuffer;
            offscreen.submitInfo.waitSemaphoreCount = 1;
            offscreen.submitInfo.pWaitSemaphores = &semaphores.acquireComplete;
            offscreen.submitInfo.signalSemaphoreCount = 1;
            offscreen.submitInfo.pSignalSemaphores = &offscreen.renderComplete;

            prepareOffscreenSampler();
        }

        virtual void buildOffscreenCommandBuffer() = 0;

        void draw() override {
            prepareFrame();
            if (offscreen.active) {
                queue.submit(offscreen.submitInfo, VK_NULL_HANDLE);
            }
            drawCurrentCommandBuffer(offscreen.active ? offscreen.renderComplete : vk::Semaphore());
            submitFrame();
        }

        void prepare() override {
            ExampleBase::prepare();
            prepareOffscreen();
        }
    };
}

