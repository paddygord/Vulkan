#pragma once

#include "vks/texture.hpp"

namespace vkx { namespace texture {
    using Texture2DPtr = vks::texture::Texture2DPtr;

    Texture2DPtr loadTexture2D(const vks::Context& context, const std::string& filename);

} }