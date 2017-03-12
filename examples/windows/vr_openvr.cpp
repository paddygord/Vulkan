#include "common.hpp"
#include "vulkanShapes.hpp"
#include <openvr.h>

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

class VrExampleBase {
public:
    vkx::Context vulkanContext;
    vkx::ShapesRenderer vulkanRenderer;
    vr::IVRSystem* vrSystem { nullptr };
    GLFWwindow* window{ nullptr };
    float fpsTimer{ 0 };
    float lastFPS{ 0 };
    uint32_t frameCounter{ 0 };
    glm::uvec2 size{ 1280, 720 };
    glm::uvec2 renderTargetSize;
    std::array<glm::mat4, 2> eyeViews;
    std::array<glm::mat4, 2> eyeProjections;


    using StringList = std::list<std::string>;
    static StringList splitString(const std::string& source, char delimiter = ' ') {
        StringList result;
        std::string::size_type start = 0, end = source.find(delimiter);
        while (end != std::string::npos) {
            result.push_back(source.substr(start, end));
            start = end + 1;
            end = source.find(delimiter, start);
        }
        result.push_back(source.substr(start, end));
        return result;
    }

    VrExampleBase() : vulkanRenderer{ vulkanContext, true } {
        glfwInit();
        vr::EVRInitError error = vr::EVRInitError::VRInitError_None;
        vrSystem = vr::VR_Init(&error, vr::EVRApplicationType::VRApplication_Scene);
        uint32_t requiredExtensionsLength = vr::VRCompositor()->GetVulkanInstanceExtensionsRequired(nullptr, 0);
        char* requiredExtensionsBuffer = new char[requiredExtensionsLength];
        vr::VRCompositor()->GetVulkanInstanceExtensionsRequired(requiredExtensionsBuffer, requiredExtensionsLength);
        auto requiredExtensions = splitString(requiredExtensionsBuffer);
        delete[] requiredExtensionsBuffer;
    }

    ~VrExampleBase() {
        if (nullptr != window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

    virtual void submitVrFrame() = 0;
    virtual void renderMirror() = 0;


    void render() {
    }

    virtual void prepare() {
        // Make the on screen window 1/4 the resolution of the render target
        size = renderTargetSize;
        size /= 4;
        vulkanContext.createContext(false);
        vulkanRenderer.framebufferSize = renderTargetSize;
        vulkanRenderer.prepare();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(size.x, size.y, "glfw", nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("Unable to create rendering window");
        }
    }

    virtual void update(float delta) {
        vulkanRenderer.update(delta, eyeProjections, eyeViews);
    }

    virtual void run() final {
        prepare();
        auto tStart = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window)) {
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            glfwPollEvents();
            update((float)tDiff / 1000.0f);
            render();
            ++frameCounter;
            fpsTimer += (float)tDiff;
            if (fpsTimer > 1000.0f) {
                std::string windowTitle = getWindowTitle();
                glfwSetWindowTitle(window, windowTitle.c_str());
                lastFPS = (float)frameCounter;
                fpsTimer = 0.0f;
                frameCounter = 0;
            }
            tStart = tEnd;
        }
    }

    virtual std::string getWindowTitle() {
        std::string device(vulkanContext.deviceProperties.deviceName);
        return "OpenGL Interop - " + device + " - " + std::to_string(frameCounter) + " fps";
    }
};

class OpenVrExample : public VrExampleBase {
    using Parent = VrExampleBase;
public:
    std::array<glm::mat4, 2> eyeOffsets;
    vr::IVRSystem* vrSystem{ nullptr };
    vr::IVRCompositor* vrCompositor{ nullptr };

    void submitVrFrame() override {
        //// Flip y-axis since GL UV coords are backwards.
        //static vr::VRTextureBounds_t leftBounds{ 0, 0, 0.5f, 1 };
        //static vr::VRTextureBounds_t rightBounds{ 0.5f, 0, 1, 1 };
        //vr::Texture_t texture{ (void*)_colorBuffer, vr::API_OpenGL, vr::ColorSpace_Auto };
        //vrCompositor->Submit(vr::Eye_Left, &texture, &leftBounds);
        //vrCompositor->Submit(vr::Eye_Right, &texture, &rightBounds);
    }

    virtual void renderMirror() override {
        //gl::nv::vk::DrawVkImage(vulkanRenderer.framebuffer.colors[0].image, 0, vec2(0), size, 0, glm::vec2(0), glm::vec2(1));
    }

    void update(float delta) override {
        vr::TrackedDevicePose_t currentTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];
        vrCompositor->WaitGetPoses(currentTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
        vr::TrackedDevicePose_t _trackedDevicePose[vr::k_unMaxTrackedDeviceCount];
        float displayFrequency = vrSystem->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);
        float frameDuration = 1.f / displayFrequency;
        float vsyncToPhotons = vrSystem->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float);
        float predictedDisplayTime = frameDuration + vsyncToPhotons;
        vrSystem->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, (float)predictedDisplayTime, _trackedDevicePose, vr::k_unMaxTrackedDeviceCount);
        auto basePose = openvr::toGlm(_trackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking);
        eyeViews = std::array<glm::mat4, 2>{ glm::inverse(basePose * eyeOffsets[0]), glm::inverse(basePose * eyeOffsets[1]) };
        Parent::update(delta);
    }

    void prepare() {
        vr::EVRInitError eError;
        vrSystem = vr::VR_Init(&eError, vr::VRApplication_Scene);
        vrSystem->GetRecommendedRenderTargetSize(&renderTargetSize.x, &renderTargetSize.y);
        vrCompositor = vr::VRCompositor();
        // Recommended render target size is per-eye, so double the X size for 
        // left + right eyes
        renderTargetSize.x *= 2;

        openvr::for_each_eye([&](vr::Hmd_Eye eye) {
            eyeOffsets[eye] = openvr::toGlm(vrSystem->GetEyeToHeadTransform(eye));
            //eyeProjections[eye] = openvr::toGlm(vrSystem->GetProjectionMatrix(eye, 0.1f, 256.0f, vr::API_OpenGL));
        });
        Parent::prepare();
    }

    std::string getWindowTitle() {
        std::string device(vulkanContext.deviceProperties.deviceName);
        return "OpenGL Interop - " + device + " - " + std::to_string(frameCounter) + " fps";
    }
};

RUN_EXAMPLE(OpenVrExample)
