/*
* Vulkan Example - OpenGL interoperability example
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>
#include <vks/texture.hpp>
#include <unordered_map>
#include <Windows.h>

#include <D:/OVRAvatarSDK/include/OVR_Avatar.h>
#pragma comment(lib, "D:/OVRAvatarSDK/Windows/libovravatar.lib")

class OculusAvatarExample {
public:
    static void log(const char* str) {
        OutputDebugStringA(str);
        OutputDebugStringA("\n");
    }

    static void log(const std::string& str) {
        log(str.c_str());
    }

    ovrAvatar* avatar{ nullptr };
    std::unordered_map<ovrAvatarAssetID, ovrAvatarAsset*> assets;


    void onSkinnnedMeshRenderPart(const ovrAvatarRenderPart_SkinnedMeshRender* renderPart) {
        log("Got skinned mesh");
    }
    void onSkinnnedMeshRenderPartPBS(const ovrAvatarRenderPart_SkinnedMeshRenderPBS* renderPart) {
        log("Got skinned mesh PBS");
    }
    void onSkinnnedMeshRenderPartPBSv2(const ovrAvatarRenderPart_SkinnedMeshRenderPBS_V2* renderPart) {
        log("Got skinned mesh PBSv2");
    }
    void onProjectorRenderPart(const ovrAvatarRenderPart_ProjectorRender* renderPart) {
        log("Got projector");
    }

    void onAvatarSpec(const ovrAvatarMessage_AvatarSpecification* spec) {
        avatar = ovrAvatar_Create(spec->avatarSpec, ovrAvatarCapability_All);

        auto assetCount = ovrAvatar_GetReferencedAssetCount(avatar);
        for (uint32_t i = 0; i < assetCount; ++i) {
            auto assetId = ovrAvatar_GetReferencedAsset(avatar, i);
            if (0 == assets.count(assetId)) {
                assets.insert({ assetId, nullptr });
                ovrAvatarAsset_BeginLoading(assetId);
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

    void onAvatarMesh(const ovrAvatarMeshAssetData* meshData) { Sleep(1); }

    void onAvatarTexture(const ovrAvatarTextureAssetData* textureData) { Sleep(1); }

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
                onAvatarMesh(ovrAvatarAsset_GetMeshData(asset));
                break;

            case ovrAvatarAssetType_CombinedMesh:
                log("Got combined mesh");
                onAvatarCombinedMesh(asset);
                break;

            case ovrAvatarAssetType_Texture:
                log("Got texture");
                onAvatarTexture(ovrAvatarAsset_GetTextureData(asset));
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
            ovrAvatarSpecificationRequest_SetCombineMeshes(specRequest, true);
            ovrAvatar_RequestAvatarSpecificationFromSpecRequest(specRequest);
            ovrAvatarSpecificationRequest_Destroy(specRequest);
        }

        // ovrAvatar_RequestAvatarSpecification(10150022857785745);
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
                    break;
                default:
                    throw std::runtime_error("Bad message");
            }
            ovrAvatarMessage_Free(message);
        }

        if (avatar != nullptr) {
            ovrAvatar_Destroy(avatar);
            avatar = nullptr;
        }
        ovrAvatar_Shutdown();
    }
};

RUN_EXAMPLE(OculusAvatarExample)
