#include "vr_common.hpp"
#include <OVR_CAPI_Vk.h>

namespace ovr {
    using TextureSwapChainDesc = ovrTextureSwapChainDesc;
    using Session = ovrSession;
    using HmdDesc = ovrHmdDesc;
    using GraphicsLuid = ovrGraphicsLuid;
    using TextureSwapChain = ovrTextureSwapChain;
    using MirrorTexture = ovrMirrorTexture;
    using EyeRenderDesc = ovrEyeRenderDesc;
    using LayerEyeFov = ovrLayerEyeFov;
    using ViewScaleDesc = ovrViewScaleDesc;
    using Posef = ovrPosef;
    using EyePoses = std::array<Posef, 2>;

    //using EyeType = ovrEyeType;
    enum class EyeType {
        Left = ovrEye_Left,
        Right = ovrEye_Right
    };

    // Convenience method for looping over each eye with a lambda
    template <typename Function>
    inline void for_each_eye(Function function) {
        for (ovrEyeType eye = ovrEyeType::ovrEye_Left;
            eye < ovrEyeType::ovrEye_Count;
            eye = static_cast<ovrEyeType>(eye + 1)) {
            function(eye);
        }
    }

    inline mat4 toGlm(const ovrMatrix4f & om) {
        return glm::transpose(glm::make_mat4(&om.M[0][0]));
    }

    inline mat4 toGlm(const ovrFovPort & fovport, float nearPlane = 0.01f, float farPlane = 10000.0f) {
        return toGlm(ovrMatrix4f_Projection(fovport, nearPlane, farPlane, true));
    }

    inline vec3 toGlm(const ovrVector3f & ov) {
        return glm::make_vec3(&ov.x);
    }

    inline vec2 toGlm(const ovrVector2f & ov) {
        return glm::make_vec2(&ov.x);
    }

    inline uvec2 toGlm(const ovrSizei & ov) {
        return uvec2(ov.w, ov.h);
    }

    inline quat toGlm(const ovrQuatf & oq) {
        return glm::make_quat(&oq.x);
    }

    inline mat4 toGlm(const ovrPosef & op) {
        mat4 orientation = glm::mat4_cast(toGlm(op.Orientation));
        mat4 translation = glm::translate(mat4(), ovr::toGlm(op.Position));
        return translation * orientation;
    }

    inline std::array<glm::mat4, 2> toGlm(const EyePoses& eyePoses) {
        return std::array<glm::mat4, 2>{ toGlm(eyePoses[0]), toGlm(eyePoses[1]) };
    }


    inline ovrMatrix4f fromGlm(const mat4 & m) {
        ovrMatrix4f result;
        mat4 transposed(glm::transpose(m));
        memcpy(result.M, &(transposed[0][0]), sizeof(float) * 16);
        return result;
    }

    inline ovrVector3f fromGlm(const vec3 & v) {
        ovrVector3f result;
        result.x = v.x;
        result.y = v.y;
        result.z = v.z;
        return result;
    }

    inline ovrVector2f fromGlm(const vec2 & v) {
        ovrVector2f result;
        result.x = v.x;
        result.y = v.y;
        return result;
    }

    inline ovrSizei fromGlm(const uvec2 & v) {
        ovrSizei result;
        result.w = v.x;
        result.h = v.y;
        return result;
    }

    inline ovrQuatf fromGlm(const quat & q) {
        ovrQuatf result;
        result.x = q.x;
        result.y = q.y;
        result.z = q.z;
        result.w = q.w;
        return result;
    }

    void OVR_CDECL logger(uintptr_t userData, int level, const char* message) {
        OutputDebugStringA("OVR_SDK: ");
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
    }
}

class OculusExample : public VrExample {
    using Parent = VrExample;
public:
    ovr::Session _session {};
    ovr::HmdDesc _hmdDesc {};
    ovr::GraphicsLuid _luid {};
    ovr::LayerEyeFov _sceneLayer;
    ovr::TextureSwapChain& _eyeTexture = _sceneLayer.ColorTexture[0];
    ovr::MirrorTexture _mirrorTexture;
    ovr::ViewScaleDesc _viewScaleDesc;

    ~OculusExample() {
        // Shut down Oculus
        ovr_Destroy(_session);
        _session = nullptr;
        ovr_Shutdown();
    }

    void prepareOculus() {
        ovrInitParams initParams { 0, OVR_MINOR_VERSION, ovr::logger, (uintptr_t)this, 0 };
        if (!OVR_SUCCESS(ovr_Initialize(&initParams))) {
            throw std::runtime_error("Unable to initialize Oculus SDK");
        }

        if (!OVR_SUCCESS(ovr_Create(&_session, &_luid))) {
            throw std::runtime_error("Unable to create HMD session");
        }

        _hmdDesc = ovr_GetHmdDesc(_session);
        _viewScaleDesc.HmdSpaceToWorldScaleInMeters = 1.0f;
        memset(&_sceneLayer, 0, sizeof(ovrLayerEyeFov));
        _sceneLayer.Header.Type = ovrLayerType_EyeFov;
        _sceneLayer.Header.Flags = ovrLayerFlag_TextureOriginAtBottomLeft;

        ovr::for_each_eye([&](ovrEyeType eye) {
            ovrEyeRenderDesc erd = ovr_GetRenderDesc(_session, eye, _hmdDesc.DefaultEyeFov[eye]);
            ovrMatrix4f ovrPerspectiveProjection =
                ovrMatrix4f_Projection(erd.Fov, 0.01f, 1000.0f, ovrProjection_ClipRangeOpenGL);
            eyeProjections[eye] = ovr::toGlm(ovrPerspectiveProjection);
            _viewScaleDesc.HmdToEyeOffset[eye] = erd.HmdToEyeOffset;

            ovrFovPort & fov = _sceneLayer.Fov[eye] = erd.Fov;
            auto eyeSize = ovr_GetFovTextureSize(_session, eye, fov, 1.0f);
            _sceneLayer.Viewport[eye].Size = eyeSize;
            _sceneLayer.Viewport[eye].Pos = { (int)renderTargetSize.x, 0 };
            renderTargetSize.y = std::max(renderTargetSize.y, (uint32_t)eyeSize.h);
            renderTargetSize.x += eyeSize.w;
        });

        context.requireExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        context.setDevicePicker([this](const std::vector<vk::PhysicalDevice>& devices)->vk::PhysicalDevice {
            VkPhysicalDevice result;
            if (!OVR_SUCCESS(ovr_GetSessionPhysicalDeviceVk(_session, _luid, context.instance, &result))) {
                throw std::runtime_error("Unable to identify Vulkan device");
            }
            return result;
        });
    }


    void prepareOculusVk() {
        ovr_SetSynchonizationQueueVk(_session, context.queue);

        ovrTextureSwapChainDesc desc = {};
        desc.Type = ovrTexture_2D;
        desc.ArraySize = 1;
        desc.Width = renderTargetSize.x;
        desc.Height = renderTargetSize.y;
        desc.MipLevels = 1;
        desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.SampleCount = 1;
        desc.StaticImage = ovrFalse;
        if (!OVR_SUCCESS(ovr_CreateTextureSwapChainVk(_session, context.device, &desc, &_eyeTexture))) {
            throw std::runtime_error("Unable to create swap chain");
        }

        int length = 0;
        if (!OVR_SUCCESS(ovr_GetTextureSwapChainLength(_session, _eyeTexture, &length)) || !length) {
            throw std::runtime_error("Unable to count swap chain textures");
        }

        ovrMirrorTextureDesc mirrorDesc;
        memset(&mirrorDesc, 0, sizeof(mirrorDesc));
        mirrorDesc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
        mirrorDesc.Width = size.x;
        mirrorDesc.Height = size.y;
        if (!OVR_SUCCESS(ovr_CreateMirrorTextureWithOptionsVk(_session, context.device, &mirrorDesc, &_mirrorTexture))) {
            throw std::runtime_error("Could not create mirror texture");
        }

        imgBlit.dstSubresource.aspectMask = imgBlit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        imgBlit.dstSubresource.layerCount = imgBlit.srcSubresource.layerCount = 1;
        imgBlit.dstOffsets[1] = imgBlit.srcOffsets[1] = vk::Offset3D { (int32_t)renderTargetSize.x, (int32_t)renderTargetSize.y, 1 };
    }

    void prepare() {
        prepareOculus();
        Parent::prepare();
        prepareOculusVk();
    }

    void update(float delta) {
        ovr::EyePoses eyePoses;
        ovr_GetEyePoses(_session, frameCounter, true, _viewScaleDesc.HmdToEyeOffset, eyePoses.data(), &_sceneLayer.SensorSampleTime);
        eyeViews = std::array<glm::mat4, 2>{ glm::inverse(ovr::toGlm(eyePoses[0])), glm::inverse(ovr::toGlm(eyePoses[1])) };
        ovr::for_each_eye([&](ovrEyeType eye) {
            const auto& vp = _sceneLayer.Viewport[eye];
            _sceneLayer.RenderPose[eye] = eyePoses[eye];
        });
        Parent::update(delta);
    }

    void render() {
        shapesRenderer->renderWithoutSemaphors();

        int curIndex;
        if (!OVR_SUCCESS(ovr_GetTextureSwapChainCurrentIndex(_session, _eyeTexture, &curIndex))) {
            throw std::runtime_error("Unable to acquire next texture index");
        }
        VkImage swapchainImage;
        if (!OVR_SUCCESS(ovr_GetTextureSwapChainBufferVk(_session, _eyeTexture, curIndex, &swapchainImage))) {
            throw std::runtime_error("Unable to acquire vulkan image for index " + std::to_string(curIndex));
        }

        context.withPrimaryCommandBuffer([&](const vk::CommandBuffer& cmdBuffer) {
            cmdBuffer.blitImage(shapesRenderer->framebuffer.colors[0].image, vk::ImageLayout::eTransferSrcOptimal, swapchainImage, vk::ImageLayout::eTransferDstOptimal, imgBlit, vk::Filter::eNearest);
        });

        if (!OVR_SUCCESS(ovr_CommitTextureSwapChain(_session, _eyeTexture))) {
            throw std::runtime_error("Unable to commit swap chain for index " + std::to_string(curIndex));
        }

        ovrLayerHeader* headerList = &_sceneLayer.Header;
        if (!OVR_SUCCESS(ovr_SubmitFrame(_session, frameCounter, &_viewScaleDesc, &headerList, 1))) {
            throw std::runtime_error("Unable to submit frame for index " + std::to_string(curIndex));
        }
    }

    std::string getWindowTitle() {
        std::string device(context.deviceProperties.deviceName);
        return "Oculus SDK Example " + device + " - " + std::to_string((int)lastFPS) + " fps";
    }

#if 0
    // FIXME restore mirror window functionality
    void renderMirror() override {
        GLuint mirrorTextureId;
        ovr_GetMirrorTextureBufferGL(_session, _mirrorTexture, &mirrorTextureId);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _mirrorFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mirrorTextureId, 0);
        glBlitFramebuffer(0, 0, size.x, size.y, 0, size.y, size.x, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
#endif

};

RUN_EXAMPLE(OculusExample)
