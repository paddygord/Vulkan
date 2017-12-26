/*
* Vulkan Example base class
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/
#include "vulkanExampleBase.h"

using namespace vkx;

// Avoid doing work in the ctor as it can't make use of overridden virtual functions
// Instead, use the `run` method
ExampleBase::ExampleBase() {
#if defined(__ANDROID__)
    global_android_app->userData = this;
    global_android_app->onInputEvent = ExampleBase::handle_input_event;
    global_android_app->onAppCmd = ExampleBase::handle_app_cmd;
#endif
}

ExampleBase::~ExampleBase() {
    // Clean up Vulkan resources
    swapChain.destroy();
    // FIXME destroy surface
    if (descriptorPool) {
        device.destroyDescriptorPool(descriptorPool);
    }
    if (!primaryCmdBuffers.empty()) {
        device.freeCommandBuffers(cmdPool, primaryCmdBuffers);
        primaryCmdBuffers.clear();
    }
    if (!drawCmdBuffers.empty()) {
        device.freeCommandBuffers(cmdPool, drawCmdBuffers);
        drawCmdBuffers.clear();
    }
    device.destroyRenderPass(renderPass);
    for (uint32_t i = 0; i < framebuffers.size(); i++) {
        device.destroyFramebuffer(framebuffers[i]);
    }

    for (auto& shaderModule : context.shaderModules) {
        device.destroyShaderModule(shaderModule);
    }
    depthStencil.destroy();

    device.destroySemaphore(semaphores.acquireComplete);
    device.destroySemaphore(semaphores.renderComplete);

    context.destroyContext();

#if defined(__ANDROID__)
    // todo : android cleanup (if required)
#else
    glfwDestroyWindow(window);
    glfwTerminate();
#endif
}

void ExampleBase::run() {
// Android initialization is handled in APP_CMD_INIT_WINDOW event
#if !defined(__ANDROID__)
    glfwInit();
    initVulkan();
    setupWindow();
    prepare();
#endif

    renderLoop();

    // Once we exit the render loop, wait for everything to become idle before proceeding to the descructor.
    context.queue.waitIdle();
    context.device.waitIdle();
}

void ExampleBase::initVulkan() {
#ifndef NDEBUG
    context.setValidationEnabled(true);
#endif
    context.requireExtensions(glfw::getRequiredInstanceExtensions());
    context.create();

    swapChain.setup(context.physicalDevice, context.device, context.queue);

    // Find a suitable depth format
    depthFormat = context.getSupportedDepthFormat();

    // Create synchronization objects

    // A semaphore used to synchronize image presentation
    // Ensures that the image is displayed before we start submitting new commands to the queu
    semaphores.acquireComplete = device.createSemaphore({});
    // A semaphore used to synchronize command submission
    // Ensures that the image is not presented until all commands have been sumbitted and executed
    semaphores.renderComplete = device.createSemaphore({});

    // Set up submit info structure
    // Semaphores will stay the same during application lifetime
    // Command buffer submission info is set by each example
    submitInfo = vk::SubmitInfo();
    submitInfo.pWaitDstStageMask = &submitPipelineStages;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &semaphores.acquireComplete;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &semaphores.renderComplete;
}

bool ExampleBase::platformLoopCondition() {
#if defined(__ANDROID__)
    bool destroy = false;
    focused = true;
    int ident, events;
    struct android_poll_source* source;
    while (!destroy && (ident = ALooper_pollAll(focused ? 0 : -1, NULL, &events, (void**)&source)) >= 0) {
        if (source != NULL) {
            source->process(global_android_app, source);
        }
        destroy =  global_android_app->destroyRequested != 0
    }

    // App destruction requested
    // Exit loop, example will be destroyed in application main
    return !destroy;
#else
    if (glfwWindowShouldClose(window)) {
        return false;
    }

    glfwPollEvents();

    if (glfwJoystickPresent(0)) {
        // FIXME implement joystick handling
        int axisCount{ 0 };
        const float* axes = glfwGetJoystickAxes(0, &axisCount);
        if (axisCount >= 2) {
            gamePadState.axes.x = axes[0] * 0.01f;
            gamePadState.axes.y = axes[1] * -0.01f;
        }
        if (axisCount >= 4) {
        }
        if (axisCount >= 6) {
            float lt = (axes[4] + 1.0f) / 2.0f;
            float rt = (axes[5] + 1.0f) / 2.0f;
            gamePadState.axes.rz = (rt - lt);
        }
        uint32_t newButtons{ 0 };
        static uint32_t oldButtons{ 0 };
        {
            int buttonCount{ 0 };
            const uint8_t* buttons = glfwGetJoystickButtons(0, &buttonCount);
            for (uint8_t i = 0; i < buttonCount && i < 64; ++i) {
                if (buttons[i]) {
                    newButtons |= (1 << i);
                }
            }
        }
        auto changedButtons = newButtons & ~oldButtons;
        if (changedButtons & 0x01) {
            keyPressed(GAMEPAD_BUTTON_A);
        }
        if (changedButtons & 0x02) {
            keyPressed(GAMEPAD_BUTTON_B);
        }
        if (changedButtons & 0x04) {
            keyPressed(GAMEPAD_BUTTON_X);
        }
        if (changedButtons & 0x08) {
            keyPressed(GAMEPAD_BUTTON_Y);
        }
        if (changedButtons & 0x10) {
            keyPressed(GAMEPAD_BUTTON_L1);
        }
        if (changedButtons & 0x20) {
            keyPressed(GAMEPAD_BUTTON_R1);
        }
        oldButtons = newButtons;
    } else {
        memset(&gamePadState.axes, 0, sizeof(gamePadState.axes));
    }
    return true;
#endif
}

void ExampleBase::renderLoop() {
    auto tStart = std::chrono::high_resolution_clock::now();

    while (platformLoopCondition()) {
        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<float, std::milli>(tEnd - tStart).count();
        auto tDiffSeconds = tDiff / 1000.0f;
        tStart = tEnd;

        // Render frame
        if (prepared) {
            render();
            update(tDiffSeconds);
        }
    }
}

std::string ExampleBase::getWindowTitle() {
    std::string device(context.deviceProperties.deviceName);
    std::string windowTitle;
    windowTitle = title + " - " + device + " - " + std::to_string(frameCounter) + " fps";
    return windowTitle;
}

void ExampleBase::prepare() {
    if (context.enableValidation) {
        vks::debug::setupDebugging(context.instance, vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning);
    }
    if (enableDebugMarkers) {
        vks::debug::marker::setup(device);
    }
    cmdPool = context.getCommandPool();

    swapChain.create(size, enableVsync);
    setupDepthStencil();
    setupRenderPass();
    setupRenderPassBeginInfo();
    setupFrameBuffer();

    prepared = true;
}

#if 0
MeshBuffer ExampleBase::loadMesh(const std::string& filename, const MeshLayout& vertexLayout, float scale) {
    MeshLoader loader;
    loader.load(filename);
    assert(loader.m_Entries.size() > 0);
    return loader.createBuffers(context, vertexLayout, scale);
}
#endif

vk::SubmitInfo ExampleBase::prepareSubmitInfo(
    const std::vector<vk::CommandBuffer>& commandBuffers,
    vk::PipelineStageFlags *pipelineStages) {
    vk::SubmitInfo submitInfo;
    submitInfo.pWaitDstStageMask = pipelineStages;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &semaphores.acquireComplete;
    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    submitInfo.pCommandBuffers = commandBuffers.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &semaphores.renderComplete;
    return submitInfo;
}

#if 0
void ExampleBase::updateTextOverlay() {
    if (!enableTextOverlay)
        return;

    textOverlay->beginTextUpdate();
    textOverlay->addText(title, 5.0f, 5.0f, TextOverlay::alignLeft);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << (frameTimer * 1000.0f) << "ms (" << lastFPS << " fps)";
    textOverlay->addText(ss.str(), 5.0f, 25.0f, TextOverlay::alignLeft);
    textOverlay->addText(context.deviceProperties.deviceName, 5.0f, 45.0f, TextOverlay::alignLeft);
    getOverlayText(textOverlay.get());
    textOverlay->endTextUpdate();

    context.trashCommandBuffers(textCmdBuffers);
    populateSubCommandBuffers(textCmdBuffers, [&](const vk::CommandBuffer& cmdBuffer) {
        textOverlay->writeCommandBuffer(cmdBuffer);
    });
    primaryCmdBuffersDirty = true;
}

void ExampleBase::getOverlayText(vkx::TextOverlay* textOverlay) {
    // Can be overriden in derived class
}
#endif

void ExampleBase::prepareFrame() {
    if (primaryCmdBuffersDirty) {
        buildCommandBuffers();
    }
    // Acquire the next image from the swap chaing
    currentBuffer = swapChain.acquireNextImage(semaphores.acquireComplete);
}

void ExampleBase::submitFrame() {
    swapChain.queuePresent(semaphores.renderComplete);
}

void ExampleBase::setupDepthStencil() {
    depthStencil.destroy();

    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    vk::ImageCreateInfo depthStencilCreateInfo;
    depthStencilCreateInfo.imageType = vk::ImageType::e2D;
    depthStencilCreateInfo.extent = vk::Extent3D{ size.width, size.height, 1 };
    depthStencilCreateInfo.format = depthFormat;
    depthStencilCreateInfo.mipLevels = 1;
    depthStencilCreateInfo.arrayLayers = 1;
    depthStencilCreateInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc;
    depthStencil = context.createImage(depthStencilCreateInfo);

    context.setImageLayout(depthStencil.image, aspect, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::ImageViewCreateInfo depthStencilView;
    depthStencilView.viewType = vk::ImageViewType::e2D;
    depthStencilView.format = depthFormat;
    depthStencilView.subresourceRange.aspectMask = aspect;
    depthStencilView.subresourceRange.levelCount = 1;
    depthStencilView.subresourceRange.layerCount = 1;
    depthStencilView.image = depthStencil.image;
    depthStencil.view = device.createImageView(depthStencilView);
}

void ExampleBase::setupFrameBuffer() {
    // Recreate the frame buffers
    if (!framebuffers.empty()) {
        for (uint32_t i = 0; i < framebuffers.size(); i++) {
            device.destroyFramebuffer(framebuffers[i]);
        }
        framebuffers.clear();
    }

    vk::ImageView attachments[2];

    // Depth/Stencil attachment is the same for all frame buffers
    attachments[1] = depthStencil.view;

    vk::FramebufferCreateInfo framebufferCreateInfo;
    framebufferCreateInfo.renderPass = renderPass;
    framebufferCreateInfo.attachmentCount = 2;
    framebufferCreateInfo.pAttachments = attachments;
    framebufferCreateInfo.width = size.width;
    framebufferCreateInfo.height = size.height;
    framebufferCreateInfo.layers = 1;

    // Create frame buffers for every swap chain image
    framebuffers = swapChain.createFramebuffers(framebufferCreateInfo);
}

void ExampleBase::setupRenderPass() {
    if (renderPass) {
        device.destroyRenderPass(renderPass);
    }

    std::vector<vk::AttachmentDescription> attachments;
    attachments.resize(2);

    // Color attachment
    attachments[0].format = colorformat;
    attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
    attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
    attachments[0].initialLayout = vk::ImageLayout::eUndefined;
    attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

    // Depth attachment
    attachments[1].format = depthFormat;
    attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
    attachments[1].storeOp = vk::AttachmentStoreOp::eDontCare;
    attachments[1].initialLayout = vk::ImageLayout::eUndefined;
    attachments[1].finalLayout = vk::ImageLayout::eUndefined;

    // Only one depth attachment, so put it first in the references
    vk::AttachmentReference depthReference;
    depthReference.attachment = 1;
    depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    std::vector<vk::AttachmentReference> colorAttachmentReferences;
    {
        vk::AttachmentReference colorReference;
        colorReference.attachment = 0;
        colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachmentReferences.push_back(colorReference);
    }

    std::vector<vk::SubpassDescription> subpasses;
    std::vector<vk::SubpassDependency> subpassDependencies;
    {
        vk::SubpassDependency dependency;
        dependency.srcSubpass = 0;
        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        subpassDependencies.push_back(dependency);

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.pDepthStencilAttachment = &depthReference;
        subpass.colorAttachmentCount = (uint32_t)colorAttachmentReferences.size();
        subpass.pColorAttachments = colorAttachmentReferences.data();
        subpasses.push_back(subpass);
    }

    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = (uint32_t)attachments.size();
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = (uint32_t)subpasses.size();
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = (uint32_t)subpassDependencies.size();
    renderPassInfo.pDependencies = subpassDependencies.data();
    renderPass = device.createRenderPass(renderPassInfo);
}

void ExampleBase::windowResize(const glm::uvec2& newSize) {
    if (!prepared) {
        return;
    }
    prepared = false;

    queue.waitIdle();
    device.waitIdle();

    // Recreate swap chain
    size.width = newSize.x;
    size.height = newSize.y;
    camera.setAspectRatio(size);
    swapChain.create(size, enableVsync);

    setupDepthStencil();
    setupFrameBuffer();
#if 0
    if (enableTextOverlay && textOverlay->visible) {
        updateTextOverlay();
    }
#endif
    setupRenderPassBeginInfo();

    // Notify derived class
    windowResized();

    // Can be overriden in derived class
    updateDrawCommandBuffers();

    // Command buffers need to be recreated as they may store
    // references to the recreated frame buffer
    buildCommandBuffers();

    viewChanged();

    prepared = true;
}

void ExampleBase::windowResized() {}

const std::string& ExampleBase::getAssetPath() {
#if defined(__ANDROID__)
    static const std::string NOTHING;
    return NOTHING;
#else
    static std::string path;
    static std::once_flag once;

    std::call_once(once, [] {
        std::string file(__FILE__);
        std::replace(file.begin(), file.end(), '\\', '/');
        std::string::size_type lastSlash = file.rfind("/");
        file = file.substr(0, lastSlash);
        path = file + "/../data/";
    });
    return path;
#endif
}


#if defined(__ANDROID__)
int32_t ExampleBase::handle_input_event(android_app* app, AInputEvent *event) {
    ExampleBase *exampleBase = reinterpret_cast<ExampleBase *>(app->userData);
    return exampleBase->onInput(event);
}

void ExampleBase::handle_app_cmd(android_app* app, int32_t cmd) {
    ExampleBase *exampleBase = reinterpret_cast<ExampleBase *>(app->userData);
    exampleBase->onAppCmd(cmd);
}

int32_t ExampleBase::onInput(AInputEvent* event) {
    auto eventType = AInputEvent_getType(event);
    switch (eventType) {
    case AINPUT_EVENT_TYPE_MOTION:
        if (AInputEvent_getSource(event) == AINPUT_SOURCE_JOYSTICK) {
            gamePadState.axes.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            gamePadState.axes.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            gamePadState.axes.z = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            gamePadState.axes.rz = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
        }
        else {
            // todo : touch input
        }
        return 1;
    case AINPUT_EVENT_TYPE_KEY: {
        int32_t keyCode = AKeyEvent_getKeyCode((const AInputEvent *)event);
        int32_t action = AKeyEvent_getAction((const AInputEvent *)event);
        int32_t button = 0;
        if (action == AKEY_EVENT_ACTION_UP)
            return 0;

        switch (keyCode) {
        case AKEYCODE_BUTTON_A:
            keyPressed(GAMEPAD_BUTTON_A);
            break;
        case AKEYCODE_BUTTON_B:
            keyPressed(GAMEPAD_BUTTON_B);
            break;
        case AKEYCODE_BUTTON_X:
            keyPressed(GAMEPAD_BUTTON_X);
            break;
        case AKEYCODE_BUTTON_Y:
            keyPressed(GAMEPAD_BUTTON_Y);
            break;
        case AKEYCODE_BUTTON_L1:
            keyPressed(GAMEPAD_BUTTON_L1);
            break;
        case AKEYCODE_BUTTON_R1:
            keyPressed(GAMEPAD_BUTTON_R1);
            break;
        case AKEYCODE_BUTTON_START:
            keyPressed(GAMEPAD_BUTTON_START);
            break;
        };
    }
    }
    return 0;
}


void ExampleBase::onAppCmd(int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (global_android_app->window != nullptr) {
            initVulkan();
            setupWindow();
            prepare();
        }
        break;
    case APP_CMD_LOST_FOCUS:
        focused = false;
        break;
    case APP_CMD_GAINED_FOCUS:
        focused = true;
        break;
    default:
        break;
    }
}

void ExampleBase::setupWindow() {
    auto window = global_android_app->window;
    size.width = ANativeWindow_getWidth(window);
    size.height = ANativeWindow_getHeight(window);
    camera.setAspectRatio(size);
    swapChain.createSurface(window);
}

#else

void ExampleBase::KeyboardHandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    if (action == GLFW_PRESS) {
        example->keyPressed(key);
    }
}

void ExampleBase::MouseHandler(GLFWwindow* window, int button, int action, int mods) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    if (action == GLFW_PRESS) {
        glm::dvec2 mousePos; glfwGetCursorPos(window, &mousePos.x, &mousePos.y);
        example->mousePos = mousePos;
    }
}

void ExampleBase::MouseMoveHandler(GLFWwindow* window, double posx, double posy) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->mouseMoved(glm::vec2(posx, posy));
}

void ExampleBase::MouseScrollHandler(GLFWwindow* window, double xoffset, double yoffset) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->mouseScrolled((float)yoffset);
}

void ExampleBase::CloseHandler(GLFWwindow* window) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->prepared = false;
    glfwSetWindowShouldClose(window, 1);
}

void ExampleBase::FramebufferSizeHandler(GLFWwindow* window, int width, int height) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->windowResize(glm::uvec2(width, height));
}

void ExampleBase::setupWindow() {
    bool fullscreen = false;

#ifdef _WIN32
    // Check command line arguments
    for (int32_t i = 0; i < __argc; i++) {
        if (__argv[i] == std::string("-fullscreen")) {
            fullscreen = true;
        }
    }
#endif

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto monitor = glfwGetPrimaryMonitor();
    auto mode = glfwGetVideoMode(monitor);
    size.width = mode->width;
    size.height = mode->height;

    if (fullscreen) {
        window = glfwCreateWindow(size.width, size.height, "My Title", monitor, NULL);
    }
    else {
        size.width /= 2;
        size.height /= 2;
        window = glfwCreateWindow(size.width, size.height, "Window Title", NULL, NULL);
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, KeyboardHandler);
    glfwSetMouseButtonCallback(window, MouseHandler);
    glfwSetCursorPosCallback(window, MouseMoveHandler);
    glfwSetWindowCloseCallback(window, CloseHandler);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeHandler);
    glfwSetScrollCallback(window, MouseScrollHandler);
    if (!window) {
        throw std::runtime_error("Could not create window");
    }
    swapChain.setSurface(glfw::createWindowSurface(context.instance, window));
    camera.setAspectRatio(size);
}

#endif

