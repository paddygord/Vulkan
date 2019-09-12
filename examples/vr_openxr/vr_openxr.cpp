#include <vr/vr_common.hpp>
#include <math.h>
#include "openxr.hpp"

namespace xrs {

// Convenience method for looping over each eye with a lambda
template <typename Function>
inline void for_each_eye(Function function) {
    for (size_t eye = 0; eye < 2; ++eye) {
        function(eye);
    }
}

inline XrFovf toTanFovf(const XrFovf& fov) {
    return  { tanf(fov.angleLeft), tanf(fov.angleRight), tanf(fov.angleUp), tanf(fov.angleDown) };
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
    //    return glm::frustumLH_ZO(fabs(fov.angleLeft), fabs(fov.angleRight), fabs(fov.angleDown), fabs(fov.angleUp), zNear, zFar);
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

}  // namespace xrs

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

class OpenXrExample : public VrExample {
    using Parent = VrExample;

public:
    struct XrStuff {
        xr::DispatchLoaderDynamic dispatch;
        xr::Instance instance;
        xr::Session session;
        xr::Space space;
        xr::Swapchain swapchain;
        XrSystemId systemId;
        XrExtent2Df bounds;
        xr::SessionState state{ xr::SessionState::Idle };
        XrFrameState frameState{ XR_TYPE_FRAME_STATE, nullptr };
        xr::Result beginFrameResult{ xr::Result::FrameDiscarded };

        std::array<XrView, 2> eyeViewStates;
        std::vector<XrSwapchainImageVulkanKHR> swapchainImages;

        XrCompositionLayerProjection projectionLayer;
        std::array<XrCompositionLayerProjectionView, 2> projectionLayerViews;
        std::vector<XrCompositionLayerBaseHeader*> layersPointers;

        bool shouldRender() const { return beginFrameResult == xr::Result::Success && frameState.shouldRender; }
    } _xr;

    vk::Semaphore blitComplete;
    std::vector<vk::CommandBuffer> openxrBlitCommands;
    std::vector<vk::CommandBuffer> mirrorBlitCommands;

    static XrBool32 debugCallback(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
                                  XrDebugUtilsMessageTypeFlagsEXT messageTypes,
                                  const XrDebugUtilsMessengerCallbackDataEXT* callbackData,
                                  void* userData) {
        return static_cast<OpenXrExample*>(userData)->onValidationMessage(messageSeverity, messageTypes, callbackData);
    }

    XrBool32 onValidationMessage(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
                                 XrDebugUtilsMessageTypeFlagsEXT messageTypes,
                                 const XrDebugUtilsMessengerCallbackDataEXT* callbackData) {
        OutputDebugStringA(callbackData->message);
        OutputDebugStringA("\n");
        return XR_TRUE;
    }

    OpenXrExample() {
        auto layersProperties = xr::enumerateApiLayerProperties();
        for (const auto& layerProperties : layersProperties) {
            std::cout << layerProperties.layerName << std::endl;
            auto extensionsProperties = xr::enumerateInstanceExtensionProperties(layerProperties.layerName);
            for (const auto& extensionProperties : extensionsProperties) {
                std::cout << "\t" << extensionProperties.extensionName << std::endl;
            }
        }
        auto extensionsProperties = xr::enumerateInstanceExtensionProperties(nullptr);
        for (const auto& extensionProperties : extensionsProperties) {
            std::cout << "\t" << extensionProperties.extensionName << std::endl;
        }

        vks::CStringVector extensions;
        //extensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
        //extensions.push_back(XR_KHR_VULKAN_SWAPCHAIN_FORMAT_LIST_EXTENSION_NAME);
        extensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);

        {
            XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO, nullptr };
            ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
            strcpy(ici.applicationInfo.applicationName, "vr_openxr");
            ici.applicationInfo.applicationVersion = 0;
            strcpy(ici.applicationInfo.engineName, "jherico");
            ici.applicationInfo.engineVersion = 0;
            ici.enabledExtensionCount = (uint32_t)extensions.size();
            ici.enabledExtensionNames = extensions.data();

            //static constexpr auto XR_ALL_SEVERITIES = XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            //                                          XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            //static constexpr auto XR_ALL_TYPES = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            //                                     XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
            //XrDebugUtilsMessengerCreateInfoEXT dumci{ XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            //dumci.messageSeverities = XR_ALL_SEVERITIES;
            //dumci.messageTypes = XR_ALL_TYPES;
            //dumci.userData = this;
            //dumci.userCallback = &OpenXrExample::debugCallback;
            //ici.next = &dumci;

            _xr.instance = xr::createInstance(&ici);
            _xr.dispatch = xr::DispatchLoaderDynamic::createFullyPopulated(_xr.instance, &xrGetInstanceProcAddr);
        }

        {
            XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO, nullptr, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY };
            _xr.instance.getSystem(&sgi, &_xr.systemId);
        }

        {
            XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
            _xr.instance.getInstanceProperties(&ip);
            XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
            _xr.instance.getSystemProperties(_xr.systemId, &sp);

            uint32_t count;
            std::vector<xr::ViewConfigurationType> viewConfigTypes;
            xrEnumerateViewConfigurations(_xr.instance, _xr.systemId, 0, &count, nullptr);
            viewConfigTypes.resize(count);
            xrEnumerateViewConfigurations(_xr.instance, _xr.systemId, count, &count, (XrViewConfigurationType*)viewConfigTypes.data());

            XrViewConfigurationProperties vp{ XR_TYPE_VIEW_CONFIGURATION_PROPERTIES };
            _xr.instance.getViewConfigurationProperties(_xr.systemId, viewConfigTypes[0], &vp);
            auto views = _xr.instance.enumerateViewConfigurationViews(_xr.systemId, viewConfigTypes[0]);

            const auto& viewConfig = views[0];
            renderTargetSize = { viewConfig.recommendedImageRectWidth * 2, viewConfig.recommendedImageRectHeight };
        }

        // Find out the Vulkan interaction requirements (instance and device extensions required)
        {
            auto instanceExtensions = splitCharBuffer<>(_xr.instance.getVulkanInstanceExtensionsKHR(_xr.systemId, _xr.dispatch));
            context.requireExtensions(instanceExtensions);
            auto deviceExtensions = splitCharBuffer<>(_xr.instance.getVulkanDeviceExtensionsKHR(_xr.systemId, _xr.dispatch));
            context.requireDeviceExtensions(deviceExtensions);
        }

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
    }

    ~OpenXrExample() { shutdownOpenXr(); }

    void recenter() override {}

    void shutdownOpenXrSession() {
        if (_xr.swapchain) {
            _xr.swapchain.destroy();
            _xr.swapchain = nullptr;
        }
        if (_xr.session) {
            _xr.session.destroy();
            _xr.session = nullptr;
        }
    }

    void shutdownOpenXr() {
        shutdownOpenXrSession();
        if (_xr.instance) {
            _xr.instance.destroy();
            _xr.instance = nullptr;
        }
    }

    void prepareOpenXrSession() {
        {
            //auto graphicsRequirements = xr::getVulkanGraphicsRequirementsKHR(_xr.instance, _xr.systemId);

            XrGraphicsBindingVulkanKHR gbv{ XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR, nullptr };
            gbv.instance = context.instance;
            gbv.physicalDevice = context.physicalDevice;
            gbv.device = context.device;
            gbv.queueFamilyIndex = context.queueIndices.graphics;
            gbv.queueIndex = 0;

            XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO, &gbv, 0, _xr.systemId };
            _xr.session = _xr.instance.createSession(&sci);
        }

        auto referenceSpaces = _xr.session.enumerateReferenceSpaces().value;
        auto referenceSpace = xr::ReferenceSpaceType::Local;
        XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO, nullptr, (XrReferenceSpaceType)referenceSpace };
        referenceSpaceCreateInfo.poseInReferenceSpace.orientation = { 0, 0, 0, 1 };
        _xr.space = _xr.session.createReferenceSpace(&referenceSpaceCreateInfo).value;
        _xr.session.getReferenceSpaceBoundsRect(referenceSpace, &_xr.bounds);
        _xr.projectionLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION, nullptr, 0, _xr.space, 2, _xr.projectionLayerViews.data() };
        _xr.layersPointers.push_back((XrCompositionLayerBaseHeader*)&_xr.projectionLayer);

        std::vector<vk::Format> swapchainFormats;
        {
            auto swapchainFormatsRaw = _xr.session.enumerateSwapchainFormats().value;
            swapchainFormats.reserve(swapchainFormatsRaw.size());
            for (const auto& format : swapchainFormatsRaw) {
                swapchainFormats.push_back((vk::Format)format);
            }
        }

        {
            XrSwapchainCreateInfo scci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
            scci.format = (VkFormat)vk::Format::eB8G8R8A8Srgb;
            scci.arraySize = 1;
            scci.faceCount = 1;
            scci.width = renderTargetSize.x;
            scci.height = renderTargetSize.y;
            scci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            scci.sampleCount = 1;
            scci.mipCount = 1;
            _xr.swapchain = _xr.session.createSwapchain(&scci).value;
        }

        {
            uint32_t count = 0;
            xrEnumerateSwapchainImages(_xr.swapchain, 0, &count, nullptr);
            _xr.swapchainImages.resize(count, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
            xrEnumerateSwapchainImages(_xr.swapchain, count, &count, (XrSwapchainImageBaseHeader*)_xr.swapchainImages.data());
        }

        auto swapchainLength = (uint32_t)_xr.swapchainImages.size();
        // Submission command buffers
        if (openxrBlitCommands.empty()) {
            vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
            cmdBufAllocateInfo.commandPool = context.getCommandPool();
            cmdBufAllocateInfo.commandBufferCount = swapchainLength;
            openxrBlitCommands = context.device.allocateCommandBuffers(cmdBufAllocateInfo);
        }

        xrs::for_each_eye([&](size_t eyeIndex) {
            auto& layerView = _xr.projectionLayerViews[eyeIndex];
            layerView = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            layerView.subImage.swapchain = _xr.swapchain;
            layerView.subImage.imageRect = { { 0, 0 }, { (int32_t)renderTargetSize.x / 2, (int32_t)renderTargetSize.y } };
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
            VkImage swapchainImage = _xr.swapchainImages[i].image;
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

    void onSessionStateChanged(const XrEventDataSessionStateChanged& sessionStateChangedEvent) {
        _xr.state = (xr::SessionState)sessionStateChangedEvent.state;
        std::cout << "Session state " << (uint32_t)_xr.state << std::endl;
        switch (_xr.state) {
            case xr::SessionState::Ready: {
                XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO, nullptr };
                beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                std::cout << "Starting session" << std::endl;
                _xr.session.beginSession(&beginInfo);
            } break;

            case xr::SessionState::Stopping: {
                std::cout << "Stopping session" << std::endl;
                _xr.session.endSession();
            } break;

            case xr::SessionState::Exiting:
            case xr::SessionState::LossPending: {
                std::cout << "Destroying session" << std::endl;
                _xr.session.destroy();
                _xr.session = nullptr;
            } break;

            default:
                break;
        }
    }

    void onInstanceLossPending(const XrEventDataInstanceLossPending& instanceLossPendingEvent) {}

    void update(float delta) {
        while (true) {
            XrEventDataBuffer eventBuffer{ XR_TYPE_EVENT_DATA_BUFFER, nullptr };
            auto pollResult = _xr.instance.pollEvent(&eventBuffer);
            if (pollResult == xr::Result::EventUnavailable) {
                break;
            }

            switch (eventBuffer.type) {
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    onSessionStateChanged(reinterpret_cast<XrEventDataSessionStateChanged&>(eventBuffer));
                    break;
                }
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                    onInstanceLossPending(reinterpret_cast<XrEventDataInstanceLossPending&>(eventBuffer));
                    break;
                }
            }
        }

        _xr.beginFrameResult = xr::Result::FrameDiscarded;
        switch (_xr.state) {
            case xr::SessionState::Focused: {
                //XrActionsSyncInfo actionsSyncInfo{ XR_TYPE_ACTIONS_SYNC_INFO, nullptr, 0, nullptr };
                //_xr.session.syncActions(&actionsSyncInfo);
            }
                // fallthough
            case xr::SessionState::Synchronized:
            case xr::SessionState::Visible: {
                XrFrameWaitInfo frameWaitInfo{ XR_TYPE_FRAME_WAIT_INFO, nullptr };
                if (xr::Result::SessionLossPending == _xr.session.waitFrame(&frameWaitInfo, &_xr.frameState)) {
                    std::cout << "Session loss pending" << std::endl;
                    return;
                }
                XrFrameBeginInfo frameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO, nullptr };
                _xr.beginFrameResult = _xr.session.beginFrame(&frameBeginInfo);
                switch (_xr.beginFrameResult) {
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
                _xr.beginFrameResult = xr::Result::FrameDiscarded;
                Sleep(100);
        }

        if (_xr.shouldRender()) {
            XrViewState vs{ XR_TYPE_VIEW_STATE };
            XrViewLocateInfo vi{ XR_TYPE_VIEW_LOCATE_INFO, nullptr, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, _xr.frameState.predictedDisplayTime, _xr.space };
            std::vector<XrView> viewStates = _xr.session.locateViews(&vi, &vs).value;

            xrs::for_each_eye([&](size_t eyeIndex) {
                const auto& viewState = _xr.eyeViewStates[eyeIndex] = viewStates[eyeIndex];
                eyeProjections[eyeIndex] = xrs::toGlm(viewState.fov);
                eyeViews[eyeIndex] = glm::inverse(xrs::toGlm(viewState.pose));
            });
            //XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
            //_xr.space.locateSpace({}, _xr.frameState.predictedDisplayTime, &spaceLocation);
        }

        Parent::update(delta);
    }

    void render() {
        if (!_xr.shouldRender()) {
            if (_xr.beginFrameResult == xr::Result::Success) {
                XrFrameEndInfo frameEndInfo{ XR_TYPE_FRAME_END_INFO, nullptr, _xr.frameState.predictedDisplayTime,  XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
                _xr.session.endFrame(&frameEndInfo);
            }
            return;
        }

        uint32_t swapchainIndex = (uint32_t)-1;
        static const XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        _xr.swapchain.acquireSwapchainImage(&ai, &swapchainIndex);

        static const XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION };
        _xr.swapchain.waitSwapchainImage(&wi);

        shapesRenderer->render();

        // Blit from our framebuffer to the OpenXR swapchain image (pre-recorded command buffer)
        context.submit(openxrBlitCommands[swapchainIndex],
                        { { shapesRenderer->semaphores.renderComplete, vk::PipelineStageFlagBits::eColorAttachmentOutput } });
        static const XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        _xr.swapchain.releaseSwapchainImage(&ri);

        uint32_t layerFlags = 0;
        xrs::for_each_eye([&](size_t eyeIndex) {
            auto& layerView = _xr.projectionLayerViews[eyeIndex];
            const auto& eyeView = _xr.eyeViewStates[eyeIndex];
            layerView.fov = eyeView.fov;
            layerView.pose = eyeView.pose;
        });

        XrFrameEndInfo frameEndInfo{ XR_TYPE_FRAME_END_INFO, nullptr, _xr.frameState.predictedDisplayTime, XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
        frameEndInfo.layerCount = (uint32_t)_xr.layersPointers.size();
        frameEndInfo.layers = _xr.layersPointers.data();
        _xr.session.endFrame(&frameEndInfo);

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
