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
        } offscreen;

        void destroyOffscreen() {
            offscreen.framebuffer.destroy();
            device.freeCommandBuffers(cmdPool, offscreen.cmdBuffer);
            device.destroyRenderPass(offscreen.renderPass);
            device.destroySemaphore(offscreen.renderComplete);
        }

        void prepareOffscreenSampler() {
            // Get device properites for the requested texture format
            vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(offscreen.framebuffer.colorFormat);

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
            offscreen.framebuffer.color.sampler = device.createSampler(sampler);
        }

        void prepareOffscreenRenderPass() {
            std::vector<vk::AttachmentDescription> attachments;
            std::vector<vk::AttachmentReference> attachmentReferences;
            attachments.resize(2);
            attachmentReferences.resize(2);

            // Color attachment
            attachments[0].format = colorformat;
            attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
            attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
            attachments[0].initialLayout = vk::ImageLayout::eUndefined;
            attachments[0].finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            // Depth attachment
            attachments[1].format = depthFormat;
            attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
            attachments[1].storeOp = vk::AttachmentStoreOp::eDontCare;
            attachments[1].initialLayout = vk::ImageLayout::eUndefined;
            attachments[1].finalLayout = vk::ImageLayout::eUndefined;

            vk::AttachmentReference& depthReference = attachmentReferences[0];
            depthReference.attachment = 1;
            depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

            vk::AttachmentReference& colorReference = attachmentReferences[1];
            colorReference.attachment = 0;
            colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

            std::vector<vk::SubpassDescription> subpasses;
            std::vector<vk::SubpassDependency> subpassDependencies;

            vk::SubpassDependency dependency;
            dependency.srcSubpass = 0;
            dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
            subpassDependencies.push_back(dependency);

            vk::SubpassDescription subpass;
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.pDepthStencilAttachment = attachmentReferences.data();
            subpass.colorAttachmentCount = attachmentReferences.size() - 1;
            subpass.pColorAttachments = attachmentReferences.data() + 1;
            subpasses.push_back(subpass);

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
            offscreen.cmdBuffer = device.allocateCommandBuffers(vkx::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1))[0];
            offscreen.renderComplete = device.createSemaphore(vk::SemaphoreCreateInfo());
            prepareOffscreenRenderPass();
            if (offscreen.framebuffer.size == glm::uvec2()) {
                throw std::runtime_error("Framebuffer size has not been set");
            }
            offscreen.framebuffer.colorFormat = vk::Format::eB8G8R8A8Unorm;
            offscreen.framebuffer.depthFormat = vkx::getSupportedDepthFormat(physicalDevice);
            offscreen.framebuffer.create(*this, offscreen.renderPass);
            offscreen.submitInfo = submitInfo;
            offscreen.submitInfo.pCommandBuffers = &offscreen.cmdBuffer;
            offscreen.submitInfo.pWaitSemaphores = &semaphores.presentComplete;
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

