#include "common.hpp"
#include "vulkanShapes.hpp"
#include "vulkanSwapChain.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1

class ShapesTestExample : public vkx::Context {
public:
    vkx::ShapesRenderer vulkanRenderer;
    GLFWwindow* window{ nullptr };
    glm::uvec2 size{ 1280, 720 };
    float fpsTimer{ 0 };
    float lastFPS{ 0 };
    uint32_t frameCounter{ 0 };
    vkx::SwapChain swapChain;
    std::vector<vk::CommandBuffer> cmdBuffers;
    struct {
        vk::Semaphore renderComplete;
    } semaphores;

    ShapesTestExample() : vulkanRenderer{ *this, true }, swapChain(*this) {
        createContext(true);

        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(size.x, size.y, "glfw", nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("Unable to create rendering window");
        }
        glfwSetWindowPos(window, 100, -1080 + 100);
        swapChain.createSurface(window);
        swapChain.create(vk::Extent2D{ size.x, size.y });
    }

    ~ShapesTestExample() {
        if (nullptr != window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

    void prepare() {
        vulkanRenderer.framebuffer.size = size;
        vulkanRenderer.prepare();

        semaphores.renderComplete = device.createSemaphore(vk::SemaphoreCreateInfo());
        if (cmdBuffers.empty()) {
            vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
            cmdBufAllocateInfo.commandPool = getCommandPool();
            cmdBufAllocateInfo.commandBufferCount = swapChain.imageCount;
            cmdBuffers = device.allocateCommandBuffers(cmdBufAllocateInfo);
        }

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;

        vk::ImageBlit blit;
        blit.srcOffsets[1].x = size.x;
        blit.srcOffsets[1].y = size.y;
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[1].x = size.x;
        blit.dstOffsets[1].y = size.y;
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.layerCount = 1;
        for (size_t i = 0; i < swapChain.imageCount; ++i) {
            vk::CommandBuffer& cmdBuffer = cmdBuffers[i];
            cmdBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
            cmdBuffer.begin(beginInfo);
            vkx::setImageLayout(cmdBuffer, swapChain.images[i].image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
            cmdBuffer.blitImage(vulkanRenderer.framebuffer.colors[0].image, vk::ImageLayout::eTransferSrcOptimal, swapChain.images[i].image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eNearest);
            vkx::setImageLayout(cmdBuffer, swapChain.images[i].image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);
            cmdBuffer.end();
        }
    }


    void render() {
        vk::Fence submitFence = swapChain.getSubmitFence(true);
        auto currentImage = swapChain.acquireNextImage(vulkanRenderer.semaphores.renderStart);
        vulkanRenderer.render();

        submit(
            cmdBuffers[currentImage], 
            { { vulkanRenderer.semaphores.renderComplete,  vk::PipelineStageFlagBits::eBottomOfPipe } }, 
            { semaphores.renderComplete }, 
            submitFence);

        swapChain.queuePresent(semaphores.renderComplete);
    }


    void run() {
        prepare();
        auto tStart = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window)) {
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            tStart = tEnd;
            glm::mat4 view1 = glm::translate(glm::mat4(), glm::vec3(-0.063f, 0, -2.5f));
            glm::mat4 view2 = glm::translate(glm::mat4(), glm::vec3(-0.063f, 0, -2.5f));
            glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)(size.x / 2) / (float)size.y, 0.001f, 256.0f);
            vulkanRenderer.update((float)tDiff / 1000.0f, { projection, projection }, { view1, view2 });
            glfwPollEvents();
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
        }
    }

    std::string getWindowTitle() {
        std::string device(deviceProperties.deviceName);
        return "OpenGL Interop - " + device + " - " + std::to_string(frameCounter) + " fps";
    }
};

RUN_EXAMPLE(ShapesTestExample)
