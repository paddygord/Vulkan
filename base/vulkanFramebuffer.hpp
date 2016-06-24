//
//  Created by Bradley Austin Davis on 2016/03/19
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once

#include <vector>
#include <algorithm>
#include <vulkan/vk_cpp.hpp>

#include "vulkanContext.hpp"

namespace vkx {
    struct Framebuffer {
        using Attachment = CreateImageResult;

        std::vector<vk::Format> colorFormats{ { vk::Format::eR8G8B8A8Unorm } };
        vk::Format depthFormat = vk::Format::eUndefined;
        vk::Device device;
        glm::uvec2 size{ 100, 100 };
        vk::Framebuffer frameBuffer;
        Attachment depth;
        std::vector<Attachment> colors;

        void destroy() {
            for (auto& color : colors) {
                color.destroy();
            }
            depth.destroy();
            if (frameBuffer) {
                device.destroyFramebuffer(frameBuffer);
                frameBuffer = vk::Framebuffer();
            }
        }

        // Prepare a new framebuffer for offscreen rendering
        // The contents of this framebuffer are then
        // blitted to our render target
        void create(const vkx::Context& context, const vk::RenderPass& renderPass, vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled) {
            device = context.device;
            destroy();

            colors.resize(colorFormats.size());

            // Color attachment
            vk::ImageCreateInfo image;
            image.imageType = vk::ImageType::e2D;
            image.extent.width = size.x;
            image.extent.height = size.y;
            image.extent.depth = 1;
            image.mipLevels = 1;
            image.arrayLayers = 1;
            image.samples = vk::SampleCountFlagBits::e1;
            image.tiling = vk::ImageTiling::eOptimal;
            // vk::Image of the framebuffer is blit source
            image.usage = vk::ImageUsageFlagBits::eColorAttachment | usage;

            vk::ImageViewCreateInfo colorImageView;
            colorImageView.viewType = vk::ImageViewType::e2D;
            colorImageView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            colorImageView.subresourceRange.levelCount = 1;
            colorImageView.subresourceRange.layerCount = 1;

            for (size_t i = 0; i < colorFormats.size(); ++i) {
                image.format = colorFormats[i];
                colors[i] = context.createImage(image, vk::MemoryPropertyFlagBits::eDeviceLocal);
                colorImageView.format = colorFormats[i];
                colorImageView.image = colors[i].image;
                colors[i].view = device.createImageView(colorImageView);
            }


            bool useDepth = depthFormat != vk::Format::eUndefined;
            // Depth stencil attachment
            if (useDepth) {
                image.format = depthFormat;
                image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
                depth = context.createImage(image, vk::MemoryPropertyFlagBits::eDeviceLocal);

                vk::ImageViewCreateInfo depthStencilView;
                depthStencilView.viewType = vk::ImageViewType::e2D;
                depthStencilView.format = depthFormat;
                depthStencilView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
                depthStencilView.subresourceRange.levelCount = 1;
                depthStencilView.subresourceRange.layerCount = 1;
                depthStencilView.image = depth.image;
                depth.view = device.createImageView(depthStencilView);

            }

            std::vector<vk::ImageView> attachments;
            attachments.resize(colors.size());
            for (size_t i = 0; i < colors.size(); ++i) {
                attachments[i] = colors[i].view;
            }
            if (useDepth) {
                attachments.push_back(depth.view);
            }

            vk::FramebufferCreateInfo fbufCreateInfo;
            fbufCreateInfo.renderPass = renderPass;
            fbufCreateInfo.attachmentCount = (uint32_t)attachments.size();
            fbufCreateInfo.pAttachments = attachments.data();
            fbufCreateInfo.width = size.x;
            fbufCreateInfo.height = size.y;
            fbufCreateInfo.layers = 1;
            frameBuffer = context.device.createFramebuffer(fbufCreateInfo);
        }
    };
}
