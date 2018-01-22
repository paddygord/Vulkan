#pragma once

#include <cstdint>
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <glm/glm.hpp>

namespace vks { namespace gltf {

namespace glb {
struct Header {
    static const uint32_t MAGIC = 0x46546C67;
    const uint32_t magic{ MAGIC };
    uint32_t version{ 2 };
    uint32_t length{ 0 };
};

struct ChunkHeader {
    enum class Type
    {
        JSON = 0x4E4F534A,
        BIN = 0x004E4942,
    };
    uint32_t length;
    Type type;
};

}  // namespace glb

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-extras
using DataBuffer = std::vector<uint8_t>;
//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-extension
using Extensions = std::unordered_map<std::string, DataBuffer>;

struct Scene;
struct Node;
struct Skin;
struct Camera;
struct Mesh;
struct Accessor;
struct Material;
struct BufferView;
struct Texture;
struct Sampler;
struct Animation;
struct Image;
struct Buffer;
struct Gltf;

using GltfPtr = std::shared_ptr<Gltf>;
using ScenePtr = std::shared_ptr<Scene>;
using NodePtr = std::shared_ptr<Node>;
using SkinPtr = std::shared_ptr<Skin>;
using CameraPtr = std::shared_ptr<Camera>;
using MeshPtr = std::shared_ptr<Mesh>;
using AccessorPtr = std::shared_ptr<Accessor>;
using MaterialPtr = std::shared_ptr<Material>;
using BufferViewPtr = std::shared_ptr<BufferView>;
using TexturePtr = std::shared_ptr<Texture>;
using SamplerPtr = std::shared_ptr<Sampler>;
using AnimationPtr = std::shared_ptr<Animation>;
using ImagePtr = std::shared_ptr<Image>;
using BufferPtr = std::shared_ptr<Buffer>;
using Mat4 = glm::mat4;
using Name = std::string;
using Color3 = glm::vec3;
using Color4 = glm::vec4;
using NodeList = std::list<NodePtr>;

static const size_t INVALID_INDEX = (size_t)-1;

// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#properties-reference

// Common properties for subtypes
struct Base {
    Name name;
    DataBuffer extras;
    Extensions extensions;
};

// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-scene
struct Scene : public Base {
    NodeList nodes;
};

// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-node
struct Node : public Base {
    CameraPtr camera;
    NodeList children;
    SkinPtr skin;
    Mat4 matrix;

    MeshPtr mesh;
    using Weights = std::vector<float>;
    Weights weights;
};

// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-texture
struct Texture : public Base {
    SamplerPtr sampler;
    ImagePtr source;
    ImagePtr& image{ source };
};

// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-image
struct Image : public Base {
    std::string uri;
    std::string mimeType;
    BufferViewPtr bufferView;
};

// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-buffer
struct Buffer : public Base {
    std::string uri;
    size_t byteLength;
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-bufferview
struct BufferView : public Base {
    enum class Target
    {
        Array = 34962,
        ElementArray = 34963,
    };

    BufferPtr buffer;
    uint32_t offset{ 0 };
    uint32_t length{ 0 };
    uint32_t stride{ 0 };
    Target target{ Target::Array };
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-camera
struct Camera : public Base {
    enum class Type
    {
        Orthographic = 0,
        Perspective = 1,
    };

    static Type typeFromString(const std::string& type) {
        static const std::string PERSPECTIVE{ "perspective" };
        static const std::string ORTHOGRAPHIC{ "orthographic" };
        if (type == PERSPECTIVE) {
            return Type::Perspective;
        } else if (type == ORTHOGRAPHIC) {
            return Type::Orthographic;
        }
        throw std::runtime_error("Failed to parse camera type");
    }

    Mat4 orthographic;
    Mat4 perspective;
    Type type{ Type::Orthographic };
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-accessor
struct Accessor : public Base {
    enum class ComponentType
    {
        Byte = 0x1400,
        UnsignedByte = 0x1401,
        Short = 0x1402,
        UnsignedShort = 0x1403,
        Int = 0x1404,
        UnsignedInt = 0x1405,
        Float = 0x1406,
    };

    static size_t componentSize(ComponentType type) {
        switch (type) {
            case ComponentType::Byte:
            case ComponentType::UnsignedByte:
                return 1;
            case ComponentType::Short:
            case ComponentType::UnsignedShort:
                return 2;
            case ComponentType::Int:
            case ComponentType::UnsignedInt:
            case ComponentType::Float:
                return 4;
        }
        throw std::runtime_error("Unknown component size " + std::to_string((uint32_t)type));
    };

    enum class Type
    {
        Scalar = 0,
        Vec2 = 1,
        Vec3 = 2,
        Vec4 = 3,
        Mat2 = 4,
        Mat3 = 5,
        Mat4 = 6,
    };

    static Type typeFromString(const std::string& type) {
        static const std::string SCALAR{ "SCALAR" };
        static const std::string VEC2{ "VEC2" };
        static const std::string VEC3{ "VEC3" };
        static const std::string VEC4{ "VEC4" };
        static const std::string MAT2{ "MAT2" };
        static const std::string MAT3{ "MAT3" };
        static const std::string MAT4{ "MAT4" };
        if (type == SCALAR) {
            return Type::Scalar;
        } else if (type == VEC2) {
            return Type::Vec2;
        } else if (type == VEC3) {
            return Type::Vec3;
        } else if (type == VEC4) {
            return Type::Vec4;
        } else if (type == MAT2) {
            return Type::Mat2;
        } else if (type == MAT3) {
            return Type::Mat3;
        } else if (type == MAT4) {
            return Type::Mat4;
        }
        throw std::runtime_error("Unknown type " + type);
    }

    static size_t typeCount(Type type) {
        switch (type) {
            case Type::Scalar:
                return 1;
            case Type::Vec2:
                return 2;
            case Type::Vec3:
                return 3;
            case Type::Vec4:
                return 4;
            case Type::Mat2:
                return 4;
            case Type::Mat3:
                return 9;
            case Type::Mat4:
                return 16;
        }
        throw std::runtime_error("Unknown type " + std::to_string((uint32_t)type));
    }

    size_t elementSize() const { return typeCount(type) * componentSize(componentType); }

    size_t size() const { return count * elementSize(); }

    BufferViewPtr bufferView;
    size_t byteOffset{ 0 };
    ComponentType componentType;
    bool normalized{ false };
    uint32_t count{ 0 };
    Type type{ Type::Scalar };
    std::vector<float> max;
    std::vector<float> min;
    // FIXME sparse
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-textureinfo
struct TextureInfo {
    TexturePtr texture;
    // This integer value is used to construct a string in the format TEXCOORD_<set index> which is a reference
    // to a key in mesh.primitives.attributes(e.g.A value of 0 corresponds to TEXCOORD_0).Mesh must have corresponding
    // texture coordinate attributes for the material to be applicable to it.
    uint32_t texCoord{ 0 };
    DataBuffer extras;
    Extensions extensions;
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-normaltextureinfo
struct NormalTextureInfo : public TextureInfo {
    float scale{ 1.0 };
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-occlusiontextureinfo
struct OcclusionTextureInfo : public TextureInfo {
    float strength{ 1.0 };
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-pbrmetallicroughness
struct PbrMetallicRoughness {
    Color4 baseColorFactor{ 1.0f };
    TextureInfo baseColorTexture;
    float metallicFactor{ 1.0f };
    float roughnessFactor{ 1.0f };
    TextureInfo metallicRoughnessTexture;
    DataBuffer extras;
    Extensions extensions;
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-material
struct Material : public Base {
    enum class AlphaMode {
        Opaque = 0,
        Mask = 1,
        Blend = 2,
    };

    static AlphaMode alphaModeFromString(const std::string& alphaMode);

    PbrMetallicRoughness pbrMetallicRoughness;
    NormalTextureInfo normalTexture;
    OcclusionTextureInfo occlusionTexture;
    TextureInfo emissiveTexture;
    Color3 emissiveFactor{ 0.0f };
    AlphaMode alphaMode{ AlphaMode::Opaque };
    float alphaCutoff{ 0.5f };
    bool doubleSided{ false };
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-sampler
struct Sampler : public Base {
    enum class MinFilter
    {
        Nearest = 0x2600,
        Linear = 0x2601,
    };

    enum class MagFilter
    {
        Nearest = 0x2600,
        Linear = 0x2601,
        NearestMipmapNearest = 0x2700,
        LinearMipmapNearest = 0x2701,
        NearestMipmapLinear = 0x2702,
        LinearMipmapLinear = 0x2703,
    };

    enum class WrapMode
    {
        Repeat = 0x2901,
        ClampToEdge = 33071,
        MirroredRepeat = 33648,
    };

    MinFilter minFilter{ MinFilter::Nearest };
    MagFilter magFilter{ MagFilter::Nearest };
    WrapMode wrapS{ WrapMode::Repeat };
    WrapMode wrapT{ WrapMode::Repeat };
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-primitive
struct Primitive {
    enum class Mode
    {
        Points = 0x0,
        Line = 0x1,
        LineLoop = 0x2,
        LineStrip = 0x3,
        Triangles = 0x4,
        TriangleStrip = 0x5,
        TriangleFan = 0x6,
    };

    using Attribute = std::pair<std::string, AccessorPtr>;
    using Attributes = std::vector<Attribute>;
    Attributes attributes;
    AccessorPtr indices;
    MaterialPtr material;
    Mode mode{ Mode::Triangles };
    // FIXME targets
    DataBuffer extras;
    Extensions extensions;
};

//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-mesh
struct Mesh : public Base {
    using Primitives = std::vector<Primitive>;
    using Weights = std::vector<float>;
    Primitives primitives;
    Weights weights;
};


// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-gltf
struct Gltf {
    static GltfPtr parse(const std::string& jsonString);

    std::string baseUri;
    // https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-asset
    struct Asset {
        std::string copyright;
        std::string generator;
        std::string version;
        std::string minVersion;
        DataBuffer extras;
        Extensions extensions;
    } asset;

    std::vector<std::string> extensionsUsed;
    std::vector<std::string> extensionsRequired;
    std::vector<AccessorPtr> accessors;
    std::vector<AnimationPtr> animations;
    std::vector<BufferPtr> buffers;
    std::vector<BufferViewPtr> bufferViews;
    std::vector<CameraPtr> cameras;
    std::vector<ImagePtr> images;
    std::vector<MaterialPtr> materials;
    std::vector<MeshPtr> meshes;
    std::vector<NodePtr> nodes;
    std::vector<SamplerPtr> samplers;
    ScenePtr scene;
    std::vector<ScenePtr> scenes;
    std::vector<SkinPtr> skins;
    std::vector<TexturePtr> textures;
    DataBuffer extras;
    Extensions extensions;
};


//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-animation
//https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#reference-skin

}}  // namespace vks::gltf
