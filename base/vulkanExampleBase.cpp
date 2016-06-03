/*
* Vulkan Example base class
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"

using namespace vkx;

ExampleBase::ExampleBase(bool enableValidation) {
    // Check for validation command line flag
#if defined(_WIN32)
    for (int32_t i = 0; i < __argc; i++) {
        if (__argv[i] == std::string("-validation")) {
            enableValidation = true;
        }
    }
#elif defined(__ANDROID__)
    // Vulkan library is loaded dynamically on Android
    bool libLoaded = loadVulkanLibrary();
    assert(libLoaded);
#elif defined(__linux__)
    initxcbConnection();
#endif

#if !defined(__ANDROID__)
    // Android Vulkan initialization is handled in APP_CMD_INIT_WINDOW event
    initVulkan(enableValidation);
#endif
}

ExampleBase::~ExampleBase() {
    // Clean up Vulkan resources
    swapChain.cleanup();
    if (descriptorPool) {
        device.destroyDescriptorPool(descriptorPool);
    }
    destroyCommandBuffers();
    device.destroyRenderPass(renderPass);
    for (uint32_t i = 0; i < frameBuffers.size(); i++) {
        device.destroyFramebuffer(frameBuffers[i]);
    }

    for (auto& shaderModule : shaderModules) {
        device.destroyShaderModule(shaderModule);
    }
    device.destroyImageView(depthStencil.view);
    device.destroyImage(depthStencil.image);
    device.freeMemory(depthStencil.mem);

    if (textureLoader) {
        delete textureLoader;
    }

    if (enableTextOverlay) {
        delete textOverlay;
    }

    device.destroySemaphore(semaphores.presentComplete);
    device.destroySemaphore(semaphores.renderComplete);
    device.destroySemaphore(semaphores.textOverlayComplete);

    destroyContext();

#if defined(__ANDROID__)
    // todo : android cleanup (if required)
#else
    glfwDestroyWindow(window);
    glfwTerminate();
#endif
}

void ExampleBase::run() {
#if defined(_WIN32)
    setupWindow();
#elif defined(__ANDROID__)
    // Attach vulkan example to global android application state
    state->userData = vulkanExample;
    state->onAppCmd = VulkanExample::handleAppCommand;
    state->onInputEvent = VulkanExample::handleAppInput;
    androidApp = state;
#elif defined(__linux__)
    setupWindow();
#endif
#if !defined(__ANDROID__)
    initSwapchain();
    prepare();
#endif
    renderLoop();
}

void ExampleBase::initVulkan(bool enableValidation) {
    createContext(enableValidation);
    // Find a suitable depth format
    depthFormat = getSupportedDepthFormat(physicalDevice);

    swapChain.connect(*this);

    // Create synchronization objects
    vk::SemaphoreCreateInfo semaphoreCreateInfo;
    // Create a semaphore used to synchronize image presentation
    // Ensures that the image is displayed before we start submitting new commands to the queu
    semaphores.presentComplete = device.createSemaphore(semaphoreCreateInfo);
    // Create a semaphore used to synchronize command submission
    // Ensures that the image is not presented until all commands have been sumbitted and executed
    semaphores.renderComplete = device.createSemaphore(semaphoreCreateInfo);
    // Create a semaphore used to synchronize command submission
    // Ensures that the image is not presented until all commands for the text overlay have been sumbitted and executed
    // Will be inserted after the render complete semaphore if the text overlay is enabled
    semaphores.textOverlayComplete = device.createSemaphore(semaphoreCreateInfo);

    // Set up submit info structure
    // Semaphores will stay the same during application lifetime
    // Command buffer submission info is set by each example
    submitInfo = vk::SubmitInfo();
    submitInfo.pWaitDstStageMask = &submitPipelineStages;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &semaphores.presentComplete;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &semaphores.renderComplete;
}

void ExampleBase::renderLoop() {
    destWidth = width;
    destHeight = height;
#if defined(__ANDROID__)
    while (1) {
        int ident;
        int events;
        struct android_poll_source* source;
        bool destroy = false;

        focused = true;

        while ((ident = ALooper_pollAll(focused ? 0 : -1, NULL, &events, (void**)&source)) >= 0) {
            if (source != NULL) {
                source->process(androidApp, source);
            }
            if (androidApp->destroyRequested != 0) {
                LOGD("Android app destroy requested");
                destroy = true;
                break;
            }
        }

        // App destruction requested
        // Exit loop, example will be destroyed in application main
        if (destroy) {
            break;
        }

        // Render frame
        if (prepared) {
            auto tStart = std::chrono::high_resolution_clock::now();
            render();
            frameCounter++;
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            frameTimer = tDiff / 1000.0f;
            // Convert to clamped timer value
            if (!paused) {
                timer += timerSpeed * frameTimer;
                if (timer > 1.0) {
                    timer -= 1.0f;
                }
            }
            fpsTimer += (float)tDiff;
            if (fpsTimer > 1000.0f) {
                lastFPS = frameCounter;
                updateTextOverlay();
                fpsTimer = 0.0f;
                frameCounter = 0;
            }
            // Check gamepad state
            const float deadZone = 0.0015f;
            // todo : check if gamepad is present
            // todo : time based and relative axis positions
            bool updateView = false;
            // Rotate
            if (std::abs(gamePadState.axes.x) > deadZone) {
                rotation.y += gamePadState.axes.x * 0.5f * rotationSpeed;
                updateView = true;
            }
            if (std::abs(gamePadState.axes.y) > deadZone) {
                rotation.x -= gamePadState.axes.y * 0.5f * rotationSpeed;
                updateView = true;
            }
            // Zoom
            if (std::abs(gamePadState.axes.rz) > deadZone) {
                zoom -= gamePadState.axes.rz * 0.01f * zoomSpeed;
                updateView = true;
            }
            if (updateView) {
                viewChanged();
            }


        }
    }
#else
    while (!glfwWindowShouldClose(window)) {
        auto tStart = std::chrono::high_resolution_clock::now();
        glfwPollEvents();
        render();
        frameCounter++;
        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        frameTimer = (float)tDiff / 1000.0f;
        // Convert to clamped timer value
        if (!paused) {
            timer += timerSpeed * frameTimer;
            if (timer > 1.0) {
                timer -= 1.0f;
            }
        }
        fpsTimer += (float)tDiff;
        if (fpsTimer > 1000.0f) {
            std::string windowTitle = getWindowTitle();
            if (!enableTextOverlay) {
                glfwSetWindowTitle(window, windowTitle.c_str());
            }
            lastFPS = frameCounter;
            updateTextOverlay();
            fpsTimer = 0.0f;
            frameCounter = 0;
        }
    }
#endif
}

std::string ExampleBase::getWindowTitle() {
    std::string device(deviceProperties.deviceName);
    std::string windowTitle;
    windowTitle = title + " - " + device + " - " + std::to_string(frameCounter) + " fps";
    return windowTitle;
}

const std::string ExampleBase::getAssetPath() {
#if defined(__ANDROID__)
    return "";
#else
    return "./../data/";
#endif
}

bool ExampleBase::checkCommandBuffers() {
    for (auto& cmdBuffer : drawCmdBuffers) {
        if (!cmdBuffer) {
            return false;
        }
    }
    return true;
}

void ExampleBase::createCommandBuffers() {
    // Create one command buffer per frame buffer
    // in the swap chain
    // Command buffers store a reference to the
    // frame buffer inside their render pass info
    // so for static usage withouth having to rebuild
    // them each frame, we use one per frame buffer
    vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
    cmdBufAllocateInfo.commandPool = cmdPool;

    // 2 extra command buffers for submitting present barriers
    cmdBufAllocateInfo.commandBufferCount = swapChain.imageCount + 2;
    drawCmdBuffers = device.allocateCommandBuffers(cmdBufAllocateInfo);
    // Pre present
    prePresentCmdBuffer = drawCmdBuffers[cmdBufAllocateInfo.commandBufferCount - 1];
    // Post present
    postPresentCmdBuffer = drawCmdBuffers[cmdBufAllocateInfo.commandBufferCount - 2];

    // Now fix the primary draw buffer container size
    drawCmdBuffers.resize(cmdBufAllocateInfo.commandBufferCount - 2);
}

void ExampleBase::destroyCommandBuffers() {
    device.freeCommandBuffers(cmdPool, drawCmdBuffers);
    device.freeCommandBuffers(cmdPool, prePresentCmdBuffer);
    device.freeCommandBuffers(cmdPool, postPresentCmdBuffer);
}

void ExampleBase::prepare() {
    if (enableValidation) {
        debug::setupDebugging(instance, vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning);
    }
    if (enableDebugMarkers) {
        debug::marker::setup(device);
    }
    createCommandPool();
    withPrimaryCommandBuffer([&](vk::CommandBuffer setupCmdBuffer) {
        setupSwapChain(setupCmdBuffer);
        setupDepthStencil(setupCmdBuffer);
    });
    createCommandBuffers();
    setupRenderPass();
    setupFrameBuffer();
    // Create a simple texture loader class
    textureLoader = new TextureLoader(*this);
#if defined(__ANDROID__)
    textureLoader->assetManager = androidApp->activity->assetManager;
#endif
    if (enableTextOverlay) {
        // Load the text rendering shaders
        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
        shaderStages.push_back(loadShader(getAssetPath() + "shaders/base/textoverlay.vert.spv", vk::ShaderStageFlagBits::eVertex));
        shaderStages.push_back(loadShader(getAssetPath() + "shaders/base/textoverlay.frag.spv", vk::ShaderStageFlagBits::eFragment));
        textOverlay = new TextOverlay(*this,
            frameBuffers,
            colorformat,
            depthFormat,
            &width,
            &height,
            shaderStages
            );
        updateTextOverlay();
    }
}

vk::PipelineShaderStageCreateInfo ExampleBase::loadGlslShader(const std::string& fileName, vk::ShaderStageFlagBits stage) {
    auto source = readTextFile(fileName.c_str());
    vk::PipelineShaderStageCreateInfo shaderStage;
    shaderStage.stage = stage;
    shaderStage.module = shader::glslToShaderModule(device, stage, source);
    shaderStage.pName = "main";
    shaderModules.push_back(shaderStage.module);
    return shaderStage;
}

vk::PipelineShaderStageCreateInfo ExampleBase::loadShader(const std::string& fileName, vk::ShaderStageFlagBits stage) {
    vk::PipelineShaderStageCreateInfo shaderStage;
    shaderStage.stage = stage;
#if defined(__ANDROID__)
    shaderStage.module = loadShader(androidApp->activity->assetManager, fileName.c_str(), device, stage);
#else
    shaderStage.module = vkx::loadShader(fileName.c_str(), device, stage);
#endif
    shaderStage.pName = "main"; // todo : make param
    assert(shaderStage.module);
    shaderModules.push_back(shaderStage.module);
    return shaderStage;
}

MeshBuffer ExampleBase::loadMesh(const std::string& filename, const MeshLayout& vertexLayout, float scale) {
    MeshLoader loader;
#if defined(__ANDROID__)
    loader.assetManager = androidApp->activity->assetManager;
#endif
    loader.load(filename);
    assert(loader.m_Entries.size() > 0);
    return loader.createBuffers(*this, vertexLayout, scale);
}

void ExampleBase::submitPrePresentBarrier(const vk::Image& image) {
    vk::CommandBufferBeginInfo cmdBufInfo;

    prePresentCmdBuffer.begin(cmdBufInfo);

    vk::ImageMemoryBarrier prePresentBarrier;
    prePresentBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    prePresentBarrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    prePresentBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
    prePresentBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    prePresentBarrier.image = image;

    prePresentCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), nullptr, nullptr, prePresentBarrier);

    prePresentCmdBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &prePresentCmdBuffer;

    queue.submit(submitInfo, VK_NULL_HANDLE);
}

void ExampleBase::submitPostPresentBarrier(const vk::Image& image) {
    vk::CommandBufferBeginInfo cmdBufInfo;

    postPresentCmdBuffer.begin(cmdBufInfo);

    vk::ImageMemoryBarrier postPresentBarrier;
    postPresentBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    postPresentBarrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    postPresentBarrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    postPresentBarrier.image = image;

    postPresentCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTopOfPipe, vk::DependencyFlags(), nullptr, nullptr, postPresentBarrier);

    postPresentCmdBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &postPresentCmdBuffer;

    queue.submit(submitInfo, VK_NULL_HANDLE);
}

vk::SubmitInfo ExampleBase::prepareSubmitInfo(
    const std::vector<vk::CommandBuffer>& commandBuffers,
    vk::PipelineStageFlags *pipelineStages) {
    vk::SubmitInfo submitInfo;
    submitInfo.pWaitDstStageMask = pipelineStages;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &semaphores.presentComplete;
    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    submitInfo.pCommandBuffers = commandBuffers.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &semaphores.renderComplete;
    return submitInfo;
}

void ExampleBase::updateTextOverlay() {
    if (!enableTextOverlay)
        return;

    textOverlay->beginTextUpdate();

    textOverlay->addText(title, 5.0f, 5.0f, TextOverlay::alignLeft);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << (frameTimer * 1000.0f) << "ms (" << lastFPS << " fps)";
    textOverlay->addText(ss.str(), 5.0f, 25.0f, TextOverlay::alignLeft);

    textOverlay->addText(deviceProperties.deviceName, 5.0f, 45.0f, TextOverlay::alignLeft);

    getOverlayText(textOverlay);

    textOverlay->endTextUpdate();
}

void ExampleBase::getOverlayText(vkx::TextOverlay *textOverlay) {
    // Can be overriden in derived class
}

void ExampleBase::prepareFrame() {
    // Acquire the next image from the swap chaing
    currentBuffer = swapChain.acquireNextImage(semaphores.presentComplete);
    // Submit barrier that transforms color attachment image layout back from khr
    submitPostPresentBarrier(swapChain.buffers[currentBuffer].image);

}

void ExampleBase::submitFrame() {
    bool submitTextOverlay = enableTextOverlay && textOverlay->visible;

    if (submitTextOverlay) {
        // Wait for color attachment output to finish before rendering the text overlay
        vk::PipelineStageFlags stageFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submitInfo.pWaitDstStageMask = &stageFlags;

        // Set semaphores
        // Wait for render complete semaphore
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &semaphores.renderComplete;
        // Signal ready with text overlay complete semaphpre
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &semaphores.textOverlayComplete;

        // Submit current text overlay command buffer
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &textOverlay->cmdBuffers[currentBuffer];
        queue.submit(submitInfo, VK_NULL_HANDLE);

        // Reset stage mask
        submitInfo.pWaitDstStageMask = &submitPipelineStages;
        // Reset wait and signal semaphores for rendering next frame
        // Wait for swap chain presentation to finish
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &semaphores.presentComplete;
        // Signal ready with offscreen semaphore
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &semaphores.renderComplete;
    }

    // Submit barrier that transforms color attachment to khr presen
    submitPrePresentBarrier(swapChain.buffers[currentBuffer].image);

    swapChain.queuePresent(queue, currentBuffer, submitTextOverlay ? semaphores.textOverlayComplete : semaphores.renderComplete);

    queue.waitIdle();
}




#if defined(__ANDROID__)
int32_t ExampleBase::handleAppInput(struct android_app* app, AInputEvent* event) {
    ExampleBase* vulkanExample = (ExampleBase*)app->userData;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        if (AInputEvent_getSource(event) == AINPUT_SOURCE_JOYSTICK) {
            vulkanExample->gamePadState.axes.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            vulkanExample->gamePadState.axes.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            vulkanExample->gamePadState.axes.z = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            vulkanExample->gamePadState.axes.rz = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
        } else {
            // todo : touch input
        }
        return 1;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t keyCode = AKeyEvent_getKeyCode((const AInputEvent*)event);
        int32_t action = AKeyEvent_getAction((const AInputEvent*)event);
        int32_t button = 0;

        if (action == AKEY_EVENT_ACTION_UP)
            return 0;

        switch (keyCode) {
        case AKEYCODE_BUTTON_A:
            vulkanExample->keyPressed(GAMEPAD_BUTTON_A);
            break;
        case AKEYCODE_BUTTON_B:
            vulkanExample->keyPressed(GAMEPAD_BUTTON_B);
            break;
        case AKEYCODE_BUTTON_X:
            vulkanExample->keyPressed(GAMEPAD_BUTTON_X);
            break;
        case AKEYCODE_BUTTON_Y:
            vulkanExample->keyPressed(GAMEPAD_BUTTON_Y);
            break;
        case AKEYCODE_BUTTON_L1:
            vulkanExample->keyPressed(GAMEPAD_BUTTON_L1);
            break;
        case AKEYCODE_BUTTON_R1:
            vulkanExample->keyPressed(GAMEPAD_BUTTON_R1);
            break;
        case AKEYCODE_BUTTON_START:
            vulkanExample->keyPressed(GAMEPAD_BUTTON_START);
            break;
        };

        LOGD("Button %d pressed", keyCode);
    }

    return 0;
}

void ExampleBase::handleAppCommand(android_app * app, int32_t cmd) {
    assert(app->userData != NULL);
    ExampleBase* vulkanExample = (ExampleBase*)app->userData;
    switch (cmd) {
    case APP_CMD_SAVE_STATE:
        LOGD("APP_CMD_SAVE_STATE");
        /*
        vulkanExample->app->savedState = malloc(sizeof(struct saved_state));
        *((struct saved_state*)vulkanExample->app->savedState) = vulkanExample->state;
        vulkanExample->app->savedStateSize = sizeof(struct saved_state);
        */
        break;
    case APP_CMD_INIT_WINDOW:
        LOGD("APP_CMD_INIT_WINDOW");
        if (vulkanExample->androidApp->window != NULL) {
            vulkanExample->initVulkan(false);
            vulkanExample->initSwapchain();
            vulkanExample->prepare();
            assert(vulkanExample->prepared);
        } else {
            LOGE("No window assigned!");
        }
        break;
    case APP_CMD_LOST_FOCUS:
        LOGD("APP_CMD_LOST_FOCUS");
        vulkanExample->focused = false;
        break;
    case APP_CMD_GAINED_FOCUS:
        LOGD("APP_CMD_GAINED_FOCUS");
        vulkanExample->focused = true;
        break;
    }
}
#else

void ExampleBase::KeyboardHandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    if (action == GLFW_PRESS) {
        switch (key) {
        case GLFW_KEY_P:
            example->paused = !example->paused;
            break;

        case GLFW_KEY_F1:
            if (example->enableTextOverlay) {
                example->textOverlay->visible = !example->textOverlay->visible;
            }
            break;

        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, 1);
            break;

        default:
            break;
        }
        example->keyPressed(key);
    }
}

void ExampleBase::mouseMoved(double posx, double posy) {
    if (GLFW_PRESS == glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)) {
        zoom += (mousePos.y - (float)posy) * .005f * zoomSpeed;
        mousePos = glm::vec2((float)posx, (float)posy);
        viewChanged();
    }
    if (GLFW_PRESS == glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)) {
        rotation.x += (mousePos.y - (float)posy) * 1.25f * rotationSpeed;
        rotation.y -= (mousePos.x - (float)posx) * 1.25f * rotationSpeed;
        mousePos = glm::vec2((float)posx, (float)posy);
        viewChanged();
    }
    if (GLFW_PRESS == glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE)) {
        cameraPos.x -= (mousePos.x - (float)posx) * 0.01f;
        cameraPos.y -= (mousePos.y - (float)posy) * 0.01f;
        viewChanged();
        mousePos.x = (float)posx;
        mousePos.y = (float)posy;
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
    example->mouseMoved(posx, posy);
}

void ExampleBase::MouseScrollHandler(GLFWwindow* window, double xoffset, double yoffset) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->zoom += (float)yoffset * 0.1f * example->zoomSpeed;
    example->viewChanged();
}
void ExampleBase::SizeHandler(GLFWwindow* window, int width, int height) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
}

void ExampleBase::CloseHandler(GLFWwindow* window) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->prepared = false;
    glfwSetWindowShouldClose(window, 1);
}

void ExampleBase::FramebufferSizeHandler(GLFWwindow* window, int width, int height) {
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->windowResize();
}

void ExampleBase::JoystickHandler(int, int) {

}

void ExampleBase::setupWindow() {
    bool fullscreen = false;
    // Check command line arguments
    for (int32_t i = 0; i < __argc; i++) {
        if (__argv[i] == std::string("-fullscreen")) {
            fullscreen = true;
        }
    }

    if (fullscreen) {
        // TODO 
    }

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto monitor = glfwGetPrimaryMonitor();
    auto mode = glfwGetVideoMode(monitor);
    auto screenWidth = mode->width;
    auto screenHeight = mode->height;

    if (fullscreen) {
        window = glfwCreateWindow(screenWidth, screenHeight, "My Title", monitor, NULL);
    } else {
        window = glfwCreateWindow(screenWidth / 2, screenHeight / 2, "Window Title", NULL, NULL);
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, KeyboardHandler);
    glfwSetMouseButtonCallback(window, MouseHandler);
    glfwSetCursorPosCallback(window, MouseMoveHandler);
    glfwSetWindowSizeCallback(window, SizeHandler);
    glfwSetWindowCloseCallback(window, CloseHandler);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeHandler);
    glfwSetScrollCallback(window, MouseScrollHandler);

    std::string windowTitle = getWindowTitle();

    if (!window) {
        throw std::runtime_error("Could not create window");
    }
}

#endif

#if 0
int glfwJoystickPresent(int joy);
const float* glfwGetJoystickAxes(int joy, int* count);
const unsigned char* glfwGetJoystickButtons(int joy, int* count);

void ExampleBase::handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_KEYDOWN:
        switch (wParam) {
            break;
        }
#endif

void ExampleBase::viewChanged() {
    // Can be overrdiden in derived class
}

void ExampleBase::keyPressed(uint32_t keyCode) {
    // Can be overriden in derived class
}

void ExampleBase::buildCommandBuffers() {
    // Can be overriden in derived class
}

void ExampleBase::createCommandPool() {
    cmdPool = getCommandPool();
}

void ExampleBase::setupDepthStencil(const vk::CommandBuffer& setupCmdBuffer) {
    depthStencil.destroy(device);

    vk::ImageCreateInfo image;
    image.imageType = vk::ImageType::e2D;
    image.format = depthFormat;
    image.extent = vk::Extent3D{ width, height, 1 };
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = vk::SampleCountFlagBits::e1;
    image.tiling = vk::ImageTiling::eOptimal;
    image.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc;

    vk::MemoryAllocateInfo mem_alloc;
    mem_alloc.allocationSize = 0;
    mem_alloc.memoryTypeIndex = 0;

    vk::ImageViewCreateInfo depthStencilView;
    depthStencilView.viewType = vk::ImageViewType::e2D;
    depthStencilView.format = depthFormat;
    depthStencilView.subresourceRange;
    depthStencilView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    depthStencilView.subresourceRange.baseMipLevel = 0;
    depthStencilView.subresourceRange.levelCount = 1;
    depthStencilView.subresourceRange.baseArrayLayer = 0;
    depthStencilView.subresourceRange.layerCount = 1;

    vk::MemoryRequirements memReqs;

    depthStencil.image = device.createImage(image);
    memReqs = device.getImageMemoryRequirements(depthStencil.image);
    mem_alloc.allocationSize = memReqs.size;
    mem_alloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthStencil.mem = device.allocateMemory(mem_alloc);

    device.bindImageMemory(depthStencil.image, depthStencil.mem, 0);
    setImageLayout(
        setupCmdBuffer,
        depthStencil.image,
        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthStencilAttachmentOptimal);

    depthStencilView.image = depthStencil.image;
    depthStencil.view = device.createImageView(depthStencilView);
}

void ExampleBase::setupFrameBuffer() {
    // Create frame buffers for every swap chain image
    frameBuffers.resize(swapChain.imageCount);
    vk::ImageView attachments[2];

    // Depth/Stencil attachment is the same for all frame buffers
    attachments[1] = depthStencil.view;

    vk::FramebufferCreateInfo frameBufferCreateInfo;
    frameBufferCreateInfo.renderPass = renderPass;
    frameBufferCreateInfo.attachmentCount = 2;
    frameBufferCreateInfo.pAttachments = attachments;
    frameBufferCreateInfo.width = width;
    frameBufferCreateInfo.height = height;
    frameBufferCreateInfo.layers = 1;

    for (uint32_t i = 0; i < frameBuffers.size(); i++) {
        attachments[0] = swapChain.buffers[i].view;
        frameBuffers[i] = device.createFramebuffer(frameBufferCreateInfo);
    }
}

void ExampleBase::setupRenderPass() {
    vk::AttachmentDescription attachments[2];

    // Color attachment
    attachments[0].format = colorformat;
    attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
    attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
    attachments[0].initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
    attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

    // Depth attachment
    attachments[1].format = depthFormat;
    attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
    attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
    attachments[1].initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference colorReference;
    colorReference.attachment = 0;
    colorReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference depthReference;
    depthReference.attachment = 1;
    depthReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    renderPass = device.createRenderPass(renderPassInfo);
}

void ExampleBase::windowResize() {
    if (!prepared) {
        return;
    }
    prepared = false;

    // Recreate swap chain
    width = destWidth;
    height = destHeight;
    withPrimaryCommandBuffer([&](const vk::CommandBuffer& setupCmdBuffer) {
        setupSwapChain(setupCmdBuffer);
        setupDepthStencil(setupCmdBuffer);
    });

    // Recreate the frame buffers


    for (uint32_t i = 0; i < frameBuffers.size(); i++) {
        device.destroyFramebuffer(frameBuffers[i]);
    }
    setupFrameBuffer();

    // Command buffers need to be recreated as they may store
    // references to the recreated frame buffer
    destroyCommandBuffers();
    createCommandBuffers();
    buildCommandBuffers();

    queue.waitIdle();
    device.waitIdle();

    if (enableTextOverlay) {
        textOverlay->reallocateCommandBuffers();
        updateTextOverlay();
    }

    // Notify derived class
    windowResized();
    viewChanged();

    prepared = true;
}

void ExampleBase::windowResized() {
    // Can be overriden in derived class
}

void ExampleBase::initSwapchain() {
#if defined(_WIN32)
    swapChain.initSurface(GetModuleHandle(NULL), glfwGetWin32Window(window));
#elif defined(__ANDROID__)    
    swapChain.initSurface(androidApp->window);
#elif defined(__linux__)
    swapChain.initSurface(connection, window);
#endif
}

void ExampleBase::setupSwapChain(const vk::CommandBuffer& setupCmdBuffer) {
    swapChain.create(setupCmdBuffer, &width, &height);
}

void ExampleBase::drawCommandBuffers(const std::vector<vk::CommandBuffer>& commandBuffers) {

    // Command buffer(s) to be sumitted to the queue
    submitInfo.commandBufferCount = commandBuffers.size();
    submitInfo.pCommandBuffers = commandBuffers.data();
    // Submit to queue
    queue.submit(submitInfo, VK_NULL_HANDLE);
}

void ExampleBase::draw() {
    // Get next image in the swap chain (back/front buffer)
    prepareFrame();

    drawCommandBuffers({ drawCmdBuffers[currentBuffer] });
    // Push the rendered frame to the surface
    submitFrame();
}
