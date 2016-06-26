#include "common.hpp"
#include <openvr.h>
#include "vulkanShapes.hpp"
#include "vulkanGL.hpp"

namespace openvr {
    template<typename F>
    void for_each_eye(F f) {
        f(vr::Hmd_Eye::Eye_Left);
        f(vr::Hmd_Eye::Eye_Right);
    }

    inline mat4 toGlm(const vr::HmdMatrix44_t& m) {
        return glm::transpose(glm::make_mat4(&m.m[0][0]));
    }

    inline vec3 toGlm(const vr::HmdVector3_t& v) {
        return vec3(v.v[0], v.v[1], v.v[2]);
    }

    inline mat4 toGlm(const vr::HmdMatrix34_t& m) {
        mat4 result = mat4(
            m.m[0][0], m.m[1][0], m.m[2][0], 0.0,
            m.m[0][1], m.m[1][1], m.m[2][1], 0.0,
            m.m[0][2], m.m[1][2], m.m[2][2], 0.0,
            m.m[0][3], m.m[1][3], m.m[2][3], 1.0f);
        return result;
    }

    inline vr::HmdMatrix34_t toOpenVr(const mat4& m) {
        vr::HmdMatrix34_t result;
        for (uint8_t i = 0; i < 3; ++i) {
            for (uint8_t j = 0; j < 4; ++j) {
                result.m[i][j] = m[j][i];
            }
        }
        return result;
    }

}


class OpenGLInteropExample {
public:
    vkx::Context vulkanContext;
    vkx::ShapesRenderer vulkanRenderer;
    GLFWwindow* window{ nullptr };
    glm::uvec2 size{ 1280, 720 };
    float fpsTimer{ 0 };
    float lastFPS{ 0 };
    uint32_t frameCounter{ 0 };
    glm::uvec2 _renderTargetSize;
    std::array<glm::mat4, 2> _eyeOffsets;
    std::array<glm::mat4, 2> _eyeProjections;

    vr::IVRSystem* vrSystem{ nullptr };
    vr::IVRCompositor* vrCompositor{ nullptr };
    GLuint _fbo{ 0 };
    GLuint _depthBuffer{ 0 };
    GLuint _colorBuffer{ 0 };
    GLuint _mirrorFbo{ 0 };

    OpenGLInteropExample() : vulkanRenderer{ vulkanContext, true } {
        glfwInit();
        vr::EVRInitError eError;
        vrSystem = vr::VR_Init(&eError, vr::VRApplication_Scene);
        vrSystem->GetRecommendedRenderTargetSize(&_renderTargetSize.x, &_renderTargetSize.y);
        vrCompositor = vr::VRCompositor();
        // Recommended render target size is per-eye, so double the X size for 
        // left + right eyes
        _renderTargetSize.x *= 2;

        openvr::for_each_eye([&](vr::Hmd_Eye eye) {
            _eyeOffsets[eye] = openvr::toGlm(vrSystem->GetEyeToHeadTransform(eye));
            _eyeProjections[eye] = openvr::toGlm(vrSystem->GetProjectionMatrix(eye, 0.1f, 256.0f, vr::API_OpenGL));
        });

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

        // Set up the framebuffer object
        glCreateFramebuffers(1, &_fbo);
        glGenRenderbuffers(1, &_depthBuffer);
        glNamedRenderbufferStorage(_depthBuffer, GL_DEPTH_COMPONENT16, _renderTargetSize.x, _renderTargetSize.y);
        glNamedFramebufferRenderbuffer(_fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthBuffer);
        glCreateTextures(GL_TEXTURE_2D, 1, &_colorBuffer);
        glTextureStorage2D(_colorBuffer, 1, GL_RGBA8, _renderTargetSize.x, _renderTargetSize.y);
        glTextureParameteri(_colorBuffer, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(_colorBuffer, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(_colorBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(_colorBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(_fbo, GL_COLOR_ATTACHMENT0, _colorBuffer, 0);
    }

    ~OpenGLInteropExample() {
        if (nullptr != window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

    void render() {
        glfwMakeContextCurrent(window);

        gl::nv::vk::SignalSemaphore(vulkanRenderer.semaphores.renderStart);
        glFlush();
        vulkanRenderer.render();
        gl::nv::vk::WaitSemaphore(vulkanRenderer.semaphores.renderComplete);
        gl::nv::vk::DrawVkImage(vulkanRenderer.framebuffer.colors[0].image, 0, vec2(0), size, 0, glm::vec2(0), glm::vec2(1));

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
        glClearColor(0, 0.5f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        gl::nv::vk::DrawVkImage(vulkanRenderer.framebuffer.colors[0].image, 0, vec2(0), _renderTargetSize, 0, glm::vec2(0), glm::vec2(1));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        // Flip y-axis since GL UV coords are backwards.
        static vr::VRTextureBounds_t leftBounds{ 0, 0, 0.5f, 1 };
        static vr::VRTextureBounds_t rightBounds{ 0.5f, 0, 1, 1 };
        vr::Texture_t texture{ (void*)_colorBuffer, vr::API_OpenGL, vr::ColorSpace_Auto };
        vrCompositor->Submit(vr::Eye_Left, &texture, &leftBounds);
        vrCompositor->Submit(vr::Eye_Right, &texture, &rightBounds);
        glfwSwapBuffers(window);
    }

    void prepare() {
        vulkanRenderer.framebuffer.size = _renderTargetSize;
        vulkanRenderer.prepare();
    }

    vr::TrackedDevicePose_t _trackedDevicePose[vr::k_unMaxTrackedDeviceCount];
    glm::mat4 basePose;

    void update(float delta) {
        vr::TrackedDevicePose_t currentTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];
        vrCompositor->WaitGetPoses(currentTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0);

        double displayFrequency = vrSystem->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);
        double frameDuration = 1.f / displayFrequency;
        double vsyncToPhotons = vrSystem->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float);
        double predictedDisplayTime = frameDuration + vsyncToPhotons;
        vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, predictedDisplayTime, _trackedDevicePose, vr::k_unMaxTrackedDeviceCount);
        basePose = openvr::toGlm(_trackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
        auto views = std::array<glm::mat4, 2>{ glm::inverse(basePose * _eyeOffsets[0]), glm::inverse(basePose * _eyeOffsets[1]) };
        vulkanRenderer.update(delta / 1000.0f, _eyeProjections, views);
    }

    void run() {
        prepare();
        auto tStart = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window)) {
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            glfwPollEvents();
            update(tDiff);
            render();
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
