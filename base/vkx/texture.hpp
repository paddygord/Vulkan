/*
* Vulkan texture loader
*
* Copyright(C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <khrpp/vks/context.hpp>

namespace vkx { namespace texture {

/** @brief Vulkan texture base class */
class Texture : public vks::Image {
    using Parent = vks::Image;

public:
    uint32_t mipLevels;
    uint32_t layerCount{ 1 };
    vk::DescriptorImageInfo descriptor;
    vk::ImageLayout imageLayout;

    Texture& operator=(const vks::Image& image) {
        destroy();
        static_cast<vks::Image&>(*this) = image;
        return *this;
    }

    /** @brief Update image descriptor from current sampler, view and image layout */
    void updateDescriptor() {
        descriptor.sampler = sampler;
        descriptor.imageView = view;
        descriptor.imageLayout = imageLayout;
    }

    /** @brief Release all Vulkan resources held by this texture */
    void destroy() override { Parent::destroy(); }
};

/** @brief 2D texture */
class Texture2D : public Texture {
    using Parent = Texture;

public:
    /**
        * Load a 2D texture including all mip levels
        *
        * @param filename File to load (supports .ktx and .dds)
        * @param format Vulkan format of the image data stored in the file
        * @param device Vulkan device to create the texture on
        * @param copyQueue Queue used for the texture staging copy commands (must support transfer)
        * @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
        * @param (Optional) imageLayout Usage layout for the texture (defaults VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        * @param (Optional) forceLinear Force linear tiling (not advised, defaults to false)
        *
        */
    void loadFromFile(const vks::Context& context,
                      const std::string& filename,
                      vk::Format format = vk::Format::eR8G8B8A8Unorm,
                      vk::ImageUsageFlags imageUsageFlags = vk::ImageUsageFlagBits::eSampled,
                      vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                      bool forceLinear = false);

    /**
        * Creates a 2D texture from a buffer
        *
        * @param buffer Buffer containing texture data to upload
        * @param bufferSize Size of the buffer in machine units
        * @param width Width of the texture to create
        * @param height Height of the texture to create
        * @param format Vulkan format of the image data stored in the file
        * @param device Vulkan device to create the texture on
        * @param copyQueue Queue used for the texture staging copy commands (must support transfer)
        * @param (Optional) filter Texture filtering for the sampler (defaults to VK_FILTER_LINEAR)
        * @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
        * @param (Optional) imageLayout Usage layout for the texture (defaults VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        */
    void fromBuffer(const vks::Context& context,
                    void* buffer,
                    vk::DeviceSize bufferSize,
                    vk::Format format,
                    const vk::Extent2D& extent,
                    vk::Filter filter = vk::Filter::eLinear,
                    vk::ImageUsageFlags imageUsageFlags = vk::ImageUsageFlagBits::eSampled,
                    vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
};

/** @brief 2D array texture */
class Texture2DArray : public Texture {
public:
    /**
        * Load a 2D texture array including all mip levels
        *
        * @param filename File to load (supports .ktx and .dds)
        * @param format Vulkan format of the image data stored in the file
        * @param device Vulkan device to create the texture on
        * @param copyQueue Queue used for the texture staging copy commands (must support transfer)
        * @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
        * @param (Optional) imageLayout Usage layout for the texture (defaults VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        *
        */
    void loadFromFile(const vks::Context& context,
                      std::string filename,
                      vk::Format format,
                      vk::ImageUsageFlags imageUsageFlags = vk::ImageUsageFlagBits::eSampled,
                      vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
};

/** @brief Cube map texture */
class TextureCubeMap : public Texture {
public:
    /**
        * Load a cubemap texture including all mip levels from a single file
        *
        * @param filename File to load (supports .ktx and .dds)
        * @param format Vulkan format of the image data stored in the file
        * @param device Vulkan device to create the texture on
        * @param copyQueue Queue used for the texture staging copy commands (must support transfer)
        * @param (Optional) imageUsageFlags Usage flags for the texture's image (defaults to VK_IMAGE_USAGE_SAMPLED_BIT)
        * @param (Optional) imageLayout Usage layout for the texture (defaults VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        *
        */
    void loadFromFile(const vks::Context& context,
                      const std::string& filename,
                      vk::Format format,
                      vk::ImageUsageFlags imageUsageFlags = vk::ImageUsageFlagBits::eSampled,
                      vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);
};

}}  // namespace vkx::texture
