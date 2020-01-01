#pragma once

#include <vulkan/vulkan.hpp>

namespace vkx { namespace vertex {

/** @brief Vertex layout components */
enum Component
{
    VERTEX_COMPONENT_POSITION = 0x0,
    VERTEX_COMPONENT_NORMAL = 0x1,
    VERTEX_COMPONENT_COLOR = 0x2,
    VERTEX_COMPONENT_UV = 0x3,
    VERTEX_COMPONENT_TANGENT = 0x4,
    VERTEX_COMPONENT_BITANGENT = 0x5,
    VERTEX_COMPONENT_DUMMY_FLOAT = 0x6,
    VERTEX_COMPONENT_DUMMY_INT = 0x7,
    VERTEX_COMPONENT_DUMMY_VEC4 = 0x8,
    VERTEX_COMPONENT_DUMMY_INT4 = 0x9,
    VERTEX_COMPONENT_DUMMY_UINT4 = 0xA,
};

/** @brief Stores vertex layout components for model loading and Vulkan vertex input and atribute bindings  */
struct Layout {
public:
    /** @brief Components used to generate vertices from */
    std::vector<Component> components;
    Layout() = default;
    Layout(std::vector<Component>&& components, uint32_t binding = 0)
        : components(std::move(components)) {}

    uint32_t componentIndex(Component component) const {
        for (size_t i = 0; i < components.size(); ++i) {
            if (components[i] == component) {
                return (uint32_t)i;
            }
        }
        return static_cast<uint32_t>(-1);
    }

    static vk::Format componentFormat(Component component) {
        switch (component) {
            case VERTEX_COMPONENT_UV:
                return vk::Format::eR32G32Sfloat;
            case VERTEX_COMPONENT_DUMMY_FLOAT:
                return vk::Format::eR32Sfloat;
            case VERTEX_COMPONENT_DUMMY_INT:
                return vk::Format::eR32Sint;
            case VERTEX_COMPONENT_DUMMY_VEC4:
                return vk::Format::eR32G32B32A32Sfloat;
            case VERTEX_COMPONENT_DUMMY_INT4:
                return vk::Format::eR32G32B32A32Sint;
            case VERTEX_COMPONENT_DUMMY_UINT4:
                return vk::Format::eR32G32B32A32Uint;
            default:
                return vk::Format::eR32G32B32Sfloat;
        }
    }

    static uint32_t componentSize(Component component) {
        switch (component) {
            case VERTEX_COMPONENT_UV:
                return 2 * sizeof(float);
            case VERTEX_COMPONENT_DUMMY_FLOAT:
                return sizeof(float);
            case VERTEX_COMPONENT_DUMMY_INT:
                return sizeof(int);
            case VERTEX_COMPONENT_DUMMY_VEC4:
                return 4 * sizeof(float);
            case VERTEX_COMPONENT_DUMMY_INT4:
                return 4 * sizeof(int32_t);
            case VERTEX_COMPONENT_DUMMY_UINT4:
                return 4 * sizeof(uint32_t);
            default:
                // All components except the ones listed above are made up of 3 floats
                return 3 * sizeof(float);
        }
    }

    uint32_t stride() const {
        uint32_t res = 0;
        for (auto& component : components) {
            res += componentSize(component);
        }
        return res;
    }

    uint32_t offset(uint32_t index) const {
        uint32_t res = 0;
        assert(index < components.size());
        for (uint32_t i = 0; i < index; ++i) {
            res += componentSize(components[i]);
        }
        return res;
    }

    vk::VertexInputBindingDescription generateBindingDescripton(uint32_t binding = 0, vk::VertexInputRate rate = vk::VertexInputRate::eVertex) {
        return vk::VertexInputBindingDescription{ binding, stride(), rate };
    }

    std::vector<vk::VertexInputAttributeDescription> genrerateAttributeDescriptions(uint32_t binding = 0, uint32_t attributeIndexOffset = 0) {
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
        auto componentsSize = components.size();
        attributeDescriptions.reserve(componentsSize);
        for (uint32_t i = 0; i < componentsSize; ++i) {
            const auto& component = components[i];
            const auto format = componentFormat(component);
            const auto offset = this->offset(i);
            attributeDescriptions.emplace_back(attributeIndexOffset + i, binding, format, offset);
        }
        return attributeDescriptions;
    }
};

}}  // namespace vkx::vertex