#include "vr_common.hpp"
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

    std::set<std::string> getInstanceExtensionsRequired(vr::IVRCompositor* compositor) {
        auto bytesRequired = compositor->GetVulkanInstanceExtensionsRequired(nullptr, 0);
        //GetVulkanDeviceExtensionsRequired
        std::vector<char> extensions;  extensions.resize(bytesRequired);
        compositor->GetVulkanInstanceExtensionsRequired(extensions.data(), extensions.size());
        std::set<std::string> result;
        std::string buffer;
        for (char c : extensions) {
            if (c == 0 || c == ' ') {
                if (!buffer.empty()) {
                    result.insert(buffer);
                    buffer.clear();
                }
                if (c == 0) {
                    break;
                }
            } else {
                buffer += c;
            }
        }

        return result;
    }

    std::set<std::string> getDeviceExtensionsRequired(const vk::PhysicalDevice& physicalDevice, vr::IVRCompositor* compositor) {
        auto bytesRequired = compositor->GetVulkanDeviceExtensionsRequired(physicalDevice, nullptr, 0);
        //GetVulkanDeviceExtensionsRequired
        std::vector<char> extensions;  extensions.resize(bytesRequired);
        compositor->GetVulkanDeviceExtensionsRequired(physicalDevice, extensions.data(), extensions.size());
        std::set<std::string> result;
        std::string buffer;
        for (char c : extensions) {
            if (c == 0 || c == ' ') {
                if (!buffer.empty()) {
                    result.insert(buffer);
                    buffer.clear();
                }
                if (c == 0) {
                    break;
                }
            }
            else {
                buffer += c;
            }
        }

        return result;
    }
}


class OpenVrExample : public VrExample {
    using Parent = VrExample;
public:
    std::array<glm::mat4, 2> eyeOffsets;
    vr::IVRSystem* vrSystem { nullptr };
    vr::IVRCompositor* vrCompositor { nullptr };


    ~OpenVrExample() {
        vrSystem = nullptr;
        vrCompositor = nullptr;
        vr::VR_Shutdown();
    }

    void prepareOpenVr() {
        vr::EVRInitError eError;
        vrSystem = vr::VR_Init(&eError, vr::VRApplication_Scene);
        vrSystem->GetRecommendedRenderTargetSize(&renderTargetSize.x, &renderTargetSize.y);
        vrCompositor = vr::VRCompositor();

        context.requireExtensions(openvr::getInstanceExtensionsRequired(vrCompositor));
        
        // Recommended render target size is per-eye, so double the X size for 
        // left + right eyes
        renderTargetSize.x *= 2;

        openvr::for_each_eye([&](vr::Hmd_Eye eye) {
            eyeOffsets[eye] = openvr::toGlm(vrSystem->GetEyeToHeadTransform(eye));
            eyeProjections[eye] = openvr::toGlm(vrSystem->GetProjectionMatrix(eye, 0.1f, 256.0f));
        });

        context.setDeviceExtensionsPicker([&](const vk::PhysicalDevice& physicalDevice)->std::set<std::string> {
            return openvr::getDeviceExtensionsRequired(physicalDevice, vrCompositor);
        });
    }

    void prepare() {
        prepareOpenVr();
        Parent::prepare();
    }

    void update(float delta) {
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

    void render() {
        vk::Fence submitFence = swapChain.getSubmitFence(true);
        auto currentImage = swapChain.acquireNextImage(shapesRenderer->semaphores.renderStart);

        shapesRenderer->render();

        vk::Format format = shapesRenderer->framebuffer.colors[0].format;
        vr::VRVulkanTextureData_t vulkanTexture {};
        vulkanTexture.m_nImage = (uint64_t)(VkImage)shapesRenderer->framebuffer.colors[0].image,
        vulkanTexture.m_pDevice = (VkDevice)context.device;
        vulkanTexture.m_pPhysicalDevice = (VkPhysicalDevice)context.physicalDevice;
        vulkanTexture.m_pInstance = (VkInstance)context.instance;
        vulkanTexture.m_pQueue = (VkQueue)context.queue;
        vulkanTexture.m_nQueueFamilyIndex = context.graphicsQueueIndex;
        vulkanTexture.m_nWidth = renderTargetSize.x;
        vulkanTexture.m_nHeight = renderTargetSize.y;
        vulkanTexture.m_nFormat = (uint32_t)(VkFormat)format;
        vulkanTexture.m_nSampleCount = 1;

        // Flip y-axis since GL UV coords are backwards.
        static vr::VRTextureBounds_t leftBounds { 0, 0, 0.5f, 1 };
        static vr::VRTextureBounds_t rightBounds { 0.5f, 0, 1, 1 };

        vr::Texture_t texture { (void*)&vulkanTexture, vr::TextureType_Vulkan, vr::ColorSpace_Auto };
        //vrCompositor->Submit(vr::Eye_Left, &texture, &leftBounds);
        //vrCompositor->Submit(vr::Eye_Right, &texture, &rightBounds);

        context.submit(cmdBuffers[currentImage],
            { { shapesRenderer->semaphores.renderComplete,  vk::PipelineStageFlagBits::eBottomOfPipe } },
            { blitComplete }, submitFence);
        swapChain.queuePresent(blitComplete);

    }

    std::string getWindowTitle() {
        std::string device(context.deviceProperties.deviceName);
        return "OpenVR SDK Example " + device + " - " + std::to_string((int)lastFPS) + " fps";
    }
};

RUN_EXAMPLE(OpenVrExample)

