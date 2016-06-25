#include "common.hpp"
#include <OVR_CAPI.h>
#include <OVR_CAPI_GL.h>
#include "vulkanShapes.hpp"
#include "vulkanGL.hpp"

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

    class App {
    protected:
        Session _session{};
        HmdDesc _hmdDesc{};
        GraphicsLuid _luid{};
        std::array<glm::mat4, 2> _eyeProjections;
        std::array<EyeRenderDesc, 2> _eyeRenderDescs;
        TextureSwapChain _eyeTexture;
        MirrorTexture _mirrorTexture;
        LayerEyeFov _sceneLayer;
        ViewScaleDesc _viewScaleDesc;
        uvec2 _renderTargetSize;

    public:
        App() {
            ovr_Initialize(nullptr);
            if (!OVR_SUCCESS(ovr_Create(&_session, &_luid))) {
                throw std::runtime_error("Unable to create HMD session");
            }

            _hmdDesc = ovr_GetHmdDesc(_session);
            _viewScaleDesc.HmdSpaceToWorldScaleInMeters = 1.0f;
            memset(&_sceneLayer, 0, sizeof(ovrLayerEyeFov));
            _sceneLayer.Header.Type = ovrLayerType_EyeFov;
            _sceneLayer.Header.Flags = ovrLayerFlag_TextureOriginAtBottomLeft;

            ovr::for_each_eye([&](ovrEyeType eye) {
                ovrEyeRenderDesc& erd = _eyeRenderDescs[eye] = ovr_GetRenderDesc(_session, eye, _hmdDesc.DefaultEyeFov[eye]);
                ovrMatrix4f ovrPerspectiveProjection =
                    ovrMatrix4f_Projection(erd.Fov, 0.01f, 1000.0f, ovrProjection_ClipRangeOpenGL);
                _eyeProjections[eye] = ovr::toGlm(ovrPerspectiveProjection);
                _viewScaleDesc.HmdToEyeOffset[eye] = erd.HmdToEyeOffset;

                ovrFovPort & fov = _sceneLayer.Fov[eye] = _eyeRenderDescs[eye].Fov;
                auto eyeSize = ovr_GetFovTextureSize(_session, eye, fov, 1.0f);
                _sceneLayer.Viewport[eye].Size = eyeSize;
                _sceneLayer.Viewport[eye].Pos = { (int)_renderTargetSize.x, 0 };

                _renderTargetSize.y = std::max(_renderTargetSize.y, (uint32_t)eyeSize.h);
                _renderTargetSize.x += eyeSize.w;
            });
        }

        ~App() {
            ovr_Destroy(_session);
            _session = nullptr;
        }

        void createTextureSwapChainGL() {
            ovrTextureSwapChainDesc desc = {};
            desc.Type = ovrTexture_2D;
            desc.ArraySize = 1;
            desc.Width = _renderTargetSize.x;
            desc.Height = _renderTargetSize.y;
            desc.MipLevels = 1;
            desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
            desc.SampleCount = 1;
            desc.StaticImage = ovrFalse;
            ovrResult result = ovr_CreateTextureSwapChainGL(_session, &desc, &_eyeTexture);
            if (!OVR_SUCCESS(result)) {
                throw std::runtime_error("Unable to create swap chain");
            }
        }

        EyePoses getEyePoses(uint64_t frame) {
            EyePoses result;
            ovr_GetEyePoses(_session, frame, true, _viewScaleDesc.HmdToEyeOffset, result.data(), &_sceneLayer.SensorSampleTime);
            return result;
        }

        GLuint getTexture() {
            int curIndex;
            if (!OVR_SUCCESS(ovr_GetTextureSwapChainCurrentIndex(_session, _eyeTexture, &curIndex))) {
                throw std::runtime_error("Unable to acquire next texture index");
            }
            GLuint curTexId;
            if (!OVR_SUCCESS(ovr_GetTextureSwapChainBufferGL(_session, _eyeTexture, curIndex, &curTexId))) {
                throw std::runtime_error("Unable to acquire GL texture for index " + std::to_string(curIndex));
            }
            return curTexId;
        }

    };

}

class OpenGLInteropExample : public ovr::App {
public:
    vkx::Context vulkanContext;
    vkx::ShapesRenderer vulkanRenderer;
    GLFWwindow* window{ nullptr };
    glm::uvec2 size{ 1280, 720 };
    float fpsTimer{ 0 };
    float lastFPS{ 0 };
    uint32_t frameCounter{ 0 };


    GLuint _fbo{ 0 };
    GLuint _depthBuffer{ 0 };
    GLuint _mirrorFbo{ 0 };

    OpenGLInteropExample() : vulkanRenderer{ vulkanContext, true } {
        glfwInit();

        // Make the on screen window 1/4 the resolution of the render target
        size = _renderTargetSize;
        size /= 4;
        vulkanContext.createContext(false);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_DEPTH_BITS, 16);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
        window = glfwCreateWindow(size.x, size.y, "glfw", nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("Unable to create rendering window");
        }
        glfwSetWindowPos(window, 100, -1080 + 100);
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);
        glewExperimental = true;
        glewInit();
        glGetError();
        gl::nv::vk::init();

        createTextureSwapChainGL();

        _sceneLayer.ColorTexture[0] = _eyeTexture;
        int length = 0;
        ovrResult result = ovr_GetTextureSwapChainLength(_session, _eyeTexture, &length);
        if (!OVR_SUCCESS(result) || !length) {
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

        // Set up the framebuffer object
        glGenFramebuffers(1, &_fbo);
        glGenRenderbuffers(1, &_depthBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
        glBindRenderbuffer(GL_RENDERBUFFER, _depthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, _renderTargetSize.x, _renderTargetSize.y);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        ovrMirrorTextureDesc mirrorDesc;
        memset(&mirrorDesc, 0, sizeof(mirrorDesc));
        mirrorDesc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
        mirrorDesc.Width = size.x;
        mirrorDesc.Height = size.y;
        if (!OVR_SUCCESS(ovr_CreateMirrorTextureGL(_session, &mirrorDesc, &_mirrorTexture))) {
            throw std::runtime_error("Could not create mirror texture");
        }
        glGenFramebuffers(1, &_mirrorFbo);
    }

    ~OpenGLInteropExample() {
        if (nullptr != window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

    void render(float delta) {
        auto eyePoses = getEyePoses(frameCounter);
        auto views = std::array<glm::mat4, 2>{ glm::inverse(ovr::toGlm(eyePoses[0])), glm::inverse(ovr::toGlm(eyePoses[1])) };
        vulkanRenderer.update(delta / 1000.0f, _eyeProjections, views);

        glfwMakeContextCurrent(window);

        // Tell the 
        //gl::nv::vk::SignalSemaphore(vulkanRenderer.semaphores.renderStart);
        //glFlush();
        //vulkanRenderer.render();
        //glClearColor(0, 0.5f, 0.8f, 1.0f);
        //glClear(GL_COLOR_BUFFER_BIT);
        //gl::nv::vk::WaitSemaphore(vulkanRenderer.semaphores.renderComplete);
        //gl::nv::vk::DrawVkImage(vulkanRenderer.framebuffer.colors[0].image, 0, vec2(0), vec2(1280, 720));
        //glfwSwapBuffers(window);


        GLuint curTexId = getTexture();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, curTexId, 0);
        glClearColor(0, 0.5f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ovr::for_each_eye([&](ovrEyeType eye) {
            const auto& vp = _sceneLayer.Viewport[eye];
            glViewport(vp.Pos.x, vp.Pos.y, vp.Size.w, vp.Size.h);
            _sceneLayer.RenderPose[eye] = eyePoses[eye];
            //renderScene(_eyeProjections[eye], ovr::toGlm(eyePoses[eye]));
        });
        // Tell the 
        gl::nv::vk::SignalSemaphore(vulkanRenderer.semaphores.renderStart);
        glFlush();
        vulkanRenderer.render();
        glClearColor(0, 0.5f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        gl::nv::vk::WaitSemaphore(vulkanRenderer.semaphores.renderComplete);
        gl::nv::vk::DrawVkImage(vulkanRenderer.framebuffer.colors[0].image, 0, vec2(0), _renderTargetSize, 0, glm::vec2(0), glm::vec2(1));
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        ovr_CommitTextureSwapChain(_session, _eyeTexture);
        ovrLayerHeader* headerList = &_sceneLayer.Header;
        ovr_SubmitFrame(_session, frameCounter, &_viewScaleDesc, &headerList, 1);

        GLuint mirrorTextureId;
        ovr_GetMirrorTextureBufferGL(_session, _mirrorTexture, &mirrorTextureId);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, _mirrorFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mirrorTextureId, 0);
        glBlitFramebuffer(0, 0, size.x, size.y, 0, size.y, size.x, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glfwSwapBuffers(window);
    }

    void prepare() {
        vulkanRenderer.framebuffer.size = _renderTargetSize;
        vulkanRenderer.prepare();
    }

    void run() {
        prepare();
        auto tStart = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window)) {
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            glfwPollEvents();
            render((float)tDiff);
            ++frameCounter;
            fpsTimer += (float)tDiff;
            if (fpsTimer > 1000.0f) {
                std::string windowTitle = getWindowTitle();
                glfwSetWindowTitle(window, windowTitle.c_str());
                lastFPS = frameCounter;
                fpsTimer = 0.0f;
                frameCounter = 0;
            }
            tStart = tEnd;
        }
    }

    std::string getWindowTitle() {
        std::string device(vulkanContext.deviceProperties.deviceName);
        return "OpenGL Interop - " + device + " - " + std::to_string(frameCounter) + " fps";
    }
};

RUN_EXAMPLE(OpenGLInteropExample)
