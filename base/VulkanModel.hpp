/*
* Vulkan Model loader using ASSIMP
*
* Copyright(C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "common.hpp"
#include "vulkanContext.hpp"
//#include "VulkanBuffer.hpp"

namespace vks
{
    using Buffer = vkx::CreateBufferResult;
        
    /** @brief Vertex layout components */
    typedef enum Component {
        VERTEX_COMPONENT_POSITION = 0x0,
        VERTEX_COMPONENT_NORMAL = 0x1,
        VERTEX_COMPONENT_COLOR = 0x2,
        VERTEX_COMPONENT_UV = 0x3,
        VERTEX_COMPONENT_TANGENT = 0x4,
        VERTEX_COMPONENT_BITANGENT = 0x5,
        VERTEX_COMPONENT_DUMMY_FLOAT = 0x6,
        VERTEX_COMPONENT_DUMMY_VEC4 = 0x7
    } Component;

    /** @brief Stores vertex layout components for model loading and Vulkan vertex input and atribute bindings  */
    struct VertexLayout {
    public:
        /** @brief Components used to generate vertices from */
        std::vector<Component> components;

        VertexLayout(const std::vector<Component>& components) : components(components) {}
        VertexLayout(std::vector<Component>&& components) : components(std::move(components)) {}

        uint32_t stride()
        {
            uint32_t res = 0;
            for (auto& component : components)
            {
                switch (component)
                {
                case VERTEX_COMPONENT_UV:
                    res += 2 * sizeof(float);
                    break;
                case VERTEX_COMPONENT_DUMMY_FLOAT:
                    res += sizeof(float);
                    break;
                case VERTEX_COMPONENT_DUMMY_VEC4:
                    res += 4 * sizeof(float);
                    break;
                default:
                    // All components except the ones listed above are made up of 3 floats
                    res += 3 * sizeof(float);
                }
            }
            return res;
        }
    };

    /** @brief Used to parametrize model loading */
    struct ModelCreateInfo {
        glm::vec3 center;
        glm::vec3 scale;
        glm::vec2 uvscale;

        ModelCreateInfo() {};

        ModelCreateInfo(const glm::vec3& scale, const glm::vec2& uvscale, const glm::vec3& center) :
            center(center),
            scale(scale),
            uvscale(uvscale) {}

        ModelCreateInfo(float scale, float uvscale, float center) : 
            ModelCreateInfo(glm::vec3(scale), glm::vec2{ uvscale}, glm::vec3(center)) {}
    };

    struct Model {
        vk::Device device;
        Buffer vertices;
        Buffer indices;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;

        /** @brief Stores vertex and index base and counts for each part of a model */
        struct ModelPart {
            uint32_t vertexBase;
            uint32_t vertexCount;
            uint32_t indexBase;
            uint32_t indexCount;
        };
        std::vector<ModelPart> parts;

        static const int defaultFlags;

        struct Dimension
        {
            glm::vec3 min = glm::vec3(FLT_MAX);
            glm::vec3 max = glm::vec3(-FLT_MAX);
            glm::vec3 size;
        } dim;

        /** @brief Release all Vulkan resources of this model */
        void destroy() {		
            vertices.destroy();
            indices.destroy();
        }

        /**
        * Loads a 3D model from a file into Vulkan buffers
        *
        * @param device Pointer to the Vulkan device used to generated the vertex and index buffers on
        * @param filename File to load (must be a model format supported by ASSIMP)
        * @param layout Vertex layout components (position, normals, tangents, etc.)
        * @param createInfo MeshCreateInfo structure for load time settings like scale, center, etc.
        * @param copyQueue Queue used for the memory staging copy commands (must support transfer)
        * @param (Optional) flags ASSIMP model loading flags
        */
        bool loadFromFile(const vkx::Context& context, const std::string& filename, const vks::VertexLayout& layout, vk::Optional<vks::ModelCreateInfo> createInfo = nullptr, const int flags = defaultFlags);

        /**
        * Loads a 3D model from a file into Vulkan buffers
        *
        * @param device Pointer to the Vulkan device used to generated the vertex and index buffers on
        * @param filename File to load (must be a model format supported by ASSIMP)
        * @param layout Vertex layout components (position, normals, tangents, etc.)
        * @param scale Load time scene scale
        * @param copyQueue Queue used for the memory staging copy commands (must support transfer)
        * @param (Optional) flags ASSIMP model loading flags
        */
        bool loadFromFile(const vkx::Context& context, const std::string& filename, vks::VertexLayout layout, float scale, const int flags = defaultFlags);
    };
};