/*
* Assorted commonly used Vulkan helper functions
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <glm/glm.hpp>

#include <vulkan/vulkan.hpp>

namespace vks { namespace util {

inline vk::ColorComponentFlags fullColorWriteMask() {
    return vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
}

inline vk::Viewport viewport(
    float width,
    float height,
    float minDepth = 0,
    float maxDepth = 1) {
    vk::Viewport viewport;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = minDepth;
    viewport.maxDepth = maxDepth;
    return viewport;
}

inline vk::Viewport viewport(
    const glm::uvec2& size,
    float minDepth = 0,
    float maxDepth = 1) {
    return viewport((float)size.x, (float)size.y, minDepth, maxDepth);
}

inline vk::Viewport viewport(
    const vk::Extent2D& size,
    float minDepth = 0,
    float maxDepth = 1) {
    return viewport((float)size.width, (float)size.height, minDepth, maxDepth);
}

inline vk::Rect2D rect2D(
    uint32_t width,
    uint32_t height,
    int32_t offsetX = 0,
    int32_t offsetY = 0) {
    vk::Rect2D rect2D;
    rect2D.extent.width = width;
    rect2D.extent.height = height;
    rect2D.offset.x = offsetX;
    rect2D.offset.y = offsetY;
    return rect2D;
}

inline vk::Rect2D rect2D(
    const glm::uvec2& size,
    const glm::ivec2& offset = glm::ivec2(0))  {
    return rect2D(size.x, size.y, offset.x, offset.y);
}

inline vk::Rect2D rect2D(
    const vk::Extent2D& size,
    const vk::Offset2D& offset = vk::Offset2D()) {
    return rect2D(size.width, size.height, offset.x, offset.y);
}

} } // namespace vks::util
