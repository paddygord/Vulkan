/*
* Vulkan Model loader using ASSIMP
*
* Copyright(C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "vertex.hpp"
#include <vks/buffer.hpp>
#include <vks/context.hpp>


struct aiScene;
namespace Assimp {
class Importer;
};
namespace vkx { namespace model {

using VertexLayout = vkx::vertex::Layout;

/** @brief Used to parametrize model loading */
struct ModelCreateInfo {
    glm::vec3 center{ 0 };
    glm::vec3 scale{ 1 };
    glm::vec2 uvscale{ 1 };

    ModelCreateInfo() = default;

    ModelCreateInfo(const glm::vec3& scale, const glm::vec2& uvscale, const glm::vec3& center)
        : center(center)
        , scale(scale)
        , uvscale(uvscale) {}

    ModelCreateInfo(float scale, float uvscale, float center)
        : ModelCreateInfo(glm::vec3(scale), glm::vec2{ uvscale }, glm::vec3(center)) {}
};

struct Model {
    vk::Device device;
    vks::Buffer vertices;
    vks::Buffer indices;
    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
    VertexLayout layout;
    glm::vec3 scale{ 1.0f };
    glm::vec3 center{ 0.0f };
    glm::vec2 uvscale{ 1.0f };

    /** @brief Stores vertex and index base and counts for each part of a model */
    struct ModelPart {
        std::string name;
        uint32_t vertexBase;
        uint32_t vertexCount;
        uint32_t indexBase;
        uint32_t indexCount;
    };
    std::vector<ModelPart> parts;

    static const int defaultFlags;

    struct Dimension {
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
    void loadFromFile(const vks::Context& context,
                      const std::string& filename,
                      const VertexLayout& layout,
                      const ModelCreateInfo& createInfo,
                      int flags = defaultFlags);

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
    void loadFromFile(const vks::Context& context,
                      const std::string& filename,
                      const VertexLayout& layout,
                      float scale = 1.0f,
                      const int flags = defaultFlags) {
        loadFromFile(context, filename, layout, ModelCreateInfo{ scale, { 1.0f }, 0.0f }, flags);
    }

    virtual void onLoad(const vks::Context& context, Assimp::Importer& importer, const aiScene* pScene) {}

    virtual void appendVertex(std::vector<uint8_t>& outputBuffer, const aiScene* pScene, uint32_t meshIndex, uint32_t vertexIndex);

    template <typename T>
    void appendOutput(std::vector<uint8_t>& outputBuffer, const T& t) {
        auto offset = outputBuffer.size();
        auto copySize = sizeof(T);
        outputBuffer.resize(offset + copySize);
        memcpy(outputBuffer.data() + offset, &t, copySize);
    }

    template <typename T>
    void appendOutput(std::vector<uint8_t>& outputBuffer, const std::vector<T>& v) {
        auto offset = outputBuffer.size();
        auto copySize = v.size() * sizeof(T);
        outputBuffer.resize(offset + copySize);
        memcpy(outputBuffer.data() + offset, v.data(), copySize);
    }
};

}}  // namespace vks::model

