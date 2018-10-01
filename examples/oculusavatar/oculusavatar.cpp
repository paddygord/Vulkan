/*
* Vulkan Example - OpenGL interoperability example
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>
#include <vks/texture.hpp>
#include <unordered_map>
#include <ktx/ktx.hpp>
#include <Windows.h>
#include <filesystem>

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION

#include <C:/git/tinygltf/tiny_gltf.h>
#include <D:/OVRAvatarSDK/include/OVR_Avatar.h>
#pragma comment(lib, "D:/OVRAvatarSDK/Windows/libovravatar.lib")

static const std::string ASSET_PATH{ "D:/ovrAvatar/" };

namespace tinygltf {

class Builder {
public:
    Model model;
    using ByteVector = std::vector<uint8_t>;

    static bool alignToSize(size_t size, ByteVector& vector) {
        auto vsize = vector.size();
        auto mod = vsize % size;
        if (0 == mod) {
            return false;
        }
        auto addSize = size - mod;
        vector.resize(vsize + addSize, 0);
        return true;
    }

    template <typename T>
    uint32_t appendVertexComponents(uint32_t type, uint32_t componentType, const T* data, size_t sourceCount = 1, size_t sourceOffset = 0) {
        return appendBuffer(TINYGLTF_TARGET_ARRAY_BUFFER, type, componentType, data, sizeof(T), sourceCount, sourceOffset);
    }

    uint32_t appendIndices(uint32_t componentType, const void* data, size_t sourceCount = 1, size_t sourceOffset = 0) {
        // Indices always scalar, uses the element array buffer target, assume tightly packed
        return appendBuffer(TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER, TINYGLTF_TYPE_SCALAR, componentType, data, 0, sourceCount, sourceOffset);
    }

    uint32_t appendBuffer(uint32_t target,
                          uint32_t type,
                          uint32_t componentType,
                          const void* sourceData,
                          size_t sourceStride = 0,
                          size_t sourceCount = 1,
                          size_t sourceOffset = 0) {
        // Allocate the accessor
        uint32_t accessorIndex = static_cast<uint32_t>(model.accessors.size());
        model.accessors.push_back({});
        auto& accessor = model.accessors.back();
        accessor.byteOffset = 0;
        accessor.normalized = false;
        accessor.count = sourceCount;
        accessor.type = type;
        accessor.componentType = componentType;

        // Allocate the buffer view
        accessor.bufferView = static_cast<uint32_t>(model.bufferViews.size());
        model.bufferViews.push_back({});
        auto& bufferView = model.bufferViews.back();

        // Append to the current buffer
        if (model.buffers.empty()) {
            model.buffers.push_back({});
        }
        bufferView.buffer = static_cast<uint32_t>(model.buffers.size() - 1);
        bufferView.target = target;

        auto& buffer = model.buffers.back();
        // Copy the data into the buffer
        {
            auto componentSize = GetComponentSizeInBytes(componentType);
            auto typeSize = GetTypeSizeInBytes(type);
            if (componentSize < 0 || typeSize < 0) {
                throw std::runtime_error("Bad type or component");
            }
            auto targetStride = componentSize * typeSize;
            //assert(targetStride % 4 == 0);
            auto& data = buffer.data;
            alignToSize(targetStride, data);
            bufferView.byteOffset = data.size();
            bufferView.byteLength = targetStride * sourceCount;
            data.resize(bufferView.byteOffset + bufferView.byteLength);
            uint8_t* destData = data.data() + bufferView.byteOffset;
            const uint8_t* sourceDataBytes = reinterpret_cast<const uint8_t*>(sourceData);
            sourceDataBytes += sourceOffset;
            if (sourceStride == 0 || sourceStride == targetStride) {
                // Tightly packed
                memcpy(destData, sourceDataBytes, bufferView.byteLength);
            } else {
                for (size_t i = 0; i < sourceCount; ++i) {
                    memcpy(destData, sourceDataBytes, targetStride);
                    destData += targetStride;
                    sourceDataBytes += sourceStride;
                }
            }
        }
        return accessorIndex;
    }
};
}  // namespace tinygltf

namespace ovr {

std::vector<uint8_t> ovrTextureToKtx(const ovrAvatarTextureAssetData* textureData) {
    using namespace khronos;
    using namespace khronos::ktx;

    Header header;
    header.set2D(textureData->sizeX, textureData->sizeY);
    header.numberOfMipmapLevels = textureData->mipCount;
    switch (textureData->format) {
        case ovrAvatarTextureFormat_RGB24:
            header.setUncompressed(GLType::UNSIGNED_BYTE, 1, GLFormat::RGB, GLInternalFormat::RGB8, GLBaseInternalFormat::RGB);
            break;

        case ovrAvatarTextureFormat_DXT1:
            header.setCompressed(GLInternalFormat::COMPRESSED_RGB_S3TC_DXT1_EXT, GLBaseInternalFormat::RGB);
            break;

        case ovrAvatarTextureFormat_DXT5:
            header.setCompressed(GLInternalFormat::COMPRESSED_RGBA_S3TC_DXT5_EXT, GLBaseInternalFormat::RGBA);
            break;

        default:
            throw std::runtime_error("not implemented");
    }

    // Figure out the layout of the KTX version of the data
    auto ktxDescriptor = ktx::KTXDescriptor(header);
    std::vector<uint8_t> output;
    output.resize(ktxDescriptor.evalStorageSize());
    memcpy(output.data(), &header, sizeof(Header));
    auto imageDataOffset = ktxDescriptor.getImagesOffset();
    size_t sourceOffset = 0;
    for (uint16_t mip = 0; mip < textureData->mipCount; ++mip) {
        uint32_t sourceImageSize = (uint32_t)header.evalUnalignedFaceSize(mip);
        uint32_t targetImageSize = (uint32_t)header.evalFaceSize(mip);
        const auto& targetDescriptor = ktxDescriptor.images[mip];
        auto targetOffset = targetDescriptor._imageOffset + imageDataOffset;
        auto* imageSizePtr = output.data() + targetOffset;
        auto* imageDataPtr = imageSizePtr + sizeof(uint32_t);
        memcpy(imageSizePtr, &targetImageSize, sizeof(uint32_t));
        if (sourceImageSize == targetImageSize) {
            memcpy(imageDataPtr, textureData->textureData + sourceOffset, sourceImageSize);
        } else {
            // If the source and target image size don't match, it's because of the KTX alignment
            // requirements.  Everything needs to align to 4 bytes, *including* the image rows
            // so if you have an RGB texture, the 2x2 mip will have a row size of 8, not 6, but the
            // incoming data is tightly packed, so we need to do a row-wise copy from the source data
            // to the destination.
            // Should only happen for uncompressed textures where the per-pixel size isn't a multiple
            // of 4, i.e. only uncompressed RGB.
            assert(header.glFormat != 0);
            auto height = header.evalPixelOrBlockHeight(mip);
            auto sourceRowSize = header.evalUnalignedRowSize(mip);
            auto targetRowSize = header.evalRowSize(mip);
            for (size_t row = 0; row < height; ++row) {
                auto targetRowOffset = targetRowSize * row;
                auto sourceRowOffset = sourceRowSize * row;
                memcpy(imageDataPtr + targetRowOffset, textureData->textureData + sourceOffset + sourceRowOffset, sourceRowSize);
            }
        }
        sourceOffset += sourceImageSize;
    }

    return output;
}

}  // namespace ovr

class OculusAvatarExample {
public:
    static void log(const char* str) {
        OutputDebugStringA(str);
        OutputDebugStringA("\n");
    }

    static void log(const std::string& str) { log(str.c_str()); }

    ovrAvatar* avatar{ nullptr };
    std::unordered_map<ovrAvatarAssetID, ovrAvatarAsset*> assets;
    std::unordered_map<uint64_t, ovrAvatarSpecification*> avatarSpecs;

    void onSkinnnedMeshRenderPart(const ovrAvatarRenderPart_SkinnedMeshRender* renderPart) {
        log("Got skinned mesh");
        log("");
    }

    void onSkinnnedMeshRenderPartPBS(const ovrAvatarRenderPart_SkinnedMeshRenderPBS* renderPart) {
        log("Got skinned mesh PBS");
        log("");
    }

    void onSkinnnedMeshRenderPartPBSv2(const ovrAvatarRenderPart_SkinnedMeshRenderPBS_V2* renderPart) {
        log("Got skinned mesh PBSv2");
        log("");
    }

    void onProjectorRenderPart(const ovrAvatarRenderPart_ProjectorRender* renderPart) {
        log("Got projector");
        log("");
    }

    void onAvatarSpec(const ovrAvatarMessage_AvatarSpecification* spec) {
        avatar = ovrAvatar_Create(spec->avatarSpec, ovrAvatarCapability_All);
        auto assetCount = ovrAvatar_GetReferencedAssetCount(avatar);
        for (uint32_t i = 0; i < assetCount; ++i) {
            auto assetId = ovrAvatar_GetReferencedAsset(avatar, i);
            if (0 == assets.count(assetId)) {
                auto assetBase = ASSET_PATH + std::to_string(assetId);
                if (!(std::filesystem::exists(assetBase + ".gltf") || std::filesystem::exists(assetBase + ".ktx"))) {
                    assets.insert({ assetId, nullptr });
                    ovrAvatarAsset_BeginLoading(assetId);
                }
            }
        }

        auto avatarComponentCount = ovrAvatarComponent_Count(avatar);
        for (uint32_t i = 0; i < avatarComponentCount; ++i) {
            auto component = ovrAvatarComponent_Get(avatar, i);
            for (uint32_t j = 0; j < component->renderPartCount; ++j) {
                auto renderPart = component->renderParts[j];
                auto type = ovrAvatarRenderPart_GetType(renderPart);
                switch (type) {
                    case ovrAvatarRenderPartType_SkinnedMeshRender:
                        onSkinnnedMeshRenderPart(ovrAvatarRenderPart_GetSkinnedMeshRender(renderPart));
                        break;
                    case ovrAvatarRenderPartType_SkinnedMeshRenderPBS:
                        onSkinnnedMeshRenderPartPBS(ovrAvatarRenderPart_GetSkinnedMeshRenderPBS(renderPart));
                        break;
                    case ovrAvatarRenderPartType_SkinnedMeshRenderPBS_V2:
                        onSkinnnedMeshRenderPartPBSv2(ovrAvatarRenderPart_GetSkinnedMeshRenderPBSV2(renderPart));
                        break;
                    case ovrAvatarRenderPartType_ProjectorRender:
                        onProjectorRenderPart(ovrAvatarRenderPart_GetProjectorRender(renderPart));
                        break;
                }
            }
            log(std::string("Got component named: ") + component->name);
        }
    }

    void onAvatarCombinedMesh(ovrAvatarAsset* asset) {
        uint32_t idCount;
        auto ids = ovrAvatarAsset_GetCombinedMeshIDs(asset, &idCount);
        auto meshData = ovrAvatarAsset_GetCombinedMeshData(asset);

        for (uint32_t i = 0; i < idCount; ++i) {
            auto destId = ids[i];
            assets[destId] = asset;
        }

        Sleep(1);
    }

    //typedef struct ovrAvatarSkinnedMeshPose_ {
    //    uint32_t jointCount; ///< Number of joints in the joint hierarchy
    //    ovrAvatarTransform jointTransform[OVR_AVATAR_MAXIMUM_JOINT_COUNT]; ///< Array of local transform from parent
    //    int jointParents[OVR_AVATAR_MAXIMUM_JOINT_COUNT]; ///< Array of indices of the parent joints
    //    const char * jointNames[OVR_AVATAR_MAXIMUM_JOINT_COUNT]; ///< Array of joint names
    //} ovrAvatarSkinnedMeshPose;
    bool populateSkin(tinygltf::Model& model, const ovrAvatarSkinnedMeshPose& pose) {
        //if (pose.jointCount == 0) {
        //    return false;
        //}

        ////ovrAvatarTransform jointTransform[OVR_AVATAR_MAXIMUM_JOINT_COUNT]; ///< Array of local transform from parent
        ////int jointParents[OVR_AVATAR_MAXIMUM_JOINT_COUNT]; ///< Array of indices of the parent joints
        ////const char * jointNames[OVR_AVATAR_MAXIMUM_JOINT_COUNT]; ///< Array of joint names
        //model.nodes.back();

        //std::map<int, int> jointParents;
        //for (int i = 0; i < pose.jointCount; ++i) {
        //    jointParents[i] = pose.jointParents[i];
        //}

        //std::map<uint32_t, std::list<uint32_t>> jointChildren;
        //for (int i = 0; i < pose.jointCount; ++i) {
        //    jointChildren[i] = {};
        //    if (pose.jointParents[i] != -1) {
        //        auto parent = pose.jointParents[i];
        //        assert(jointChildren.count(parent) != 0);

        //    }

        //    jointParents[i] = pose.jointParents[i];
        //}

        //assert(pose.jointParents[0] == -1);
        //auto rootNodeIndex = model.nodes.size();
        //model.nodes.push_back({});
        //auto rootNode = model.nodes.back();
        return false;
    }

    void onAvatarMesh(ovrAvatarAssetID assetID, const ovrAvatarMeshAssetData* meshData) {
        tinygltf::Builder builder;
        auto& model = builder.model;
        model.asset.version = "2.0";

        using Vertex = ovrAvatarMeshVertex;
        using Index = uint16_t;
        auto vertexDataSize = sizeof(Vertex) * meshData->vertexCount;
        auto indexDataSize = sizeof(Index) * meshData->indexCount;
        glm::dvec3 minPos{ std::numeric_limits<double>::infinity() }, maxPos{ -minPos };
        {
            for (uint32_t i = 0; i < meshData->vertexCount; ++i) {
                const auto& vertex = meshData->vertexBuffer[i];
                glm::dvec3 normal{ vertex.nx, vertex.ny, vertex.nz };
                auto length = glm::length2(normal);
                double dev = glm::abs(length - 1.0f);
                if (dev > 0.0001) {
                    std::cerr << "Shit";
                }
                glm::dvec3 pos{ vertex.x, vertex.y, vertex.z };
                minPos = glm::min(minPos, pos);
                minPos = glm::max(minPos, pos);
            }
        }

        static const uint32_t FLOAT = TINYGLTF_COMPONENT_TYPE_FLOAT;
        static const uint32_t UBYTE = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        static const uint32_t USHORT = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
        static const uint32_t VEC3 = TINYGLTF_TYPE_VEC3;
        static const uint32_t VEC4 = TINYGLTF_TYPE_VEC4;
        static const uint32_t VEC2 = TINYGLTF_TYPE_VEC2;

        model.meshes.resize(1);
        auto& mesh = model.meshes.back();
        mesh.primitives.resize(1);
        auto& prim = mesh.primitives.back();
        prim.mode = TINYGLTF_MODE_TRIANGLES;
        prim.indices = builder.appendIndices(USHORT, meshData->indexBuffer, meshData->indexCount);
        auto offset = offsetof(Vertex, x);

        // De-interleave the vertex components for easier alignment
        auto positionAccessorIndex = builder.appendVertexComponents(VEC3, FLOAT, meshData->vertexBuffer, meshData->vertexCount, offset);
        auto& positionAccessor = builder.model.accessors[positionAccessorIndex];
        {
            glm::dvec3 min{ std::numeric_limits<double>::infinity() }, max{ -min };
            for (uint32_t i = 0; i < meshData->vertexCount; ++i) {
                const auto& vertex = meshData->vertexBuffer[i];
                glm::dvec3 pos{ vertex.x, vertex.y, vertex.z };
                min = glm::min(min, pos);
                max = glm::max(max, pos);
            }
            positionAccessor.minValues.resize(3);
            positionAccessor.maxValues.resize(3);
            memcpy(positionAccessor.minValues.data(), &min, sizeof(min));
            memcpy(positionAccessor.maxValues.data(), &max, sizeof(max));
        }
        prim.attributes["POSITION"] = positionAccessorIndex;
        offset = offsetof(Vertex, nx);
        prim.attributes["NORMAL"] = builder.appendVertexComponents(VEC3, FLOAT, meshData->vertexBuffer, meshData->vertexCount, offset);
        offset = offsetof(Vertex, tx);
        prim.attributes["TANGENT"] = builder.appendVertexComponents(VEC4, FLOAT, meshData->vertexBuffer, meshData->vertexCount, offset);
        offset = offsetof(Vertex, u);
        prim.attributes["TEXCOORD_0"] = builder.appendVertexComponents(VEC2, FLOAT, meshData->vertexBuffer, meshData->vertexCount, offset);
        offset = offsetof(Vertex, blendIndices);
        prim.attributes["JOINTS_0"] = builder.appendVertexComponents(VEC4, UBYTE, meshData->vertexBuffer, meshData->vertexCount, offset);
        offset = offsetof(Vertex, blendWeights);
        prim.attributes["WEIGHTS_0"] = builder.appendVertexComponents(VEC4, FLOAT, meshData->vertexBuffer, meshData->vertexCount, offset);

        populateSkin(model, meshData->skinnedBindPose);

        auto nodeIndex = model.nodes.size();
        model.nodes.push_back({});
        auto& node = model.nodes.back();
        node.mesh = 0;
        model.scenes.push_back({});
        auto& scene = model.scenes.back();
        scene.nodes.push_back((uint32_t)nodeIndex);
        auto assetName = std::to_string(assetID);
        tinygltf::TinyGLTF().WriteGltfSceneToFile(&builder.model, ASSET_PATH + assetName + ".gltf", true, false);
        Sleep(1);
    }

    void onAvatarTexture(ovrAvatarAssetID assetID, const ovrAvatarTextureAssetData* textureData) {
#if 0
        // Raw data dump for debugging
        {
            std::string assetName = std::to_string(assetID);
            {
                std::ofstream out(ASSET_PATH + assetName + ".raw", std::ios::binary | std::ios::trunc);
                out.write((const char*)textureData->textureData, textureData->textureDataSize);
            }
            json meta;
            {
                json size;
                size["x"] = textureData->sizeX;
                size["y"] = textureData->sizeX;
                meta["size"] = size;
            }
            meta["mipCount"] = textureData->mipCount;
            meta["format"] = textureData->format;
            {
                std::ofstream out(ASSET_PATH + assetName + ".json", std::ios::trunc);
                out << meta.dump(2);
            }
        }
#else
        auto output = ovr::ovrTextureToKtx(textureData);
#if 0
        // Validate before write
        StoragePointer buffer = Storage::create(output.size(), output.data());
        if (!KTX::validate(buffer)) {
            throw std::runtime_error("Bad ktx");
        }
#endif

        {
            std::string assetName = std::to_string(assetID);
            std::ofstream out(ASSET_PATH + assetName + ".ktx", std::ios::binary | std::ios::trunc);
            out.write((const char*)output.data(), output.size());
            out.close();
        }
#endif
    }

    void onAvatarMaterial(const ovrAvatarMaterialState* materialState) { Sleep(1); }

    void onAvatarPbsMaterial(const ovrAvatarPBSMaterialState* materialState) { Sleep(1); }

    bool onAvatarAsset(const ovrAvatarMessage_AssetLoaded* assetLoadedMessage) {
        const auto& asset = assetLoadedMessage->asset;
        const auto& assetID = assetLoadedMessage->assetID;
        const auto& lod = assetLoadedMessage->lod;
        assets[assetID] = asset;
        auto type = ovrAvatarAsset_GetType(asset);
        switch (type) {
            case ovrAvatarAssetType_Mesh:
                log("Got mesh");
                onAvatarMesh(assetID, ovrAvatarAsset_GetMeshData(asset));
                break;

            case ovrAvatarAssetType_CombinedMesh:
                log("Got combined mesh");
                onAvatarCombinedMesh(asset);
                break;

            case ovrAvatarAssetType_Texture:
                log("Got texture");
                onAvatarTexture(assetID, ovrAvatarAsset_GetTextureData(asset));
                break;

            case ovrAvatarAssetType_Material:
                log("Got material");
                onAvatarMaterial(ovrAvatarAsset_GetMaterialData(asset));
                break;

            case ovrAvatarAssetType_PBSMaterial:
                log("Got PBS material");
                onAvatarPbsMaterial(ovrAvatarAsset_GetPBSMaterialData(asset));
                break;

            case ovrAvatarAssetType_FailedLoad:
                throw std::runtime_error("Failed load");
            case ovrAvatarAssetType_Pose:
            default:
                throw std::runtime_error("Unknown asset type");
        }

        bool full = true;
        for (const auto& entry : assets) {
            if (nullptr == entry.second) {
                full = false;
            }
        }
        return full;
    }

    void run() {
        ovrAvatar_Initialize("Test");
        ovrAvatar_RegisterLoggingCallback(&OculusAvatarExample::log);
        ovrAvatar_SetLoggingLevel(ovrAvatarLogLevel_Verbose);

        std::vector<uint64_t> avatarIds{ {
            10150022857785745,
            10150022857770130,
            10150022857753417,
            10150022857731826,
        } };

        for (const auto& avatarId : avatarIds) {
            auto specRequest = ovrAvatarSpecificationRequest_Create(avatarId);
            //ovrAvatarSpecificationRequest_SetCombineMeshes(specRequest, true);
            ovrAvatarSpecificationRequest_SetCombineMeshes(specRequest, false);
            ovrAvatar_RequestAvatarSpecificationFromSpecRequest(specRequest);
            ovrAvatarSpecificationRequest_Destroy(specRequest);
        }

        bool loaded = false;
        while (!loaded) {
            ovrAvatarMessage* message = ovrAvatarMessage_Pop();
            if (nullptr == message) {
                Sleep(10);
                continue;
            }
            auto type = ovrAvatarMessage_GetType(message);
            switch (type) {
                case ovrAvatarMessageType_AvatarSpecification:
                    log("Avatar specification message");
                    onAvatarSpec(ovrAvatarMessage_GetAvatarSpecification(message));
                    break;
                case ovrAvatarMessageType_AssetLoaded:
                    log("Asset loaded message");
                    loaded = onAvatarAsset(ovrAvatarMessage_GetAssetLoaded(message));
                    ovrAvatarMessage_Free(message);
                    break;
                default:
                    throw std::runtime_error("Bad message");
            }
        }

        if (avatar != nullptr) {
            ovrAvatar_Destroy(avatar);
            avatar = nullptr;
        }
        ovrAvatar_Shutdown();
    }
};

RUN_EXAMPLE(OculusAvatarExample)
