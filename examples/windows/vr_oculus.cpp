#include "vr_common.hpp"
#include <OVR_CAPI.h>
#include <OVR_CAPI_GL.h>

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
    
}

class OpenGLInteropExample : public VrExampleBase {
    using Parent = VrExampleBase;
public:
    GLuint _mirrorFbo{ 0 };
    ovr::Session _session{};
    ovr::HmdDesc _hmdDesc{};
    ovr::GraphicsLuid _luid{};
    ovr::TextureSwapChain _eyeTexture;
    ovr::MirrorTexture _mirrorTexture;
    ovr::LayerEyeFov _sceneLayer;
    ovr::ViewScaleDesc _viewScaleDesc;

    OpenGLInteropExample() {
        ovr_Initialize(nullptr);
    }

    ~OpenGLInteropExample() {
        ovr_Destroy(_session);
        _session = nullptr;
        ovr_Shutdown();
    }

    void submitVrFrame() override {
        ovrLayerHeader* headerList = &_sceneLayer.Header;
        ovr_SubmitFrame(_session, frameCounter, &_viewScaleDesc, &headerList, 1);
    }


    void renderMirror() override {
        GLuint mirrorTextureId;
        ovr_GetMirrorTextureBufferGL(_session, _mirrorTexture, &mirrorTextureId);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _mirrorFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mirrorTextureId, 0);
        glBlitFramebuffer(0, 0, size.x, size.y, 0, size.y, size.x, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    void setupVrFramebuffer() override {
        {
            ovrTextureSwapChainDesc desc = {};
            desc.Type = ovrTexture_2D;
            desc.ArraySize = 1;
            desc.Width = renderTargetSize.x;
            desc.Height = renderTargetSize.y;
            desc.MipLevels = 1;
            desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
            desc.SampleCount = 1;
            desc.StaticImage = ovrFalse;
            if (!OVR_SUCCESS(ovr_CreateTextureSwapChainGL(_session, &desc, &_eyeTexture))) {
                throw std::runtime_error("Unable to create swap chain");
            }
            int length = 0;
            if (!OVR_SUCCESS(ovr_GetTextureSwapChainLength(_session, _eyeTexture, &length)) || !length) {
                throw std::runtime_error("Unable to count swap chain textures");
            }
            for (int i = 0; i < length; ++i) {
                GLuint chainTexId;
                ovr_GetTextureSwapChainBufferGL(_session, _eyeTexture, i, &chainTexId);
                glBindTexture(GL_TEXTURE_2D, chainTexId);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        _sceneLayer.ColorTexture[0] = _eyeTexture;
        // Set up the framebuffer object
        glCreateFramebuffers(1, &_fbo);

        ovrMirrorTextureDesc mirrorDesc;
        memset(&mirrorDesc, 0, sizeof(mirrorDesc));
        mirrorDesc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
        mirrorDesc.Width = size.x;
        mirrorDesc.Height = size.y;
        if (!OVR_SUCCESS(ovr_CreateMirrorTextureGL(_session, &mirrorDesc, &_mirrorTexture))) {
            throw std::runtime_error("Could not create mirror texture");
        }
        glCreateFramebuffers(1, &_mirrorFbo);
    }

    virtual void bindVrFramebuffer() {
        int curIndex;
        if (!OVR_SUCCESS(ovr_GetTextureSwapChainCurrentIndex(_session, _eyeTexture, &curIndex))) {
            throw std::runtime_error("Unable to acquire next texture index");
        }
        GLuint curTexId;
        if (!OVR_SUCCESS(ovr_GetTextureSwapChainBufferGL(_session, _eyeTexture, curIndex, &curTexId))) {
            throw std::runtime_error("Unable to acquire GL texture for index " + std::to_string(curIndex));
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, curTexId, 0);
    }

    virtual void unbindVrFramebuffer() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        ovr_CommitTextureSwapChain(_session, _eyeTexture);
    }

    void prepare() {
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

        Parent::prepare();
    }

    void update(float delta) {
        ovr::EyePoses eyePoses;
        ovr_GetEyePoses(_session, frameCounter, true, _viewScaleDesc.HmdToEyeOffset, eyePoses.data(), &_sceneLayer.SensorSampleTime);
        eyeViews = std::array<glm::mat4, 2>{ glm::inverse(ovr::toGlm(eyePoses[0])), glm::inverse(ovr::toGlm(eyePoses[1])) };
        ovr::for_each_eye([&](ovrEyeType eye) {
            const auto& vp = _sceneLayer.Viewport[eye];
            _sceneLayer.RenderPose[eye] = eyePoses[eye];
            //renderScene(_eyeProjections[eye], ovr::toGlm(eyePoses[eye]));
        });
        Parent::update(delta);
    }

    std::string getWindowTitle() {
        std::string device(vulkanContext.deviceProperties.deviceName);
        return "OpenGL Interop - " + device + " - " + std::to_string(frameCounter) + " fps";
    }
};

RUN_EXAMPLE(OpenGLInteropExample)
