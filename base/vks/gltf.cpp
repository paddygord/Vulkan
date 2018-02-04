#include "gltf.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../json.hpp"

namespace vks { namespace gltf {

namespace tags {

static const std::string ACCESSORS{ "accessors" };
static const std::string ANIMATIONS{ "animations" };
static const std::string ASSET{ "asset" };
static const std::string BASE_COLOR_FACTOR{ "baseColorFactor" };
static const std::string BASE_COLOR_TEXTURE{ "baseColorTexture" };
static const std::string BUFFERS{ "buffers" };
static const std::string BUFFER_VIEW{ "bufferView" };
static const std::string BUFFER_VIEWS{ "bufferViews" };
static const std::string BUFFER{ "buffer" };
static const std::string BYTE_LENGTH{ "byteLength" };
static const std::string BYTE_OFFSET{ "byteOffset" };
static const std::string BYTE_STRIDE{ "byteStride" };
static const std::string CAMERAS{ "cameras" };
static const std::string COPYRIGHT{ "copyright" };
static const std::string EXTRAS{ "extras" };
static const std::string EXTENSIONS{ "extensions" };
static const std::string EXTENSIONS_USED{ "extensionsUsed" };
static const std::string EXTENSIONS_REQUIRED{ "extensionsRequired" };
static const std::string GENERATOR{ "generator" };
static const std::string IMAGES{ "images" };
static const std::string INDEX{ "index" };
static const std::string MAG_FILTER{ "magFilter" };
static const std::string MATERIALS{ "materials" };
static const std::string MESHES{ "meshes" };
static const std::string METALLIC_ROUGHNESS_TEXTURE{ "metallicRoughnessTexture" };
static const std::string METALIIC_FACTOR{ "metallicFactor" };
static const std::string MIN_FILTER{ "minFilter" };
static const std::string MIME_TYPE{ "mimeType" };
static const std::string MIN_VERSION{ "minVersion" };
static const std::string NAME{ "extras" };
static const std::string NODES{ "nodes" };
static const std::string PBR_METALLIC_ROUGHNESS{ "pbrMetallicRoughness" };
static const std::string ROUGHNESS_FACTOR{ "roughnessFactor" };
static const std::string SAMPLER{ "sampler" };
static const std::string SAMPLERS{ "samplers" };
static const std::string SCALE{ "scale" };
static const std::string STRENGTH{ "strength" };
static const std::string SCENE{ "scene" };
static const std::string SCENES{ "scenes" };
static const std::string SKINS{ "skins" };
static const std::string SOURCE{ "source" };
static const std::string TARGET{ "target" };
static const std::string TEX_COORD{ "texCoord" };
static const std::string TEXTURES{ "textures" };
static const std::string URI{ "uri" };
static const std::string VERSION{ "version" };
static const std::string WRAP_S{ "wrapS" };
static const std::string WRAP_T{ "wrapT" };

}  // namespace tags

using json = nlohmann::json;

vks::gltf::Material::AlphaMode vks::gltf::Material::alphaModeFromString(const std::string& alphaMode) {
    static const std::string OPAQUE{ "OPAQUE" };
    static const std::string MASK{ "MASK" };
    static const std::string BLEND{ "BLEND" };
    if (alphaMode == OPAQUE) {
        return AlphaMode::Opaque;
    } else if (alphaMode == MASK) {
        return AlphaMode::Mask;
    } else if (alphaMode == BLEND) {
        return AlphaMode::Blend;
    }
    throw std::runtime_error("Unknown alpha mode " + alphaMode);
}


struct GltfImpl : public Gltf {
    void parseJson(const json& rootNode) {
        using namespace tags;
        if (!rootNode.count(ASSET)) {
            throw std::runtime_error("Missing asset tag");
        }
        parseAsset(asset, rootNode[ASSET]);
        if (rootNode.count(EXTENSIONS_USED)) {
            parseArrayValues(extensionsUsed, rootNode[EXTENSIONS_USED]);
        }
        if (rootNode.count(EXTENSIONS_REQUIRED)) {
            parseArrayValues(extensionsUsed, rootNode[EXTENSIONS_REQUIRED]);
        }
        if (rootNode.count(BUFFERS)) {
            parseArray(buffers, rootNode[BUFFERS]);
        }
        if (rootNode.count(BUFFER_VIEWS)) {
            parseArray(bufferViews, rootNode[BUFFER_VIEWS]);
        }
        if (rootNode.count(IMAGES)) {
            parseArray(images, rootNode[IMAGES]);
        }
        if (rootNode.count(SAMPLERS)) {
            parseArray(samplers, rootNode[SAMPLERS]);
        }
        if (rootNode.count(TEXTURES)) {
            parseArray(textures, rootNode[TEXTURES]);
        }
        if (rootNode.count(MATERIALS)) {
            parseArray(materials, rootNode[MATERIALS]);
        }
        if (rootNode.count(ACCESSORS)) {
            parseArray(accessors, rootNode[ACCESSORS]);
        }
        if (rootNode.count(MESHES)) {
            parseArray(meshes, rootNode[MESHES]);
        }
        if (rootNode.count(CAMERAS)) {
            parseArray(cameras, rootNode[CAMERAS]);
        }
        if (rootNode.count(NODES)) {
            parseArray(nodes, rootNode[NODES]);
            resolveNodes(rootNode[NODES]);
        }
        if (rootNode.count(SKINS)) {
            throw std::runtime_error("Not implemented");
        }
        if (rootNode.count(SCENES)) {
            parseArray(scenes, rootNode[SCENES]);
        }
        if (rootNode.count(SCENE)) {
            size_t sceneIndex = rootNode[SCENE];
            assert(scenes.size() > sceneIndex);
            scene = scenes[sceneIndex];
        }
    }

    void resolveNodeChildren(Node& glnode, const json& childrenNode) {
        size_t childCount = childrenNode.size();
        glnode.children.resize(childCount);
        for (size_t j = 0; j < childCount; ++j) {
            size_t childIndex = childrenNode[j];
            assert(nodes.size() > childIndex);
            glnode.children.push_back(nodes[childIndex]);
        }
    }

    void resolveNodes(const json& arrayNode) {
        static const std::string CHILDREN{ "children" };
        auto count = arrayNode.size();
        for (size_t i = 0; i < count; ++i) {
            const auto& node = arrayNode[i];
            if (node.count(CHILDREN)) {
                resolveNodeChildren(*nodes[i], node[CHILDREN]);
            }
        }
    }

    static void parse(glm::vec4& v, const json& node) {
        assert(node.is_array() && node.size() == 4);
        v.x = node[0];
        v.y = node[1];
        v.z = node[2];
        v.w = node[3];
    }

    static void parse(glm::vec3& v, const json& node) {
        assert(node.is_array() && node.size() == 3);
        v.x = node[0];
        v.y = node[1];
        v.z = node[2];
    }

    static void parse(glm::quat& q, const json& node) {
        assert(node.is_array() && node.size() == 4);
        q.x = node[0];
        q.y = node[1];
        q.z = node[2];
        q.w = node[3];
    }

    static void parse(glm::mat4& m, const json& node) {
        assert(node.is_array() && node.size() == 16);
        std::vector<float> v;
        parse(v, node);
        memcpy(&(m[0][0]), v.data(), 16 * sizeof(float));
    }

    static void parse(Buffer& buffer, const json& bufferNode) {
        using namespace tags;
        assert(bufferNode.count(BYTE_LENGTH));
        buffer.byteLength = bufferNode[BYTE_LENGTH];
        if (bufferNode.count(URI)) {
            buffer.uri = bufferNode[URI];
        }
        parseExtras(buffer.extras, bufferNode);
        parseExtensions(buffer.extensions, bufferNode);
    }

    static void parse(std::vector<float>& v, const json& node) { parseArrayValues(v, node); }

    template <typename T>
    static void parseArrayValues(std::vector<T>& v, const json& node) {
        assert(node.is_array());
        auto count = node.size();
        v.resize(count);
        for (size_t i = 0; i < count; ++i) {
            v[i] = node[i];
        }
    }

    static void parseName(std::string& name, const json& node) {
        using namespace tags;
        if (node.count(NAME)) {
            name = node[NAME];
        }
    }

    static void parseExtras(DataBuffer& extras, const json& node) {
        using namespace tags;
        if (node.count(EXTRAS)) {
            // FIXME implement
        }
    }

    static void parseExtensions(Extensions& extras, const json& node) {
        using namespace tags;
        if (node.count(EXTENSIONS)) {
            // FIXME implement
        }
    }

    static void parseAsset(Asset& asset, const json& assetNode) {
        using namespace tags;
        asset.version = assetNode[VERSION];
        if (assetNode.count(COPYRIGHT)) {
            asset.copyright = assetNode[COPYRIGHT];
        }
        if (assetNode.count(GENERATOR)) {
            asset.generator = assetNode[GENERATOR];
        }
        if (assetNode.count(MIN_VERSION)) {
            asset.minVersion = assetNode[MIN_VERSION];
        }
        parseExtras(asset.extras, assetNode);
        parseExtensions(asset.extensions, assetNode);
    }

    template <typename T>
    void parseArray(std::vector<std::shared_ptr<T>>& output, const json& arrayNode) {
        assert(arrayNode.is_array());
        output.reserve(arrayNode.size());
        for (const auto& node : arrayNode) {
            // FIXME C++17 returns the reference from emplace_back
            output.emplace_back(std::make_shared<T>());
            parse(*output.back(), node);
        }
    }

    template <typename T>
    void parseArray(std::vector<T>& output, const json& arrayNode) {
        assert(arrayNode.is_array());
        auto count = arrayNode.size();
        output.resize(count);
        for (size_t i = 0; i < count; ++i) {
            parse(output[i], arrayNode[i]);
        }
    }

    void parse(BufferView& bufferView, const json& node) {
        using namespace tags;
        assert(node.count(BUFFER));
        assert(node.count(BYTE_LENGTH));
        size_t bufferIndex = node[BUFFER];
        assert(buffers.size() > bufferIndex);
        bufferView.buffer = buffers[bufferIndex];
        bufferView.length = node[BYTE_LENGTH];
        if (node.count(BYTE_OFFSET)) {
            bufferView.offset = node[BYTE_OFFSET];
        }
        if (node.count(BYTE_STRIDE)) {
            bufferView.stride = node[BYTE_STRIDE];
        }
        if (node.count(TARGET)) {
            bufferView.target = (BufferView::Target)(uint32_t)node[TARGET];
        }
        parseName(bufferView.name, node);
        parseExtras(bufferView.extras, node);
        parseExtensions(bufferView.extensions, node);
    }

    void parse(Primitive& primitive, const json& node) {
        static const std::string ATTRIBUTES{ "attributes" };
        static const std::string INDICES{ "indices" };
        static const std::string MATERIAL{ "material" };
        static const std::string MODE{ "mode" };
        static const std::string TARGETS{ "targets" };
        assert(node.count(ATTRIBUTES));
        auto attributes = node[ATTRIBUTES];
        for (json::iterator itr = attributes.begin(); itr != attributes.end(); ++itr) {
            std::string key = itr.key();
            size_t index = *itr;
            assert(accessors.size() > index);
            primitive.attributes.insert({ key, accessors[index] });
        }
        if (node.count(INDICES)) {
            size_t indicesIndex = node[INDICES];
            assert(accessors.size() > indicesIndex);
            primitive.indices = accessors[indicesIndex];
        }
        if (node.count(MATERIAL)) {
            size_t materialIndex = node[MATERIAL];
            assert(materials.size() > materialIndex);
            primitive.material = materials[materialIndex];
        }
        if (node.count(MODE)) {
            primitive.mode = (Primitive::Mode)(uint32_t)node[MODE];
        }
        if (node.count(TARGETS)) {
            throw std::runtime_error("Not implemented");
        }
        parseExtras(primitive.extras, node);
        parseExtensions(primitive.extensions, node);
    }

    void parse(Mesh& mesh, const json& node) {
        static const std::string PRIMITIVES{ "primitives" };
        static const std::string WEIGHTS{ "weights" };
        using namespace tags;
        assert(node.count(PRIMITIVES));
        parseArray(mesh.primitives, node[PRIMITIVES]);
        if (node.count(WEIGHTS)) {
            parse(mesh.weights, node[WEIGHTS]);
        }
        parseName(mesh.name, node);
        parseExtras(mesh.extras, node);
        parseExtensions(mesh.extensions, node);
    }

    void parse(Accessor& accessor, const json& node) {
        static const std::string COMPONENT_TYPE{ "componentType" };
        static const std::string COUNT{ "count" };
        static const std::string NORMALIZED{ "normalized" };
        static const std::string MAX{ "max" };
        static const std::string MIN{ "min" };
        static const std::string TYPE{ "type" };
        static const std::string SPARSE{ "sparse" };

        using namespace tags;
        accessor.componentType = (Accessor::ComponentType)(uint32_t)node[COMPONENT_TYPE];
        accessor.count = node[COUNT];
        accessor.type = Accessor::typeFromString(node[TYPE]);
        if (node.count(NORMALIZED)) {
            accessor.normalized = node[NORMALIZED];
        }
        if (node.count(BUFFER_VIEW)) {
            size_t bufferViewIndex = node[BUFFER_VIEW];
            assert(bufferViews.size() > bufferViewIndex);
            accessor.bufferView = bufferViews[bufferViewIndex];
        }
        if (node.count(BYTE_OFFSET)) {
            accessor.byteOffset = node[BYTE_OFFSET];
        }
        size_t count = Accessor::typeCount(accessor.type);

        if (node.count(MAX)) {
            assert(node[MAX].is_array());
            assert(node[MAX].size() == count);
            parse(accessor.max, node[MAX]);
        }
        if (node.count(MIN)) {
            assert(node[MIN].is_array());
            assert(node[MIN].size() == count);
            parse(accessor.min, node[MIN]);
        }
        if (node.count(SPARSE)) {
            // FIXME implement
            assert(false);
        }
        parseName(accessor.name, node);
        parseExtras(accessor.extras, node);
        parseExtensions(accessor.extensions, node);
    }

    void parse(Image& image, const json& node) {
        using namespace tags;
        assert((node.count(URI) != 0) ^ (node.count(BUFFER_VIEW) != 0));
        if (node.count(MIME_TYPE)) {
            image.mimeType = node[MIME_TYPE];
        }
        if (node.count(URI)) {
            image.uri = node[URI];
        }
        if (node.count(BUFFER_VIEW)) {
            size_t bufferViewIndex = node[BUFFER_VIEW];
            assert(bufferViews.size() > bufferViewIndex);
            image.bufferView = bufferViews[bufferViewIndex];
        }
        parseName(image.name, node);
        parseExtras(image.extras, node);
        parseExtensions(image.extensions, node);
    }

    void parse(Sampler& sampler, const json& node) {
        using namespace tags;
        if (node.count(MAG_FILTER)) {
            sampler.magFilter = (Sampler::MagFilter)(uint32_t)node[MAG_FILTER];
        }
        if (node.count(MIN_FILTER)) {
            sampler.minFilter = (Sampler::MinFilter)(uint32_t)node[MIN_FILTER];
        }
        if (node.count(WRAP_S)) {
            sampler.wrapS = (Sampler::WrapMode)(uint32_t)node[WRAP_S];
        }
        if (node.count(WRAP_T)) {
            sampler.wrapT = (Sampler::WrapMode)(uint32_t)node[WRAP_T];
        }
        parseName(sampler.name, node);
        parseExtras(sampler.extras, node);
        parseExtensions(sampler.extensions, node);
    }

    void parse(Texture& texture, const json& node) {
        using namespace tags;
        if (node.count(SAMPLER)) {
            size_t samplerIndex = node[SAMPLER];
            assert(samplers.size() > samplerIndex);
            texture.sampler = samplers[samplerIndex];
        }
        if (node.count(SOURCE)) {
            size_t imageIndex = node[SOURCE];
            assert(images.size() > imageIndex);
            texture.source = images[imageIndex];
        }
        parseName(texture.name, node);
        parseExtras(texture.extras, node);
        parseExtensions(texture.extensions, node);
    }

    void parse(TextureInfo& textureInfo, const json& node) {
        using namespace tags;
        size_t textureIndex = node[INDEX];
        assert(textures.size() > textureIndex);
        textureInfo.texture = textures[textureIndex];
        if (node.count(TEX_COORD)) {
            textureInfo.texCoord = node[TEX_COORD];
        }
        parseExtras(textureInfo.extras, node);
        parseExtensions(textureInfo.extensions, node);
    }

    void parse(NormalTextureInfo& textureInfo, const json& node) {
        using namespace tags;
        parse((TextureInfo&)textureInfo, node);
        if (node.count(SCALE)) {
            textureInfo.scale = node[SCALE];
        }
    }

    void parse(OcclusionTextureInfo& textureInfo, const json& node) {
        using namespace tags;
        parse((TextureInfo&)textureInfo, node);
        if (node.count(STRENGTH)) {
            textureInfo.strength = node[STRENGTH];
        }
    }

    void parse(PbrMetallicRoughness& pbrMetallicRoughness, const json& node) {
        using namespace tags;
        if (node.count(BASE_COLOR_FACTOR)) {
            parse(pbrMetallicRoughness.baseColorFactor, node[BASE_COLOR_FACTOR]);
        }
        if (node.count(BASE_COLOR_TEXTURE)) {
            parse(pbrMetallicRoughness.baseColorTexture, node[BASE_COLOR_TEXTURE]);
        }
        if (node.count(METALLIC_ROUGHNESS_TEXTURE)) {
            parse(pbrMetallicRoughness.metallicRoughnessTexture, node[METALLIC_ROUGHNESS_TEXTURE]);
        }
        if (node.count(METALIIC_FACTOR)) {
            pbrMetallicRoughness.metallicFactor = node[METALIIC_FACTOR];
        }
        if (node.count(ROUGHNESS_FACTOR)) {
            pbrMetallicRoughness.metallicFactor = node[ROUGHNESS_FACTOR];
        }
        parseExtras(pbrMetallicRoughness.extras, node);
        parseExtensions(pbrMetallicRoughness.extensions, node);
    }

    void parse(Material& material, const json& node) {
        static const std::string NORMAL_TEXTURE{ "normalTexture" };
        static const std::string OCCLUSION_TEXTURE{ "occlusionTexture" };
        static const std::string EMISSIVE_TEXTURE{ "emissiveTexture" };
        static const std::string EMISSIVE_FACTOR{ "emissiveFactor" };
        static const std::string ALPHA_MODE{ "alphaMode" };
        static const std::string ALPHA_CUTOFF{ "alphaCutoff" };
        static const std::string DOUBLE_SIDED{ "doubleSided" };

        using namespace tags;
        if (node.count(PBR_METALLIC_ROUGHNESS)) {
            parse(material.pbrMetallicRoughness, node[PBR_METALLIC_ROUGHNESS]);
        }
        if (node.count(NORMAL_TEXTURE)) {
            parse(material.normalTexture, node[NORMAL_TEXTURE]);
        }
        if (node.count(OCCLUSION_TEXTURE)) {
            parse(material.occlusionTexture, node[OCCLUSION_TEXTURE]);
        }
        if (node.count(EMISSIVE_TEXTURE)) {
            parse(material.emissiveTexture, node[EMISSIVE_TEXTURE]);
        }
        if (node.count(EMISSIVE_FACTOR)) {
            parse(material.emissiveFactor, node[EMISSIVE_FACTOR]);
        }
        if (node.count(ALPHA_MODE)) {
            material.alphaMode = Material::alphaModeFromString(node[ALPHA_MODE]);
        }
        if (node.count(ALPHA_CUTOFF)) {
            material.alphaCutoff = node[ALPHA_CUTOFF];
        }
        if (node.count(DOUBLE_SIDED)) {
            material.doubleSided = node[DOUBLE_SIDED];
        }
        parseName(material.name, node);
        parseExtras(material.extras, node);
        parseExtensions(material.extensions, node);
    }

    void parse(Camera& camera, const json& node) {
        static const std::string ORTHOGRAPHIC{ "orthographic" };
        static const std::string PERSPECTIVE{ "perspective" };
        static const std::string TYPE{ "type" };
        if (node.count(ORTHOGRAPHIC)) {
            parse(camera.orthographic, node[ORTHOGRAPHIC]);
        }
        if (node.count(PERSPECTIVE)) {
            parse(camera.perspective, node[PERSPECTIVE]);
        }
        if (node.count(TYPE)) {
            camera.type = Camera::typeFromString(node[TYPE]);
        }
        parseName(camera.name, node);
        parseExtras(camera.extras, node);
        parseExtensions(camera.extensions, node);
    }

    void parse(Node& glnode, const json& node) {
        static const std::string CAMERA{ "camera" };
        static const std::string SKIN{ "skin" };
        static const std::string MATRIX{ "matrix" };
        static const std::string MESH{ "mesh" };
        static const std::string ROTATION{ "rotation" };
        static const std::string SCALE{ "scale" };
        static const std::string TRANSLATION{ "translation" };
        static const std::string WEIGHTS{ "weights" };
        if (node.count(CAMERA)) {
            size_t cameraIndex = node[CAMERA];
            assert(cameras.size() > cameraIndex);
            glnode.camera = cameras[cameraIndex];
        }
        // FIXME skin
        if (node.count(MATRIX)) {
            parse(glnode.matrix, node[MATRIX]);
        }
        if ((node.count(ROTATION) != 0) || (node.count(SCALE) != 0) || (node.count(TRANSLATION) != 0)) {
            glm::vec3 scale{ 1.0f };
            glm::quat rotation;
            glm::vec3 translation{ 0.0f };
            if (node.count(SCALE)) {
                parse(scale, node[SCALE]);
            }
            if (node.count(ROTATION)) {
                parse(rotation, node[ROTATION]);
            }
            if (node.count(TRANSLATION)) {
                parse(translation, node[TRANSLATION]);
            }
            glnode.matrix = glm::translate(glm::mat4(), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(), scale);
        }
        if (node.count(MESH)) {
            size_t meshIndex = node[MESH];
            assert(meshes.size() > meshIndex);
            glnode.mesh = meshes[meshIndex];
        }
        if (node.count(WEIGHTS)) {
            parse(glnode.weights, node[WEIGHTS]);
        }
        parseName(glnode.name, node);
        parseExtras(glnode.extras, node);
        parseExtensions(glnode.extensions, node);
    }

    void parse(Scene& scene, const json& node) {
        static const std::string NODES{ "nodes" };
        if (node.count(NODES)) {
            std::vector<size_t> indices;
            parseArrayValues(indices, node[NODES]);
            for (auto nodeIndex : indices) {
                assert(nodes.size() > nodeIndex);
                scene.nodes.push_back(nodes[nodeIndex]);
            }
        }
        parseName(scene.name, node);
        parseExtras(scene.extras, node);
        parseExtensions(scene.extensions, node);
    }
};

GltfPtr Gltf::parse(const std::string& jsonString) {
    using namespace tags;
    std::shared_ptr<GltfImpl> result = std::make_shared<GltfImpl>();
    result->parseJson(json::parse(jsonString));
    return result;
}

}}  // namespace vks::gltf
