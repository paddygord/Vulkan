#include <vr/vr_common.hpp>
#include <math.h>
#include <openxr/openxr.hpp>
#include <unordered_map>

namespace xrs {

template <typename T>
std::vector<std::string> splitCharBuffer(const T& buffer) {
    std::vector<std::string> result;
    std::string curString;
    for (const auto c : buffer) {
        if (c == 0 || c == ' ') {
            if (!curString.empty()) {
                result.push_back(curString);
                curString.clear();
            }
            if (c == 0) {
                break;
            }
            continue;
        }
        curString.push_back(c);
    }
    return result;
}

inline XrFovf toTanFovf(const XrFovf& fov) {
    return { tanf(fov.angleLeft), tanf(fov.angleRight), tanf(fov.angleUp), tanf(fov.angleDown) };
}

inline mat4 toGlm(const XrFovf& fov, float nearZ = 0.01f, float farZ = 10000.0f) {
    auto tanFov = toTanFovf(fov);
    const auto& tanAngleRight = tanFov.angleRight;
    const auto& tanAngleLeft = tanFov.angleLeft;
    const auto& tanAngleUp = tanFov.angleUp;
    const auto& tanAngleDown = tanFov.angleDown;

    const float tanAngleWidth = tanAngleRight - tanAngleLeft;
    const float tanAngleHeight = (tanAngleDown - tanAngleUp);
    const float offsetZ = 0;

    glm::mat4 resultm{};
    float* result = &resultm[0][0];
    // normal projection
    result[0] = 2 / tanAngleWidth;
    result[4] = 0;
    result[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
    result[12] = 0;

    result[1] = 0;
    result[5] = 2 / tanAngleHeight;
    result[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
    result[13] = 0;

    result[2] = 0;
    result[6] = 0;
    result[10] = -(farZ + offsetZ) / (farZ - nearZ);
    result[14] = -(farZ * (nearZ + offsetZ)) / (farZ - nearZ);

    result[3] = 0;
    result[7] = 0;
    result[11] = -1;
    result[15] = 0;

    return resultm;
}

inline quat toGlm(const XrQuaternionf& q) {
    return glm::make_quat(&q.x);
}

inline vec3 toGlm(const XrVector3f& v) {
    return glm::make_vec3(&v.x);
}

inline mat4 toGlm(const XrPosef& p) {
    mat4 orientation = glm::mat4_cast(toGlm(p.orientation));
    mat4 translation = glm::translate(mat4(), toGlm(p.position));
    return translation * orientation;
}

inline ivec2 toGlm(const XrExtent2Di& e) {
    return { e.width, e.height };
}

// Oculus runtime current reports the XR_EXT_debug_utils is supported, but fails when you request it
#define SUPPRESS_DEBUG_UTILS 1

struct Context {
    using InteractionProfileChangedHandler = std::function<void(const xr::EventDataInteractionProfileChanged&)>;

    // Interaction with non-core (KHR, EXT, etc) functions requires a dispatch instance
    xr::DispatchLoaderDynamic dispatch;
    bool enableDebug{ true };
    std::unordered_map<std::string, xr::ExtensionProperties> discoveredExtensions;
    xr::Instance instance;
    xr::SystemId systemId;
    xr::Session session;
    xr::InstanceProperties instanceProperties;
    xr::SystemProperties systemProperties;
    bool stopped{ false };

    xr::Swapchain swapchain;
    xr::Extent2Df bounds;
    xr::SessionState state{ xr::SessionState::Idle };
    xr::FrameState frameState;
    xr::Result beginFrameResult{ xr::Result::FrameDiscarded };
    xr::ViewConfigurationType viewConfigType;
    xr::ViewConfigurationProperties viewConfigProperties;
    std::vector<xr::ViewConfigurationView> viewConfigViews;
    std::vector<xr::View> eyeViewStates;

    std::array<xr::CompositionLayerProjectionView, 2> projectionLayerViews;
    
    xr::CompositionLayerProjection projectionLayer{ {}, {}, 2, projectionLayerViews.data() };
    xr::Space& space{ projectionLayer.space };
    std::vector<xr::CompositionLayerBaseHeader*> layersPointers;

    InteractionProfileChangedHandler interactionProfileChangedHandler{ [](const xr::EventDataInteractionProfileChanged&) {} };

    static XrBool32 debugCallback(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
                                  XrDebugUtilsMessageTypeFlagsEXT messageTypes,
                                  const XrDebugUtilsMessengerCallbackDataEXT* callbackData,
                                  void* userData) {
        return static_cast<Context*>(userData)->onValidationMessage(messageSeverity, messageTypes, callbackData);
    }

    XrBool32 onValidationMessage(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
                                 XrDebugUtilsMessageTypeFlagsEXT messageTypes,
                                 const XrDebugUtilsMessengerCallbackDataEXT* callbackData) {
#ifdef WIN32
        OutputDebugStringA(callbackData->message);
        OutputDebugStringA("\n");
#endif
        std::cout << callbackData->message << std::endl;
        return XR_TRUE;
    }

    void create() {
        for (const auto& extensionProperties : xr::enumerateInstanceExtensionProperties(nullptr)) {
            discoveredExtensions.insert({ extensionProperties.extensionName, extensionProperties });
        }

        if (0 == discoveredExtensions.count(XR_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            enableDebug = false;
        }

        vks::CStringVector requestedExtensions;
#if defined(XR_USE_GRAPHICS_API_VULKAN)
        if (0 == discoveredExtensions.count(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME)) {
            throw std::runtime_error("Vulkan XR extension not available");
        }
        requestedExtensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
#endif

#if !SUPPRESS_DEBUG_UTILS
        if (enableDebug) {
            requestedExtensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
#endif

        {
            xr::InstanceCreateInfo ici{ {},
                                        { "vr_openxr", 0, "vulkan_cpp_examples", 0, XR_CURRENT_API_VERSION },
                                        0,
                                        nullptr,
                                        (uint32_t)requestedExtensions.size(),
                                        requestedExtensions.data() };

            xr::DebugUtilsMessengerCreateInfoEXT dumci;
            static constexpr auto XR_ALL_TYPES = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                 XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
            dumci.messageSeverities = xr::DebugUtilsMessageSeverityFlagBitsEXT::AllBits;
            dumci.messageTypes = xr::DebugUtilsMessageTypeFlagBitsEXT::AllBits;
            dumci.userData = this;
            dumci.userCallback = &Context::debugCallback;

#if !SUPPRESS_DEBUG_UTILS
            if (enableDebug) {
                ici.next = &dumci;
            }
#endif
            instance = xr::createInstance(ici);
        }
        instanceProperties = instance.getInstanceProperties();

        // Having created the isntance, the very first thing to do is populate the dynamic dispatch, loading
        // all the available functions from the runtime
        dispatch = xr::DispatchLoaderDynamic::createFullyPopulated(instance, &xrGetInstanceProcAddr);

        // We want to create an HMD example
        systemId = instance.getSystem(xr::SystemGetInfo{ xr::FormFactor::HeadMountedDisplay });
        systemProperties = instance.getSystemProperties(systemId);

        // Find out what view configurations we have available
        {
            auto viewConfigTypes = instance.enumerateViewConfigurations(systemId);
            viewConfigType = viewConfigTypes[0];
            if (viewConfigType != xr::ViewConfigurationType::PrimaryStereo) {
                throw std::runtime_error("Example only supports stereo-based HMD rendering");
            }
            viewConfigProperties = instance.getViewConfigurationProperties(systemId, viewConfigType);
        }

        viewConfigViews = instance.enumerateViewConfigurationViews(systemId, viewConfigType);
    }

    void destroy() {
        destroySession();
        if (instance) {
            instance.destroy();
            instance = nullptr;
        }
    }

    void destroySwapchain() {
        if (swapchain) {
            swapchain.destroy();
            swapchain = nullptr;
        }

#if defined(XR_USE_GRAPHICS_API_VULKAN)
        vulkanSwapchainImages.clear();
#endif
    }

#if defined(XR_USE_GRAPHICS_API_VULKAN)
    std::vector<xr::SwapchainImageVulkanKHR> vulkanSwapchainImages;
    std::vector<std::string> getVulkanInstanceExtensions() { return splitCharBuffer<>(instance.getVulkanInstanceExtensionsKHR(systemId, dispatch)); }
    std::vector<std::string> getVulkanDeviceExtensions() { return splitCharBuffer<>(instance.getVulkanDeviceExtensionsKHR(systemId, dispatch)); }
    std::vector<vk::Format> getVulkanSwapchainFormats() {
        std::vector<vk::Format> swapchainFormats;
        auto swapchainFormatsRaw = session.enumerateSwapchainFormats();
        swapchainFormats.reserve(swapchainFormatsRaw.size());
        for (const auto& format : swapchainFormatsRaw) {
            swapchainFormats.push_back((vk::Format)format);
        }
        return swapchainFormats;
    }

    void createVulkanSwapchain(const uvec2& size,
                               vk::Format format = vk::Format::eB8G8R8A8Srgb,
                               xr::SwapchainUsageFlags usageFlags = xr::SwapchainUsageFlagBits::TransferDst,
                               uint32_t samples = 1,
                               uint32_t arrayCount = 1,
                               uint32_t faceCount = 1,
                               uint32_t mipCount = 1) {
        createVulkanSwapchain(xr::SwapchainCreateInfo{ {}, usageFlags, (VkFormat)format, samples, size.x, size.y, faceCount, arrayCount, mipCount });
    }

    void createVulkanSwapchain(const xr::SwapchainCreateInfo& createInfo) {
        swapchain = session.createSwapchain(createInfo);
        vulkanSwapchainImages = swapchain.enumerateSwapchainImages<xr::SwapchainImageVulkanKHR>();
    }
#endif

    void destroySession() {
        destroySwapchain();
        if (session) {
            session.destroy();
            session = nullptr;
        }
    }

    template <typename T>
    void createSession(const T& graphicsBinding) {
        // Create the session bound to the vulkan device and queue
        {
            //auto graphicsRequirements = xr::getVulkanGraphicsRequirementsKHR(_xr.instance, _xr.systemId);
            xr::SessionCreateInfo sci{ {}, systemId };
            sci.next = &graphicsBinding;
            session = instance.createSession(sci);
        }

        auto referenceSpaces = session.enumerateReferenceSpaces();
        space = session.createReferenceSpace(xr::ReferenceSpaceCreateInfo{ xr::ReferenceSpaceType::Local });
        session.getReferenceSpaceBoundsRect(xr::ReferenceSpaceType::Local, bounds);
        projectionLayer.space = space;
        projectionLayer.viewCount = 2;
        projectionLayer.views = projectionLayerViews.data();
        layersPointers.push_back((xr::CompositionLayerBaseHeader*)&projectionLayer);
    }

    void pollEvents() {
        while (true) {
            xr::EventDataBuffer eventBuffer;
            auto pollResult = instance.pollEvent(eventBuffer);
            if (pollResult == xr::Result::EventUnavailable) {
                break;
            }

            switch (eventBuffer.type) {
                case xr::StructureType::EventDataSessionStateChanged: {
                    onSessionStateChanged(reinterpret_cast<xr::EventDataSessionStateChanged&>(eventBuffer));
                    break;
                }
                case xr::StructureType::EventDataInstanceLossPending: {
                    onInstanceLossPending(reinterpret_cast<xr::EventDataInstanceLossPending&>(eventBuffer));
                    break;
                }
                case xr::StructureType::EventDataInteractionProfileChanged: {
                    onInteractionprofileChanged(reinterpret_cast<xr::EventDataInteractionProfileChanged&>(eventBuffer));
                    break;
                }
                case xr::StructureType::EventDataReferenceSpaceChangePending: {
                    onReferenceSpaceChangePending(reinterpret_cast<xr::EventDataReferenceSpaceChangePending&>(eventBuffer));
                    break;
                }
            }
        }
    }

    void onSessionStateChanged(const xr::EventDataSessionStateChanged& sessionStateChangedEvent) {
        state = sessionStateChangedEvent.state;
        std::cout << "Session state " << (uint32_t)state << std::endl;
        switch (state) {
            case xr::SessionState::Ready: {
                std::cout << "Starting session" << std::endl;
                if (!stopped) {
                    session.beginSession(xr::SessionBeginInfo{ viewConfigType });
                }
            } break;

            case xr::SessionState::Stopping: {
                std::cout << "Stopping session" << std::endl;
                session.endSession();
                stopped = true;
            } break;

            case xr::SessionState::Exiting:
            case xr::SessionState::LossPending: {
                std::cout << "Destroying session" << std::endl;
                destroySession();
            } break;

            default:
                break;
        }
    }
    void onInstanceLossPending(const xr::EventDataInstanceLossPending&) {}
    void onEventsLost(const xr::EventDataEventsLost&) {}

    void onReferenceSpaceChangePending(const xr::EventDataReferenceSpaceChangePending& event) { int i = 0; }

    void onInteractionprofileChanged(const xr::EventDataInteractionProfileChanged& event) { interactionProfileChangedHandler(event); }

    void onFrameStart() {
        beginFrameResult = xr::Result::FrameDiscarded;
        switch (state) {
            case xr::SessionState::Focused: {
                //XrActionsSyncInfo actionsSyncInfo{ XR_TYPE_ACTIONS_SYNC_INFO, nullptr, 0, nullptr };
                //_xr.session.syncActions(&actionsSyncInfo);
            }
                // fallthough
            case xr::SessionState::Synchronized:
            case xr::SessionState::Visible: {
                session.waitFrame(xr::FrameWaitInfo{}, frameState);
                beginFrameResult = session.beginFrame(xr::FrameBeginInfo{});
                switch (beginFrameResult) {
                    case xr::Result::SessionLossPending:
                        std::cout << "Session loss pending" << std::endl;
                        return;

                    case xr::Result::FrameDiscarded:
                        std::cout << "Frame discarded" << std::endl;
                        return;

                    default:
                        break;
                }
            } break;

            default:
                beginFrameResult = xr::Result::FrameDiscarded;
                Sleep(100);
        }
    }

    bool shouldRender() const { return beginFrameResult == xr::Result::Success && frameState.shouldRender; }
};

const std::array<std::string, 2> HAND_PATHS{ {
    "/user/hand/left",
    "/user/hand/right",
} };

}  // namespace xrs

struct InputState {
    xr::ActionSet actionSet;
    xr::Action grabAction;
    xr::Action moveAction;
    xr::Action poseAction;
    xr::Action vibrateAction;
    xr::Action quitAction;
    std::array<xr::Path, 2> handSubactionPath;
    std::array<xr::Space, 2> handSpace;
    std::array<float, 2> handScale;
    std::array<xr::Bool32, 2> renderHand;
    glm::vec2 moveAmount;

    static xr::BilateralPaths makeHandSubpaths(const xr::Instance& instance, const std::string& subpath = "") {
        xr::BilateralPaths result;
        // Create subactions for left and right hands.
        xr::for_each_side_index([&](uint32_t side) {
            std::string fullPath = xrs::HAND_PATHS[side] + subpath;
            result[side] = instance.stringToPath(fullPath.c_str());
        });
        return result;
    }

    void initialize(const xr::Instance& instance) {
        // Create an action set.
        actionSet = instance.createActionSet(xr::ActionSetCreateInfo{ "gameplay", "Gameplay" });

        // Create subactions for left and right hands.
        handSubactionPath = makeHandSubpaths(instance);

        // Create actions.
        // Create an input action for grabbing objects with the left and right hands.
        grabAction = actionSet.createAction({ "grab_object", xr::ActionType::FloatInput, 2, handSubactionPath.data(), "Grab Object" });
        // Create an input action getting the left and right hand poses.
        poseAction = actionSet.createAction({ "hand_pose", xr::ActionType::PoseInput, 2, handSubactionPath.data(), "Hand Pose" });
        // Create output actions for vibrating the left and right controller.
        vibrateAction = actionSet.createAction({ "hand_vibrate_handpose", xr::ActionType::VibrationOutput, 2, handSubactionPath.data(), "Vibrate Hand" });
        // Create input actions for quitting the session using the left and right controller.
        quitAction = actionSet.createAction({ "quit_session", xr::ActionType::BooleanInput, 2, handSubactionPath.data(), "Quit Session" });
        // An action for moving in the X/Z plane
        moveAction = actionSet.createAction({ "move_player", xr::ActionType::Vector2FInput, 2, handSubactionPath.data(), "Move Player" });

        auto selectPath = makeHandSubpaths(instance, "/input/select/click");
        auto squeezeValuePath = makeHandSubpaths(instance, "/input/squeeze/value");
        auto squeezeClickPath = makeHandSubpaths(instance, "/input/squeeze/click");
        auto posePath = makeHandSubpaths(instance, "/input/grip/pose");
        auto hapticPath = makeHandSubpaths(instance, "/output/haptic");
        auto menuClickPath = makeHandSubpaths(instance, "/input/menu/click");
        auto moveValuePath = makeHandSubpaths(instance, "/input/thumbstick");

        std::vector<xr::ActionSuggestedBinding> commonBindings{ { { poseAction, posePath[xr::Side::Left] },
                                                                  { poseAction, posePath[xr::Side::Right] },
                                                                  { quitAction, menuClickPath[xr::Side::Left] },
                                                                  { quitAction, menuClickPath[xr::Side::Right] },
                                                                  { vibrateAction, hapticPath[xr::Side::Left] },
                                                                  { vibrateAction, hapticPath[xr::Side::Right] } } };

        // Suggest bindings for KHR Simple.
        {
            std::vector<xr::ActionSuggestedBinding> bindings{ {
                // Fall back to a click input for the grab action.
                { grabAction, selectPath[xr::Side::Left] },
                { grabAction, selectPath[xr::Side::Right] },
            } };
            bindings.insert(bindings.end(), commonBindings.begin(), commonBindings.end());
            auto interactionProfilePath = instance.stringToPath("/interaction_profiles/khr/simple_controller");
            instance.suggestInteractionProfileBindings({ interactionProfilePath, (uint32_t)bindings.size(), bindings.data() });
        }

        // Suggest bindings for the Oculus Touch.
        {
            std::vector<xr::ActionSuggestedBinding> bindings{ {
                { grabAction, squeezeValuePath[xr::Side::Left] },
                { grabAction, squeezeValuePath[xr::Side::Right] },
                { moveAction, moveValuePath[xr::Side::Left] },
                { moveAction, moveValuePath[xr::Side::Right] },
            } };
            bindings.insert(bindings.end(), commonBindings.begin(), commonBindings.end());
            auto interactionProfilePath = instance.stringToPath("/interaction_profiles/oculus/touch_controller");
            instance.suggestInteractionProfileBindings({ interactionProfilePath, (uint32_t)bindings.size(), bindings.data() });
        }

        // Suggest bindings for the Vive Controller.
        {
            std::vector<xr::ActionSuggestedBinding> bindings{ {
                { grabAction, squeezeClickPath[xr::Side::Left] },
                { grabAction, squeezeClickPath[xr::Side::Right] },
            } };
            bindings.insert(bindings.end(), commonBindings.begin(), commonBindings.end());
            auto interactionProfilePath = instance.stringToPath("/interaction_profiles/htc/vive_controller");
            instance.suggestInteractionProfileBindings({ interactionProfilePath, (uint32_t)bindings.size(), bindings.data() });
        }

        // Suggest bindings for the Microsoft Mixed Reality Motion Controller.
        {
            std::vector<xr::ActionSuggestedBinding> bindings{ {
                { grabAction, squeezeClickPath[xr::Side::Left] },
                { grabAction, squeezeClickPath[xr::Side::Right] },
            } };
            bindings.insert(bindings.end(), commonBindings.begin(), commonBindings.end());
            auto interactionProfilePath = instance.stringToPath("/interaction_profiles/microsoft/motion_controller");
            instance.suggestInteractionProfileBindings({ interactionProfilePath, (uint32_t)bindings.size(), bindings.data() });
        }
    }

    void attach(const xr::Session& session) {
        xr::for_each_side_index([&](uint32_t side) { handSpace[side] = session.createActionSpace({ poseAction, handSubactionPath[side], {} }); });
        session.attachSessionActionSets(xr::SessionActionSetsAttachInfo{ 1, &actionSet });
    }

    void pollActions(xr::SessionState state, const xr::Session& session) {
        renderHand = { XR_FALSE, XR_FALSE };
        if (state != xr::SessionState::Focused) {
            return;
        }

        // Sync actions
        const xr::ActiveActionSet activeActionSet{ actionSet, XR_NULL_PATH };

        session.syncActions({ 1, &activeActionSet });

        // Get pose and grab action state and start haptic vibrate when hand is 90% squeezed.
        xr::for_each_side_index([&](uint32_t hand) {
            moveAmount = {};
            xr::ActionStateVector2f moveValue = session.getActionStateVector2f({ moveAction, handSubactionPath[hand] });
            if (moveValue.isActive) {
                moveAmount += glm::vec2{ moveValue.currentState.x, moveValue.currentState.y };
            } 
            auto grabValue = session.getActionStateFloat({ grabAction, handSubactionPath[hand] });

            if (grabValue.isActive) {
                // Scale the rendered hand by 1.0f (open) to 0.5f (fully squeezed).
                handScale[hand] = 1.0f - 0.5f * grabValue.currentState;
                if (grabValue.currentState > 0.9f) {
                    xr::HapticVibration vibration{ xr::Duration::minHaptic(), XR_FREQUENCY_UNSPECIFIED, 0.5f };
                    session.applyHapticFeedback({ vibrateAction, handSubactionPath[hand] }, (XrHapticBaseHeader*)&vibration);
                }
            }

            auto quitValue = session.getActionStateBoolean({ quitAction, handSubactionPath[hand] });
            if (quitValue.isActive && quitValue.changedSinceLastSync && quitValue.currentState) {
                session.requestExitSession();
            }

            auto poseState = session.getActionStatePose({ poseAction, handSubactionPath[hand] });
            renderHand[hand] = poseState.isActive;
        });
    }

    std::array<mat4, 2> getHandPoses(xr::Space appSpace, xr::Time displayTime) {
        std::array<mat4, 2> result;
        xr::for_each_side_index([&](uint32_t hand) {
            auto spaceLocation = handSpace[hand].locateSpace(appSpace, displayTime);
            auto requiredBits = xr::SpaceLocationFlagBits::PositionValid;
            const xr::SpaceLocationFlags requiredFlags =
                xr::SpaceLocationFlags{ xr::SpaceLocationFlagBits::PositionValid } | xr::SpaceLocationFlags{ xr::SpaceLocationFlagBits::OrientationValid };
            if (spaceLocation.locationFlags & requiredFlags) {
                result[hand] = xrs::toGlm(spaceLocation.pose);
            }
        });
        return result;
    }
};

class OpenXrExample : public VrExample {
    using Parent = VrExample;

public:
    xrs::Context _xr;
    InputState _xrInput;

    vk::Semaphore blitComplete;
    std::vector<vk::CommandBuffer> openxrBlitCommands;
    std::vector<vk::CommandBuffer> mirrorBlitCommands;

    void logActionSourceName(const xr::Action& action, const std::string& actionName) {
        std::vector<xr::Path> paths = _xr.session.enumerateBoundSourcesForAction({ action });

        std::string sourceName;
        for (const auto& path : paths) {
            sourceName += _xr.session.getInputSourceLocalizedName({ path, xr::InputSourceLocalizedNameFlagBits::AllBits });
        }

        auto message = FORMAT("{} action is bound to {}", actionName.c_str(), ((sourceName.size() > 0) ? sourceName.c_str() : " nothing"));
        OutputDebugString(message.c_str());
        OutputDebugString("\n");
    }

    OpenXrExample() {
        // Startup the OpenXR instance and get a system ID and view configuration
        // All of this is independent of the interaction between Xr and the
        // eventual Graphics API used for rendering
        _xr.create();

        _xrInput.initialize(_xr.instance);
        _xr.interactionProfileChangedHandler = [this](const xr::EventDataInteractionProfileChanged& event) {
            logActionSourceName(_xrInput.grabAction, "Grab");
            logActionSourceName(_xrInput.quitAction, "Quit");
            logActionSourceName(_xrInput.poseAction, "Pose");
            logActionSourceName(_xrInput.vibrateAction, "Vibrate");
            logActionSourceName(_xrInput.moveAction, "Move");
        };

        // Set up interaction between OpenXR and Vulkan
        // This work MUST happen before you create a Vulkan instance, since
        // OpenXR may require specific Vulkan instance and device extensions
        context.requireExtensions(_xr.getVulkanInstanceExtensions());
        context.requireDeviceExtensions(_xr.getVulkanDeviceExtensions());

        // Our example Vulkan abstraction allows a client to select a specific vk::PhysicalDevice via a DevicePicker callback
        // This is critical because the HMD will ultimately be dependent on the specific GPU to which it is
        // attached
        // Note that we don't and can't actually determine the target vk::PhysicalDevice right now because
        // vk::Instance::getVulkanGraphicsDeviceKHR depends on a passed VkInstance, hence the use of a callback which will be executed
        // later, once the
        context.setDevicePicker([this](const std::vector<vk::PhysicalDevice>& availableDevices) -> vk::PhysicalDevice {
            vk::PhysicalDevice targetDevice;
            {
                VkPhysicalDevice targetDeviceRaw;
                _xr.instance.getVulkanGraphicsDeviceKHR(_xr.systemId, context.instance, &targetDeviceRaw, _xr.dispatch);
                targetDevice = targetDeviceRaw;
            }
            for (const auto& availableDevice : availableDevices) {
                if (availableDevice == targetDevice) {
                    return availableDevice;
                }
            }
            throw std::runtime_error("Requested device not found");
        });

        // The initialization of the parent class depends on the renderTargetSize so it can create a desktop window with the same aspect ratio
        // as the offscreen framebuffer
        renderTargetSize = { _xr.viewConfigViews[0].recommendedImageRectWidth * 2, _xr.viewConfigViews[0].recommendedImageRectHeight };
    }

    ~OpenXrExample() { _xr.destroy(); }

    void recenter() override {}

    void prepareOpenXrSession() {
        _xr.createSession(xr::GraphicsBindingVulkanKHR{ context.instance, context.physicalDevice, context.device, context.queueIndices.graphics, 0 });
        _xrInput.attach(_xr.session);
        _xr.createVulkanSwapchain(renderTargetSize);

        auto swapchainLength = (uint32_t)_xr.vulkanSwapchainImages.size();
        // Submission command buffers
        if (openxrBlitCommands.empty()) {
            vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
            cmdBufAllocateInfo.commandPool = context.getCommandPool();
            cmdBufAllocateInfo.commandBufferCount = swapchainLength;
            openxrBlitCommands = context.device.allocateCommandBuffers(cmdBufAllocateInfo);
        }

        xr::for_each_side_index([&](uint32_t eyeIndex) {
            auto& layerView = _xr.projectionLayerViews[eyeIndex];
            layerView.subImage.swapchain = _xr.swapchain;
            layerView.subImage.imageRect.extent = { (int32_t)renderTargetSize.x / 2, (int32_t)renderTargetSize.y };
            if (eyeIndex == 1) {
                layerView.subImage.imageRect.offset.x = layerView.subImage.imageRect.extent.width;
            }
        });

        vk::ImageBlit sceneBlit;
        sceneBlit.dstSubresource.aspectMask = sceneBlit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        sceneBlit.dstSubresource.layerCount = sceneBlit.srcSubresource.layerCount = 1;
        sceneBlit.dstOffsets[1] = sceneBlit.srcOffsets[1] = vk::Offset3D{ (int32_t)renderTargetSize.x, (int32_t)renderTargetSize.y, 1 };
        for (uint32_t i = 0; i < swapchainLength; ++i) {
            vk::CommandBuffer& cmdBuffer = openxrBlitCommands[i];
            VkImage swapchainImage = _xr.vulkanSwapchainImages[i].image;
            cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
            cmdBuffer.begin(vk::CommandBufferBeginInfo{});
            context.setImageLayout(cmdBuffer, swapchainImage, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eUndefined,
                                   vk::ImageLayout::eTransferDstOptimal);
            cmdBuffer.blitImage(shapesRenderer->framebuffer.colors[0].image, vk::ImageLayout::eTransferSrcOptimal, swapchainImage,
                                vk::ImageLayout::eTransferDstOptimal, sceneBlit, vk::Filter::eNearest);
            context.setImageLayout(cmdBuffer, swapchainImage, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eTransferDstOptimal,
                                   vk::ImageLayout::eTransferSrcOptimal);
            cmdBuffer.end();
        }
    }

    void prepareMirror() {
        if (mirrorBlitCommands.empty()) {
            vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
            cmdBufAllocateInfo.commandPool = context.getCommandPool();
            cmdBufAllocateInfo.commandBufferCount = swapchain.imageCount;
            mirrorBlitCommands = context.device.allocateCommandBuffers(cmdBufAllocateInfo);
        }

        vk::ImageBlit mirrorBlit;
        mirrorBlit.dstSubresource.aspectMask = mirrorBlit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        mirrorBlit.dstSubresource.layerCount = mirrorBlit.srcSubresource.layerCount = 1;
        mirrorBlit.srcOffsets[1] = { (int32_t)renderTargetSize.x, (int32_t)renderTargetSize.y, 1 };
        mirrorBlit.dstOffsets[1] = { (int32_t)size.x, (int32_t)size.y, 1 };

        for (size_t i = 0; i < swapchain.imageCount; ++i) {
            vk::CommandBuffer& cmdBuffer = mirrorBlitCommands[i];
            cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
            cmdBuffer.begin(vk::CommandBufferBeginInfo{});
            context.setImageLayout(cmdBuffer, swapchain.images[i].image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eUndefined,
                                   vk::ImageLayout::eTransferDstOptimal);
            cmdBuffer.blitImage(shapesRenderer->framebuffer.colors[0].image, vk::ImageLayout::eTransferSrcOptimal, swapchain.images[i].image,
                                vk::ImageLayout::eTransferDstOptimal, mirrorBlit, vk::Filter::eNearest);
            context.setImageLayout(cmdBuffer, swapchain.images[i].image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eTransferDstOptimal,
                                   vk::ImageLayout::ePresentSrcKHR);
            cmdBuffer.end();
        }
    }

    void prepare() {
        context.setValidationEnabled(true);
        Parent::prepare();
        prepareMirror();
        prepareOpenXrSession();
        blitComplete = context.device.createSemaphore({});
    }

    glm::vec3 translation;

    void update(float delta) {
        if (_xr.stopped) {
            requestClose();
        }
        _xr.pollEvents();

        _xrInput.pollActions(_xr.state, _xr.session);

        translation += glm::vec3(-_xrInput.moveAmount.x, 0.0f, _xrInput.moveAmount.y) * 0.01f;

        _xr.onFrameStart();

        if (_xr.shouldRender()) {
            _xrInput.getHandPoses(_xr.space, _xr.frameState.predictedDisplayTime);

            xr::ViewState vs;
            xr::ViewLocateInfo vi{ xr::ViewConfigurationType::PrimaryStereo, _xr.frameState.predictedDisplayTime, _xr.space };
            _xr.eyeViewStates = _xr.session.locateViews(vi, &(vs.operator XrViewState&()));

            xr::for_each_side_index([&](size_t eyeIndex) {
                const auto& viewState = _xr.eyeViewStates[eyeIndex];
                eyeProjections[eyeIndex] = xrs::toGlm(viewState.fov);
                eyeViews[eyeIndex] = glm::inverse(xrs::toGlm(viewState.pose)) * glm::translate({}, translation);
            });
            //XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
            //_xr.space.locateSpace({}, _xr.frameState.predictedDisplayTime, &spaceLocation);
        }

        Parent::update(delta);
    }

    void render() {
        if (!_xr.shouldRender()) {
            if (_xr.beginFrameResult == xr::Result::Success) {
                _xr.session.endFrame(xr::FrameEndInfo{ _xr.frameState.predictedDisplayTime, xr::EnvironmentBlendMode::Opaque });
            }
            return;
        }

        uint32_t swapchainIndex = (uint32_t)-1;
        _xr.swapchain.acquireSwapchainImage(xr::SwapchainImageAcquireInfo{}, &swapchainIndex);
        _xr.swapchain.waitSwapchainImage(xr::SwapchainImageWaitInfo{ xr::Duration::infinite() });

        shapesRenderer->render();

        // Blit from our framebuffer to the OpenXR swapchain image (pre-recorded command buffer)
        context.submit(openxrBlitCommands[swapchainIndex],
                       { { shapesRenderer->semaphores.renderComplete, vk::PipelineStageFlagBits::eColorAttachmentOutput } });
        _xr.swapchain.releaseSwapchainImage(xr::SwapchainImageReleaseInfo{});

        uint32_t layerFlags = 0;
        xr::for_each_side_index([&](size_t eyeIndex) {
            auto& layerView = _xr.projectionLayerViews[eyeIndex];
            const auto& eyeView = _xr.eyeViewStates[eyeIndex];
            layerView.fov = eyeView.fov;
            layerView.pose = eyeView.pose;
        });

        _xr.session.endFrame(xr::FrameEndInfo{ _xr.frameState.predictedDisplayTime, xr::EnvironmentBlendMode::Opaque, (uint32_t)_xr.layersPointers.size(),
                                               (const xr::CompositionLayerBaseHeader* const*)_xr.layersPointers.data() });

        vk::Fence submitFence = swapchain.getSubmitFence(true);
        auto swapchainAcquireResult = swapchain.acquireNextImage(shapesRenderer->semaphores.renderStart);
        swapchainIndex = swapchainAcquireResult.value;
        context.submit(mirrorBlitCommands[swapchainIndex], {}, {}, blitComplete, submitFence);
        swapchain.queuePresent(blitComplete);
    }

    std::string getWindowTitle() {
        std::string device(context.deviceProperties.deviceName);
        return "OpenXR SDK Example " + device + " - " + std::to_string((int)lastFPS) + " fps";
    }
};

RUN_EXAMPLE(OpenXrExample)
