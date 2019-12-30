/*
* Vulkan Example base class
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "common.hpp"

#include <khrpp/vks/helpers.hpp>
#include <khrpp/vks/shaders.hpp>
#include <khrpp/vks/pipelines.hpp>
#include <khrpp/vks/swapchain.hpp>
#include <khrpp/vks/renderpass.hpp>

#include "ui.hpp"
#include "utils.hpp"
#include "camera.hpp"
#include "compute.hpp"
#include "keycodes.hpp"
#include "storage.hpp"
#include "texture.hpp"

#if defined(__ANDROID__)
#include "AndroidNativeApp.hpp"
#endif

#define GAMEPAD_BUTTON_A 0x1000
#define GAMEPAD_BUTTON_B 0x1001
#define GAMEPAD_BUTTON_X 0x1002
#define GAMEPAD_BUTTON_Y 0x1003
#define GAMEPAD_BUTTON_L1 0x1004
#define GAMEPAD_BUTTON_R1 0x1005
#define GAMEPAD_BUTTON_START 0x1006

namespace vks {

using UIOverlay = vkx::ui::UIOverlay;

}

namespace vks { namespace tools {

void exitFatal(const char* message, VkResult error) {
    throw std::runtime_error(message);
}

}}  // namespace vks::tools

namespace vkx {

struct UpdateOperation {
    const vk::Buffer buffer;
    const vk::DeviceSize size;
    const vk::DeviceSize offset;
    const uint32_t* data;

    template <typename T>
    UpdateOperation(const vk::Buffer& buffer, const T& data, vk::DeviceSize offset = 0)
        : buffer(buffer)
        , size(sizeof(T))
        , offset(offset)
        , data((uint32_t*)&data) {
        assert(0 == (sizeof(T) % 4));
        assert(0 == (offset % 4));
    }
};

class ExampleBase {
protected:
#ifdef NDEBUG
    static constexpr bool DEFAULT_VALIDATION{ false };
#else
    static constexpr bool DEFAULT_VALIDATION{ true };
#endif
    ExampleBase(bool enableValidation = DEFAULT_VALIDATION);
    ~ExampleBase();

    using vAF = vk::AccessFlagBits;
    using vBU = vk::BufferUsageFlagBits;
    using vDT = vk::DescriptorType;
    using vF = vk::Format;
    using vIL = vk::ImageLayout;
    using vIT = vk::ImageType;
    using vIVT = vk::ImageViewType;
    using vIU = vk::ImageUsageFlagBits;
    using vIA = vk::ImageAspectFlagBits;
    using vMP = vk::MemoryPropertyFlagBits;
    using vPS = vk::PipelineStageFlagBits;
    using vSS = vk::ShaderStageFlagBits;

public:
    void run();
    // Called if the window is resized and some resources have to be recreatesd
    void windowResize(const glm::uvec2& newSize);

private:
    // Set to true when the debug marker extension is detected
    bool enableDebugMarkers{ false };
    // fps timer (one second interval)
    float fpsTimer = 0.0f;
    // Get window title with example name, device, et.
    std::string getWindowTitle();

protected:
    bool enableVsync{ false };
    // Command buffers used for rendering
    std::vector<vk::CommandBuffer> drawCmdBuffers;
    std::vector<vk::ClearValue> clearValues;
    vk::RenderPassBeginInfo renderPassBeginInfo;
    vk::Viewport viewport() { return vks::util::viewport(size); }
    vk::Rect2D scissor() { return vks::util::rect2D(size); }

    virtual void clearCommandBuffers() final;
    virtual void allocateCommandBuffers() final;
    virtual void setupRenderPassBeginInfo();
    virtual void buildCommandBuffers() final;

protected:
#ifdef USE_VMA
    VmaAllocator& allocator{ context.allocator };
#endif
    float zoom;
    vec3 cameraPos;
    vec3 rotation;
    //vk::SubmitInfo submitInfo;

    // Last frame time, measured using a high performance timer (if available)
    float frameTimer{ 0.0015f };
    // Frame counter to display fps
    uint32_t frameCounter{ 0 };
    uint32_t lastFPS{ 0 };

    // Color buffer format
    vk::Format colorformat{ vk::Format::eB8G8R8A8Unorm };

    // Depth buffer format...  selected during Vulkan initialization
    vk::Format depthFormat{ vk::Format::eUndefined };

    // Global render pass for frame buffer writes
    vk::RenderPass renderPass;

    // List of available frame buffers (same as number of swap chain images)
    std::vector<vk::Framebuffer> frameBuffers;
    // Active frame buffer index
    uint32_t currentBuffer = 0;
    // Descriptor set pool
    vk::DescriptorPool descriptorPool;

    void addRenderWaitSemaphore(const vk::Semaphore& semaphore, const vk::PipelineStageFlags& waitStages = vk::PipelineStageFlagBits::eTopOfPipe);

    struct Synchronization {
        std::vector<vk::Semaphore> renderWaitSemaphores;
        std::vector<vk::PipelineStageFlags> renderWaitStages;
        std::vector<vk::Semaphore> renderSignalSemaphores;
    } synchronization;
    void submitWithSynchronization(const vk::ArrayProxy<const vk::CommandBuffer>& commands, vk::Fence fence = nullptr);
    vks::Context context;
    const vk::PhysicalDevice& physicalDevice{ context.physicalDevice };
    const vk::Device& device{ context.device };
    const vk::Queue& queue{ context.queue };
    const vk::PhysicalDeviceFeatures& deviceFeatures{ context.deviceFeatures };
    vk::PhysicalDeviceFeatures& enabledFeatures{ context.enabledFeatures };
    vkx::ui::UIOverlay ui{ context };
    const vk::PipelineCache& pipelineCache{ context.pipelineCache };

    vk::SurfaceKHR surface;
    // Wraps the swap chain to present images (framebuffers) to the windowing system
    vks::SwapChain swapChain;

    // Synchronization semaphores
    struct {
        // Swap chain image presentation
        vk::Semaphore acquireComplete;
        // Command buffer submission and execution
        vk::Semaphore renderComplete;
        // UI buffer submission and execution
        vk::Semaphore overlayComplete;
#if 0
        vk::Semaphore transferComplete;
#endif
    } semaphores;

    // Returns the base asset path (for shaders, models, textures) depending on the os
    const std::string& getAssetPath() const { return ::vkx::getAssetPath(); }

protected:
    /** @brief Example settings that can be changed e.g. by command line arguments */
    struct Settings {
        /** @brief Activates validation layers (and message output) when set to true */
        bool validation = false;
        /** @brief Set to true if fullscreen mode has been requested via command line */
        bool fullscreen = false;
        /** @brief Set to true if v-sync will be forced for the swapchain */
        bool vsync = false;
        /** @brief Enable UI overlay */
        bool overlay = true;
    } settings;

    struct {
        bool left = false;
        bool right = false;
        bool middle = false;
    } mouseButtons;

    struct {
        bool active = false;
    } benchmark;

    // Command buffer pool
    vk::CommandPool cmdPool;

    bool prepared = false;
    uint32_t version = VK_MAKE_VERSION(1, 1, 0);
    vk::Extent2D size{ 1280, 720 };
    uint32_t& width{ size.width };
    uint32_t& height{ size.height };

    vk::ClearColorValue defaultClearColor = vks::util::clearColor(glm::vec4({ 0.025f, 0.025f, 0.025f, 1.0f }));
    vk::ClearDepthStencilValue defaultClearDepth{ 1.0f, 0 };

    // Defines a frame rate independent timer value clamped from -1.0...1.0
    // For use in animations, rotations, etc.
    float timer = 0.0f;
    // Multiplier for speeding up (or slowing down) the global timer
    float timerSpeed = 0.25f;

    bool paused = false;

    // Use to adjust mouse rotation speed
    float rotationSpeed = 1.0f;
    // Use to adjust mouse zoom speed
    float zoomSpeed = 1.0f;

    Camera camera;
    glm::vec2 mousePos;
    bool viewUpdated{ false };

    std::string title = "Vulkan Example";
    std::string name = "vulkanExample";
    vks::Image depthStencil;

    // Gamepad state (only one pad supported)
    struct {
        glm::vec2 axisLeft = glm::vec2(0.0f);
        glm::vec2 axisRight = glm::vec2(0.0f);
        float rz{ 0.0f };
    } gamePadState;

    void updateOverlay();

    virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay) {}
    virtual void OnUpdateUIOverlay() { this->OnUpdateUIOverlay(&ui); }
    virtual void OnSetupUIOverlay(vkx::ui::UIOverlay::CreateInfo& uiCreateInfo) {}

    // Setup the vulkan instance, enable required extensions and connect to the physical device (GPU)
    virtual void initVulkan();
    virtual void setupSwapchain();
    virtual void setupWindow();
    virtual void getEnabledFeatures();
    // A default draw implementation
    virtual void draw();
    // Basic render function
    virtual void render();
    virtual void update(float deltaTime);
    // Called when view change occurs
    // Can be overriden in derived class to e.g. update uniform buffers
    // Containing view dependant matrices
    virtual void viewChanged() {}

    // Called when the window has been resized
    // Can be overriden in derived class to recreate or rebuild resources attached to the frame buffer / swapchain
    virtual void windowResized() {}

    // Setup default depth and stencil views
    void setupDepthStencil();
    // Create framebuffers for all requested swap chain images
    // Can be overriden in derived class to setup a custom framebuffer (e.g. for MSAA)
    virtual void setupFrameBuffer();

    // Setup a default render pass
    // Can be overriden in derived class to setup a custom render pass (e.g. for MSAA)
    virtual void setupRenderPass();

    void setupUi();

    virtual void updateCommandBufferPreDraw(const vk::CommandBuffer& commandBuffer) {}

    virtual void updateDrawCommandBuffer(const vk::CommandBuffer& commandBuffer) {}

    virtual void updateCommandBufferPostDraw(const vk::CommandBuffer& commandBuffer) {}

    void drawCurrentCommandBuffer();

    // Prepare commonly used Vulkan functions
    virtual void prepare();

    virtual void loadAssets() {}

    bool platformLoopCondition();

    // Start the main render loop
    void renderLoop();

    // Prepare the frame for workload submission
    // - Acquires the next image from the swap chain
    // - Submits a post present barrier
    // - Sets the default wait and signal semaphores
    void prepareFrame();

    // Submit the frames' workload
    // - Submits the text overlay (if enabled)
    // -
    void submitFrame();

    virtual const glm::mat4& getProjection() const { return camera.matrices.perspective; }

    virtual const glm::mat4& getView() const { return camera.matrices.view; }

    // Called if a key is pressed
    // Can be overriden in derived class to do custom key handling
    virtual void keyPressed(uint32_t key);
    virtual void keyReleased(uint32_t key);

    virtual void mouseMoved(const glm::vec2& newPos);
    virtual void mouseScrolled(float delta);

private:
    // OS specific
#if defined(__ANDROID__)
    // true if application has focused, false if moved to background
    ANativeWindow* window{ nullptr };
    bool focused = false;
    static int32_t handle_input_event(android_app* app, AInputEvent* event);
    int32_t onInput(AInputEvent* event);
    static void handle_app_cmd(android_app* app, int32_t cmd);
    void onAppCmd(int32_t cmd);
#else
    GLFWwindow* window{ nullptr };
    // Keyboard movement handler
    virtual void mouseAction(int buttons, int action, int mods);
    static void KeyboardHandler(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseHandler(GLFWwindow* window, int button, int action, int mods);
    static void MouseMoveHandler(GLFWwindow* window, double posx, double posy);
    static void MouseScrollHandler(GLFWwindow* window, double xoffset, double yoffset);
    static void FramebufferSizeHandler(GLFWwindow* window, int width, int height);
    static void CloseHandler(GLFWwindow* window);
#endif
};
}  // namespace vkx

#include <imgui.h>

// Avoid doing work in the ctor as it can't make use of overridden virtual functions
// Instead, use the `prepare` and `run` methods
vkx::ExampleBase::ExampleBase(bool enableValidation) {
    context.setValidationEnabled(enableValidation);
#if defined(__ANDROID__)
    vks::storage::setAssetManager(vkx::android::androidApp->activity->assetManager);
    vkx::android::androidApp->userData = this;
    vkx::android::androidApp->onInputEvent = ExampleBase::handle_input_event;
    vkx::android::androidApp->onAppCmd = ExampleBase::handle_app_cmd;
#endif
    camera.setPerspective(60.0f, size, 0.1f, 256.0f);
}

vkx::ExampleBase::~ExampleBase() {
    context.queue.waitIdle();
    context.device.waitIdle();

    // Clean up Vulkan resources
    swapChain.destroy();
    // FIXME destroy surface
    if (descriptorPool) {
        device.destroyDescriptorPool(descriptorPool);
    }
    if (!drawCmdBuffers.empty()) {
        device.freeCommandBuffers(cmdPool, drawCmdBuffers);
        drawCmdBuffers.clear();
    }
    device.destroyRenderPass(renderPass);
    for (uint32_t i = 0; i < frameBuffers.size(); i++) {
        device.destroyFramebuffer(frameBuffers[i]);
    }

    depthStencil.destroy();

    device.destroySemaphore(semaphores.acquireComplete);
    device.destroySemaphore(semaphores.renderComplete);
    device.destroySemaphore(semaphores.overlayComplete);

    ui.destroy();

    context.destroy();

#if defined(__ANDROID__)
    // todo : android cleanup (if required)
#else
    glfwDestroyWindow(window);
    glfwTerminate();
#endif
}

void vkx::ExampleBase::run() {
    try {
// Android initialization is handled in APP_CMD_INIT_WINDOW event
#if !defined(__ANDROID__)
        glfwInit();
        setupWindow();
        initVulkan();
        setupSwapchain();
        prepare();
#endif

        renderLoop();

        // Once we exit the render loop, wait for everything to become idle before proceeding to the descructor.
        context.queue.waitIdle();
        context.device.waitIdle();
    } catch (const std::system_error& err) {
        std::cerr << err.what() << std::endl;
    }
}

void vkx::ExampleBase::getEnabledFeatures() {
}

void vkx::ExampleBase::initVulkan() {
    // TODO make this less stupid
    context.setDeviceFeaturesPicker([this](const vk::PhysicalDevice& device, vk::PhysicalDeviceFeatures2& features) {
        if (deviceFeatures.textureCompressionBC) {
            enabledFeatures.textureCompressionBC = VK_TRUE;
        } else if (context.deviceFeatures.textureCompressionASTC_LDR) {
            enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
        } else if (context.deviceFeatures.textureCompressionETC2) {
            enabledFeatures.textureCompressionETC2 = VK_TRUE;
        }
        if (deviceFeatures.samplerAnisotropy) {
            enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
        getEnabledFeatures();
    });

#if defined(__ANDROID__)
    context.requireExtensions({ VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME });
#else
    context.requireExtensions(glfw::Window::getRequiredInstanceExtensions());
#endif
    context.requireDeviceExtensions({ VK_KHR_SWAPCHAIN_EXTENSION_NAME });
    context.createInstance(version);

#if defined(__ANDROID__)
    surface = context.instance.createAndroidSurfaceKHR({ {}, window });
#else
    surface = glfw::Window::createWindowSurface(window, context.instance);
#endif

    context.createDevice(surface);

    // Find a suitable depth format
    depthFormat = context.getSupportedDepthFormat();

    // Create synchronization objects

    // A semaphore used to synchronize image presentation
    // Ensures that the image is displayed before we start submitting new commands to the queu
    semaphores.acquireComplete = device.createSemaphore({});
    // A semaphore used to synchronize command submission
    // Ensures that the image is not presented until all commands have been sumbitted and executed
    semaphores.renderComplete = device.createSemaphore({});

    semaphores.overlayComplete = device.createSemaphore({});

    synchronization.renderWaitSemaphores.push_back(semaphores.acquireComplete);
    synchronization.renderWaitStages.push_back(vk::PipelineStageFlagBits::eBottomOfPipe);
    synchronization.renderSignalSemaphores.push_back(semaphores.renderComplete);
}

void vkx::ExampleBase::setupSwapchain() {
    swapChain.setup(context.physicalDevice, context.device, context.queue, context.queueFamilyIndices.graphics);
    swapChain.setSurface(surface);
}

bool vkx::ExampleBase::platformLoopCondition() {
#if defined(__ANDROID__)
    bool destroy = false;
    focused = true;
    int ident, events;
    struct android_poll_source* source;
    while (!destroy && (ident = ALooper_pollAll(focused ? 0 : -1, NULL, &events, (void**)&source)) >= 0) {
        if (source != NULL) {
            source->process(vkx::android::androidApp, source);
        }
        destroy = vkx::android::androidApp->destroyRequested != 0;
    }

    // App destruction requested
    // Exit loop, example will be destroyed in application main
    return !destroy;
#else
    if (0 != glfwWindowShouldClose(window)) {
        return false;
    }

    glfwPollEvents();

    if (0 != glfwJoystickPresent(0)) {
        // FIXME implement joystick handling
        int axisCount{ 0 };
        const float* axes = glfwGetJoystickAxes(0, &axisCount);
        if (axisCount >= 2) {
            gamePadState.axisLeft.x = axes[0] * 0.01f;
            gamePadState.axisLeft.y = axes[1] * -0.01f;
        }
        if (axisCount >= 4) {
            gamePadState.axisRight.x = axes[0] * 0.01f;
            gamePadState.axisRight.y = axes[1] * -0.01f;
        }
        if (axisCount >= 6) {
            float lt = (axes[4] + 1.0f) / 2.0f;
            float rt = (axes[5] + 1.0f) / 2.0f;
            gamePadState.rz = (rt - lt);
        }
        uint32_t newButtons{ 0 };
        static uint32_t oldButtons{ 0 };
        {
            int buttonCount{ 0 };
            const uint8_t* buttons = glfwGetJoystickButtons(0, &buttonCount);
            for (uint8_t i = 0; i < buttonCount && i < 64; ++i) {
                if (0 != buttons[i]) {
                    newButtons |= (1 << i);
                }
            }
        }
        auto changedButtons = newButtons & ~oldButtons;
        //if (changedButtons & 0x01) {
        //    keyPressed(GAMEPAD_BUTTON_A);
        //}
        //if (changedButtons & 0x02) {
        //    keyPressed(GAMEPAD_BUTTON_B);
        //}
        //if (changedButtons & 0x04) {
        //    keyPressed(GAMEPAD_BUTTON_X);
        //}
        //if (changedButtons & 0x08) {
        //    keyPressed(GAMEPAD_BUTTON_Y);
        //}
        //if (changedButtons & 0x10) {
        //    keyPressed(GAMEPAD_BUTTON_L1);
        //}
        //if (changedButtons & 0x20) {
        //    keyPressed(GAMEPAD_BUTTON_R1);
        //}
        oldButtons = newButtons;
    } else {
        memset(&gamePadState, 0, sizeof(gamePadState));
    }
    return true;
#endif
}

void vkx::ExampleBase::renderLoop() {
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

std::string vkx::ExampleBase::getWindowTitle() {
    std::string device(context.deviceProperties.deviceName);
    std::string windowTitle;
    windowTitle = title + " - " + device + " - " + std::to_string(frameCounter) + " fps";
    return windowTitle;
}

void vkx::ExampleBase::setupUi() {
    settings.overlay = settings.overlay && (!benchmark.active);
    if (!settings.overlay) {
        return;
    }

    struct vkx::ui::UIOverlay::CreateInfo overlayCreateInfo;
    // Setup default overlay creation info
    overlayCreateInfo.copyQueue = queue;
    overlayCreateInfo.framebuffers = frameBuffers;
    overlayCreateInfo.colorformat = swapChain.colorFormat;
    overlayCreateInfo.depthformat = depthFormat;
    overlayCreateInfo.size = size;

    ImGui::SetCurrentContext(ImGui::CreateContext());

    // Virtual function call for example to customize overlay creation
    OnSetupUIOverlay(overlayCreateInfo);
    ui.create(overlayCreateInfo);

    for (auto& shader : overlayCreateInfo.shaders) {
        device.destroyShaderModule(shader.module);
        shader.module = vk::ShaderModule{};
    }
    updateOverlay();
}

void vkx::ExampleBase::prepare() {
    cmdPool = context.getCommandPool();

    swapChain.create(size, enableVsync);
    setupDepthStencil();
    setupRenderPass();
    setupRenderPassBeginInfo();
    setupFrameBuffer();
    setupUi();
    loadAssets();
}

void vkx::ExampleBase::setupRenderPassBeginInfo() {
    clearValues.clear();
    clearValues.push_back(vks::util::clearColor(glm::vec4(0.1, 0.1, 0.1, 1.0)));
    clearValues.push_back(vk::ClearDepthStencilValue{ 1.0f, 0 });

    renderPassBeginInfo = vk::RenderPassBeginInfo();
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.extent = size;
    renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size();
    renderPassBeginInfo.pClearValues = clearValues.data();
}

void vkx::ExampleBase::allocateCommandBuffers() {
    clearCommandBuffers();
    // Create one command buffer per image in the swap chain

    // Command buffers store a reference to the
    // frame buffer inside their render pass info
    // so for static usage without having to rebuild
    // them each frame, we use one per frame buffer
    drawCmdBuffers = device.allocateCommandBuffers({ cmdPool, vk::CommandBufferLevel::ePrimary, swapChain.imageCount });
}

void vkx::ExampleBase::clearCommandBuffers() {
    if (!drawCmdBuffers.empty()) {
        context.trashCommandBuffers(cmdPool, drawCmdBuffers);
        // FIXME find a better way to ensure that the draw and text buffers are no longer in use before
        // executing them within this command buffer.
        context.queue.waitIdle();
        context.device.waitIdle();
        context.recycle();
    }
}

void vkx::ExampleBase::buildCommandBuffers() {
    // Destroy and recreate command buffers if already present
    allocateCommandBuffers();

    vk::CommandBufferBeginInfo cmdBufInfo{ vk::CommandBufferUsageFlagBits::eSimultaneousUse };
    for (size_t i = 0; i < swapChain.imageCount; ++i) {
        const auto& cmdBuffer = drawCmdBuffers[i];
        cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
        cmdBuffer.begin(cmdBufInfo);
        updateCommandBufferPreDraw(cmdBuffer);
        // Let child classes execute operations outside the renderpass, like buffer barriers or query pool operations
        renderPassBeginInfo.framebuffer = frameBuffers[i];
        cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        updateDrawCommandBuffer(cmdBuffer);
        cmdBuffer.endRenderPass();
        updateCommandBufferPostDraw(cmdBuffer);
        cmdBuffer.end();
    }
}

void vkx::ExampleBase::prepareFrame() {
    // Acquire the next image from the swap chaing
    auto resultValue = swapChain.acquireNextImage(semaphores.acquireComplete);
    if (resultValue.result == vk::Result::eSuboptimalKHR) {
#if !defined(__ANDROID__)
        ivec2 newSize;
        glfwGetWindowSize(window, &newSize.x, &newSize.y);
        windowResize(newSize);
        resultValue = swapChain.acquireNextImage(semaphores.acquireComplete);
#endif
    }
    currentBuffer = resultValue.value;
}

void vkx::ExampleBase::submitFrame() {
    bool submitOverlay = settings.overlay && ui.visible && (ui.cmdBuffers.size() > currentBuffer);
    if (submitOverlay) {
        vk::SubmitInfo submitInfo;
        // Wait for color attachment output to finish before rendering the text overlay
        vk::PipelineStageFlags stageFlags = vk::PipelineStageFlagBits::eBottomOfPipe;
        submitInfo.pWaitDstStageMask = &stageFlags;
        // Wait for render complete semaphore
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &semaphores.renderComplete;
        // Signal ready with UI overlay complete semaphpre
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &semaphores.overlayComplete;

        // Submit current UI overlay command buffer
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &ui.cmdBuffers[currentBuffer];
        queue.submit({ submitInfo }, {});
    }
    swapChain.queuePresent(submitOverlay ? semaphores.overlayComplete : semaphores.renderComplete);
}

void vkx::ExampleBase::setupDepthStencil() {
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

void vkx::ExampleBase::setupFrameBuffer() {
    // Recreate the frame buffers
    if (!frameBuffers.empty()) {
        for (const auto& framebuffer : frameBuffers) {
            device.destroy(framebuffer);
        }
        frameBuffers.clear();
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
    frameBuffers = swapChain.createFramebuffers(framebufferCreateInfo);
}

void vkx::ExampleBase::setupRenderPass() {
    if (renderPass) {
        device.destroyRenderPass(renderPass);
    }

    renderPass = vks::renderpass::Builder{}.simple(colorformat, depthFormat).create(device);
}

void vkx::ExampleBase::addRenderWaitSemaphore(const vk::Semaphore& semaphore, const vk::PipelineStageFlags& waitStages) {
    synchronization.renderWaitSemaphores.push_back(semaphore);
    synchronization.renderWaitStages.push_back(waitStages);
}

void vkx::ExampleBase::submitWithSynchronization(const vk::ArrayProxy<const vk::CommandBuffer>& commands, vk::Fence fence) {
    vk::SubmitInfo submitInfo;
    submitInfo.waitSemaphoreCount = (uint32_t)synchronization.renderWaitSemaphores.size();
    submitInfo.pWaitSemaphores = synchronization.renderWaitSemaphores.data();
    submitInfo.pWaitDstStageMask = synchronization.renderWaitStages.data();

    submitInfo.signalSemaphoreCount = (uint32_t)synchronization.renderSignalSemaphores.size();
    submitInfo.pSignalSemaphores = synchronization.renderSignalSemaphores.data();
    submitInfo.pCommandBuffers = commands.data();
    submitInfo.commandBufferCount = commands.size();
    queue.submit(submitInfo, fence);
}

void vkx::ExampleBase::drawCurrentCommandBuffer() {
    vk::Fence fence = swapChain.getSubmitFence();
    {
        uint32_t fenceIndex = currentBuffer;
        context.dumpster.push_back([fenceIndex, this] { swapChain.clearSubmitFence(fenceIndex); });
    }

    // Command buffer(s) to be sumitted to the queue
    context.emptyDumpster(fence);
    submitWithSynchronization(drawCmdBuffers[currentBuffer], fence);
    context.recycle();
}

void vkx::ExampleBase::draw() {
    // Get next image in the swap chain (back/front buffer)
    prepareFrame();
    // Execute the compiled command buffer for the current swap chain image
    drawCurrentCommandBuffer();
    // Push the rendered frame to the surface
    submitFrame();
}

void vkx::ExampleBase::render() {
    if (!prepared) {
        return;
    }
    draw();
}

void vkx::ExampleBase::update(float deltaTime) {
    frameTimer = deltaTime;
    ++frameCounter;

    camera.update(deltaTime);
    if (camera.moving()) {
        viewUpdated = true;
    }

    // Convert to clamped timer value
    if (!paused) {
        timer += timerSpeed * frameTimer;
        if (timer > 1.0) {
            timer -= 1.0f;
        }
    }
    fpsTimer += frameTimer;
    if (fpsTimer > 1.0f) {
#if !defined(__ANDROID__)
        std::string windowTitle = getWindowTitle();
        glfwSetWindowTitle(window, windowTitle.c_str());
#endif
        lastFPS = frameCounter;
        fpsTimer = 0.0f;
        frameCounter = 0;
    }

    updateOverlay();

    // Check gamepad state
    const float deadZone = 0.0015f;
    // todo : check if gamepad is present
    // todo : time based and relative axis positions
    if (camera.type != Camera::CameraType::firstperson) {
        // Rotate
        if (std::abs(gamePadState.axisLeft.x) > deadZone) {
            camera.rotate(glm::vec3(0.0f, gamePadState.axisLeft.x * 0.5f, 0.0f));
            viewUpdated = true;
        }
        if (std::abs(gamePadState.axisLeft.y) > deadZone) {
            camera.rotate(glm::vec3(gamePadState.axisLeft.y * 0.5f, 0.0f, 0.0f));
            viewUpdated = true;
        }
        // Zoom
        if (std::abs(gamePadState.axisRight.y) > deadZone) {
            camera.dolly(gamePadState.axisRight.y * 0.01f * zoomSpeed);
            viewUpdated = true;
        }
    } else {
        viewUpdated |= camera.updatePad(gamePadState.axisLeft, gamePadState.axisRight, frameTimer);
    }

    if (viewUpdated) {
        viewUpdated = false;
        rotation = camera.rotation;
        viewChanged();
    }
}

void vkx::ExampleBase::windowResize(const glm::uvec2& newSize) {
    if (!prepared) {
        return;
    }
    prepared = false;

    queue.waitIdle();
    device.waitIdle();

    // Recreate swap chain
    size.width = newSize.x;
    size.height = newSize.y;
    swapChain.create(size, enableVsync);

    setupDepthStencil();
    setupFrameBuffer();
    setupRenderPassBeginInfo();

    if (settings.overlay) {
        ui.resize(size, frameBuffers);
    }

    // Notify derived class
    windowResized();

    // Command buffers need to be recreated as they may store
    // references to the recreated frame buffer
    buildCommandBuffers();

    viewChanged();

    prepared = true;
}

void vkx::ExampleBase::updateOverlay() {
    if (!settings.overlay) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = ImVec2((float)size.width, (float)size.height);
    io.DeltaTime = frameTimer;

    io.MousePos = ImVec2(mousePos.x, mousePos.y);
    io.MouseDown[0] = mouseButtons.left;
    io.MouseDown[1] = mouseButtons.right;

    ImGui::NewFrame();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    //ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiSetCond_FirstUseEver);
    ImGui::Begin("Vulkan Example", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted(title.c_str());
    ImGui::TextUnformatted(context.deviceProperties.deviceName);
    ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / lastFPS), lastFPS);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 5.0f * ui.scale));
#endif
    ImGui::PushItemWidth(110.0f * ui.scale);
    OnUpdateUIOverlay();
    ImGui::PopItemWidth();
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    ImGui::PopStyleVar();
#endif

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::Render();

    ui.update();

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    if (mouseButtons.left) {
        mouseButtons.left = false;
    }
#endif
}

void vkx::ExampleBase::mouseMoved(const glm::vec2& newPos) {
    auto imgui = ImGui::GetIO();
    if (imgui.WantCaptureMouse) {
        mousePos = newPos;
        return;
    }

    glm::vec2 deltaPos = mousePos - newPos;
    if (deltaPos == vec2()) {
        return;
    }

    const auto& dx = deltaPos.x;
    const auto& dy = deltaPos.y;
    bool handled = false;
    if (settings.overlay) {
        ImGuiIO& io = ImGui::GetIO();
        handled = io.WantCaptureMouse;
    }

    if (mouseButtons.left) {
        camera.rotate(glm::vec3(dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
        viewUpdated = true;
    }
    if (mouseButtons.right) {
        camera.dolly(dy * .005f * zoomSpeed);
        viewUpdated = true;
    }
    if (mouseButtons.middle) {
        camera.translate(glm::vec3(-dx * 0.01f, -dy * 0.01f, 0.0f));
        viewUpdated = true;
    }
    mousePos = newPos;
}

void vkx::ExampleBase::mouseScrolled(float delta) {
    camera.translate(glm::vec3(0.0f, 0.0f, (float)delta * 0.005f * zoomSpeed));
    viewUpdated = true;
}

void vkx::ExampleBase::keyPressed(uint32_t key) {
    if (camera.firstperson) {
        switch (key) {
            case KEY_W:
                camera.keys.up = true;
                break;
            case KEY_S:
                camera.keys.down = true;
                break;
            case KEY_A:
                camera.keys.left = true;
                break;
            case KEY_D:
                camera.keys.right = true;
                break;
        }
    }

    switch (key) {
        case KEY_P:
            paused = !paused;
            break;

        case KEY_F1:
            ui.visible = !ui.visible;
            break;

        case KEY_ESCAPE:
#if defined(__ANDROID__)
#else
            glfwSetWindowShouldClose(window, 1);
#endif
            break;

        default:
            break;
    }
}

void vkx::ExampleBase::keyReleased(uint32_t key) {
    if (camera.firstperson) {
        switch (key) {
            case KEY_W:
                camera.keys.up = false;
                break;
            case KEY_S:
                camera.keys.down = false;
                break;
            case KEY_A:
                camera.keys.left = false;
                break;
            case KEY_D:
                camera.keys.right = false;
                break;
        }
    }
}

#if defined(__ANDROID__)

int32_t ExampleBase::handle_input_event(android_app* app, AInputEvent* event) {
    ExampleBase* exampleBase = reinterpret_cast<ExampleBase*>(app->userData);
    return exampleBase->onInput(event);
}

void ExampleBase::handle_app_cmd(android_app* app, int32_t cmd) {
    ExampleBase* exampleBase = reinterpret_cast<ExampleBase*>(app->userData);
    exampleBase->onAppCmd(cmd);
}

int32_t ExampleBase::onInput(AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        bool handled = false;
        ivec2 touchPoint;
        int32_t eventSource = AInputEvent_getSource(event);
        switch (eventSource) {
            case AINPUT_SOURCE_TOUCHSCREEN: {
                int32_t action = AMotionEvent_getAction(event);

                switch (action) {
                    case AMOTION_EVENT_ACTION_UP:
                        mouseButtons.left = false;
                        break;

                    case AMOTION_EVENT_ACTION_DOWN:
                        // Detect double tap
                        mouseButtons.left = true;
                        mousePos.x = AMotionEvent_getX(event, 0);
                        mousePos.y = AMotionEvent_getY(event, 0);
                        break;

                    case AMOTION_EVENT_ACTION_MOVE:
                        touchPoint.x = AMotionEvent_getX(event, 0);
                        touchPoint.y = AMotionEvent_getY(event, 0);
                        mouseMoved(vec2{ touchPoint });
                        break;

                    default:
                        break;
                }
            }
                return 1;
        }
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t keyCode = AKeyEvent_getKeyCode((const AInputEvent*)event);
        int32_t action = AKeyEvent_getAction((const AInputEvent*)event);
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
                paused = !paused;
                break;
        };
    }
    return 0;
}

void ExampleBase::onAppCmd(int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (vkx::android::androidApp->window != nullptr) {
                setupWindow();
                initVulkan();
                setupSwapchain();
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
    window = vkx::android::androidApp->window;
    size.width = ANativeWindow_getWidth(window);
    size.height = ANativeWindow_getHeight(window);
    camera.updateAspectRatio(size);
}

#else

void vkx::ExampleBase::setupWindow() {
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
        window = glfwCreateWindow(size.width, size.height, "My Title", monitor, nullptr);
    } else {
        size.width /= 2;
        size.height /= 2;
        window = glfwCreateWindow(size.width, size.height, "Window Title", nullptr, nullptr);
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
}

void vkx::ExampleBase::mouseAction(int button, int action, int mods) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            mouseButtons.left = action == GLFW_PRESS;
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            mouseButtons.right = action == GLFW_PRESS;
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            mouseButtons.middle = action == GLFW_PRESS;
            break;
    }
}

void vkx::ExampleBase::KeyboardHandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto example = (ExampleBase*)glfwGetWindowUserPointer(window);
    switch (action) {
        case GLFW_PRESS:
            example->keyPressed(key);
            break;

        case GLFW_RELEASE:
            example->keyReleased(key);
            break;

        default:
            break;
    }
}

void vkx::ExampleBase::MouseHandler(GLFWwindow* window, int button, int action, int mods) {
    auto example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->mouseAction(button, action, mods);
}

void vkx::ExampleBase::MouseMoveHandler(GLFWwindow* window, double posx, double posy) {
    auto example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->mouseMoved(glm::vec2(posx, posy));
}

void vkx::ExampleBase::MouseScrollHandler(GLFWwindow* window, double xoffset, double yoffset) {
    auto example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->mouseScrolled((float)yoffset);
}

void vkx::ExampleBase::CloseHandler(GLFWwindow* window) {
    auto example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->prepared = false;
    glfwSetWindowShouldClose(window, 1);
}

void vkx::ExampleBase::FramebufferSizeHandler(GLFWwindow* window, int width, int height) {
    auto example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->windowResize(glm::uvec2(width, height));
}

using VulkanExampleBase = vkx::ExampleBase;
#endif
