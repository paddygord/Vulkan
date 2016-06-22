#include "common.hpp"

#include "vulkanContext.hpp"

namespace vkx {

    class SwapChain {
    private:
        vkx::Context context;
        vk::SurfaceKHR surface;
        
        struct SwapChainImage {
            vk::Image image;
            vk::ImageView view;
        };

    public:
        vk::Format colorFormat{ vk::Format::eUndefined };
        vk::ColorSpaceKHR colorSpace;
        vk::SwapchainKHR swapChain;

        uint32_t imageCount{ 0 };
        std::vector<SwapChainImage> buffers;

        // Creates an os specific surface
        // Tries to find a graphics and a present queue
        void initSurface(const vkx::Context& context, GLFWwindow* window) {
            this->context = context;
            // Create surface depending on OS
            vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo;
            surfaceCreateInfo.hinstance = GetModuleHandle(NULL);
            surfaceCreateInfo.hwnd = glfwGetWin32Window(window);
            surface = context.instance.createWin32SurfaceKHR(surfaceCreateInfo);

            // Get list of supported surface formats
            std::vector<vk::SurfaceFormatKHR> surfaceFormats = context.physicalDevice.getSurfaceFormatsKHR(surface);
            auto formatCount = surfaceFormats.size();


            // If the surface format list only includes one entry with  vk::Format::eUndefined,
            // there is no preferered format, so we assume  vk::Format::eB8G8R8A8Unorm
            if ((formatCount == 1) && (surfaceFormats[0].format == vk::Format::eUndefined)) {
                colorFormat = vk::Format::eB8G8R8A8Unorm;
            } else {
                // Always select the first available color format
                // If you need a specific format (e.g. SRGB) you'd need to
                // iterate over the list of available surface format and
                // check for it's presence
                colorFormat = surfaceFormats[0].format;
            }
            colorSpace = surfaceFormats[0].colorSpace;
        }

        // Create the swap chain and get images with given width and height
        void create(glm::uvec2& size) {
            // Get physical device surface properties and formats
            vk::SurfaceCapabilitiesKHR surfCaps = context.physicalDevice.getSurfaceCapabilitiesKHR(surface);
            // Get available present modes
            std::vector<vk::PresentModeKHR> presentModes = context.physicalDevice.getSurfacePresentModesKHR(surface);
            auto presentModeCount = presentModes.size();

            auto queueNodeIndex = context.findQueue(vk::QueueFlagBits::eGraphics, surface);

            vk::Extent2D swapchainExtent;
            // width and height are either both -1, or both not -1.
            if (surfCaps.currentExtent.width == -1) {
                // If the surface size is undefined, the size is set to
                // the size of the images requested.
                swapchainExtent.width = size.x;
                swapchainExtent.height = size.y;
            } else {
                // If the surface size is defined, the swap chain size must match
                swapchainExtent = surfCaps.currentExtent;
                size.x = surfCaps.currentExtent.width;
                size.y = surfCaps.currentExtent.height;
            }

            // Prefer mailbox mode if present, it's the lowest latency non-tearing present  mode
            vk::PresentModeKHR swapchainPresentMode = vk::PresentModeKHR::eFifo;
            for (size_t i = 0; i < presentModeCount; i++) {
                if (presentModes[i] == vk::PresentModeKHR::eMailbox) {
                    swapchainPresentMode = vk::PresentModeKHR::eMailbox;
                    break;
                }
                if ((swapchainPresentMode != vk::PresentModeKHR::eMailbox) && (presentModes[i] == vk::PresentModeKHR::eImmediate)) {
                    swapchainPresentMode = vk::PresentModeKHR::eImmediate;
                }
            }

            // Determine the number of images
            uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
            if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount)) {
                desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
            }

            // Normally we want an identity transform, but the surface might require some sort of 
            // alternative transformation, like mirroring vertically, if the hardware Y axis is 
            // inverted 
            vk::SurfaceTransformFlagBitsKHR preTransform;
            if (surfCaps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
                preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
            } else {
                preTransform = surfCaps.currentTransform;
            }

            vk::SwapchainKHR oldSwapChain = swapChain;
            {
                vk::SwapchainCreateInfoKHR swapchainCI;
                swapchainCI.surface = surface;
                swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
                swapchainCI.imageFormat = colorFormat;
                swapchainCI.imageColorSpace = colorSpace;
                swapchainCI.imageExtent = vk::Extent2D{ swapchainExtent.width, swapchainExtent.height };
                swapchainCI.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
                swapchainCI.preTransform = preTransform;
                swapchainCI.imageArrayLayers = 1;
                swapchainCI.imageSharingMode = vk::SharingMode::eExclusive;
                swapchainCI.queueFamilyIndexCount = queueNodeIndex;
                swapchainCI.pQueueFamilyIndices = NULL;
                swapchainCI.presentMode = swapchainPresentMode;
                swapchainCI.oldSwapchain = oldSwapChain;
                swapchainCI.clipped = true;
                swapchainCI.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
                swapChain = context.device.createSwapchainKHR(swapchainCI);
            }

            // If an existing sawp chain is re-created, destroy the old swap chain
            // This also cleans up all the presentable images
            if (oldSwapChain) {
                for (uint32_t i = 0; i < imageCount; i++) {
                    context.device.destroyImageView(buffers[i].view);
                }
                context.device.destroySwapchainKHR(oldSwapChain);
            }

            vk::ImageViewCreateInfo colorAttachmentView;
            colorAttachmentView.format = colorFormat;
            colorAttachmentView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            colorAttachmentView.subresourceRange.levelCount = 1;
            colorAttachmentView.subresourceRange.layerCount = 1;
            colorAttachmentView.viewType = vk::ImageViewType::e2D;

            // Get the swap chain images
            auto images = context.device.getSwapchainImagesKHR(swapChain);
            imageCount = (uint32_t)images.size();

            // Get the swap chain buffers containing the image and imageview
            buffers.resize(imageCount);
            for (uint32_t i = 0; i < imageCount; i++) {
                buffers[i].image = images[i];
                buffers[i].view = context.device.createImageView(colorAttachmentView.setImage(images[i]));
            }
        }

        // Acquires the next image in the swap chain
        uint32_t acquireNextImage(vk::Semaphore presentCompleteSemaphore) {
            auto resultValue = context.device.acquireNextImageKHR(swapChain, UINT64_MAX, presentCompleteSemaphore, vk::Fence());
            vk::Result result = resultValue.result;
            if (result != vk::Result::eSuccess) {
                // TODO handle eSuboptimalKHR
                std::cerr << "Invalid acquire result: " << vk::to_string(result);
                throw std::error_code(result);
            }

            return resultValue.value;
        }

        // Present the current image to the queue
        vk::Result queuePresent(vk::Queue queue, uint32_t currentBuffer, vk::Semaphore waitSemaphore) {
            vk::PresentInfoKHR presentInfo;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapChain;
            presentInfo.pImageIndices = &currentBuffer;
            if (waitSemaphore) {
                presentInfo.pWaitSemaphores = &waitSemaphore;
                presentInfo.waitSemaphoreCount = 1;
            }
            return queue.presentKHR(presentInfo);
        }


        // Free all Vulkan resources used by the swap chain
        void cleanup() {
            for (uint32_t i = 0; i < imageCount; i++) {
                context.device.destroyImageView(buffers[i].view);
            }
            context.device.destroySwapchainKHR(swapChain);
            context.instance.destroySurfaceKHR(surface);
        }
    };
}

class InitSwapchainExample : public vkx::Context {
    GLFWwindow* window{ nullptr };
    vkx::SwapChain swapChain;
    // The currently active swap chain image
    uint32_t currentBuffer{ 0 };
    glm::uvec2 size;

    vk::RenderPass renderPass;
    // List of command buffers (same as number of swap chain images)
    std::vector<vk::CommandBuffer> commandBuffers;
    // List of available frame buffers (same as number of swap chain images)
    std::vector<vk::Framebuffer> frameBuffers;
    struct {
        vk::Semaphore presentComplete;
        vk::Semaphore renderComplete;
    } semaphores;

public:
    InitSwapchainExample() {
        createContext(true);
        createWindow();
        createSwapChain();
        createRenderPass();
        createFramebuffers();
        createCommandBuffers();
        // Create synchronization objects
        vk::SemaphoreCreateInfo semaphoreCreateInfo;
        // Create a semaphore used to synchronize image presentation
        // Ensures that the image is displayed before we start submitting new commands to the queu
        semaphores.presentComplete = device.createSemaphore(semaphoreCreateInfo);
        // Create a semaphore used to synchronize command submission
        // Ensures that the image is not presented until all commands have been sumbitted and executed
        semaphores.renderComplete = device.createSemaphore(semaphoreCreateInfo);
    }

    ~InitSwapchainExample() {
        glfwDestroyWindow(window);
        glfwTerminate();
        destroyContext();
    }

    void createWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        auto monitor = glfwGetPrimaryMonitor();
        auto mode = glfwGetVideoMode(monitor);
        auto screenWidth = mode->width;
        auto screenHeight = mode->height;
        size = glm::uvec2 { screenWidth / 2, screenHeight / 2 };
        window = glfwCreateWindow(size.x, size.y, "Window Title", NULL, NULL);
        // Disable window resize
        glfwSetWindowSizeLimits(window, size.x, size.y, size.x, size.y);
        glfwSetWindowUserPointer(window, this);
        glfwSetWindowPos(window, 100, 100);
        glfwShowWindow(window);
    }

    void createSwapChain() {
        swapChain.initSurface(*this, window);
        swapChain.create(size);
    }

    std::vector<vk::AttachmentDescription> attachments;
    std::vector<vk::AttachmentReference> attachmentReferences;
    std::vector<vk::SubpassDescription> subpasses;
    std::vector<vk::SubpassDependency> subpassDependencies;

    void createRenderPass() {
        vk::AttachmentDescription colorAttachment;
        colorAttachment.format = swapChain.colorFormat;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
        attachments.push_back(colorAttachment);

        vk::AttachmentReference colorAttachmentReference;
        colorAttachmentReference.attachment = 0;
        colorAttachmentReference.layout = vk::ImageLayout::eColorAttachmentOptimal;
        attachmentReferences.push_back(colorAttachmentReference);

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = attachmentReferences.data();
        subpasses.push_back(subpass);

        vk::SubpassDependency dependency;
        dependency.srcSubpass = 0;
        dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;

        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        subpassDependencies.push_back(dependency);

        vk::RenderPassCreateInfo renderPassInfo;
        renderPassInfo.attachmentCount = (uint32_t)attachments.size();
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = (uint32_t)subpasses.size();
        renderPassInfo.pSubpasses = subpasses.data();
        renderPassInfo.dependencyCount = (uint32_t)subpassDependencies.size();
        renderPassInfo.pDependencies = subpassDependencies.data();
        renderPass = device.createRenderPass(renderPassInfo);
    }

    void createFramebuffers() {
        // Create frame buffers for every swap chain image
        frameBuffers.resize(swapChain.imageCount);

        vk::FramebufferCreateInfo frameBufferCreateInfo;
        frameBufferCreateInfo.renderPass = renderPass;
        frameBufferCreateInfo.attachmentCount = 1;
        frameBufferCreateInfo.width = size.x;
        frameBufferCreateInfo.height = size.y;
        frameBufferCreateInfo.layers = 1;

        for (uint32_t i = 0; i < frameBuffers.size(); i++) {
            frameBufferCreateInfo.pAttachments = &swapChain.buffers[i].view;
            frameBuffers[i] = device.createFramebuffer(frameBufferCreateInfo);
        }
    }

    void createCommandBuffers() {
        // Allocate command buffers
        {
            vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
            commandBufferAllocateInfo.commandPool = getCommandPool();
            commandBufferAllocateInfo.commandBufferCount = swapChain.imageCount;
            commandBufferAllocateInfo.level = vk::CommandBufferLevel::ePrimary;
            commandBuffers = device.allocateCommandBuffers(commandBufferAllocateInfo);
        }

        static const std::vector<VK_CLEAR_COLOR_TYPE> CLEAR_COLORS{
            vkx::clearColor({ 1, 0, 0, 0 }),
            vkx::clearColor({ 0, 1, 0, 0 }),
            vkx::clearColor({ 0, 0, 1, 0 }),
            vkx::clearColor({ 0, 1, 1, 0 }),
            vkx::clearColor({ 1, 0, 1, 0 }),
            vkx::clearColor({ 1, 1, 0, 0 }),
            vkx::clearColor({ 1, 1, 1, 0 }),
        };
        vk::CommandBufferBeginInfo cmdBufInfo;

        vk::ClearValue clearValue;
        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.extent.width = size.x;
        renderPassBeginInfo.renderArea.extent.height = size.y;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearValue;
        vk::Viewport viewport = vkx::viewport(size);
        vk::Rect2D scissor = vkx::rect2D(size);
        vk::DeviceSize offset = 0;

        for (size_t i = 0; i < swapChain.imageCount; ++i) {
            const auto& commandBuffer = commandBuffers[i];
            clearValue.color = CLEAR_COLORS[i % CLEAR_COLORS.size()];
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = frameBuffers[i];
            commandBuffer.begin(cmdBufInfo);
            commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            commandBuffer.endRenderPass();
            commandBuffer.end();
        }
    }

    void run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            prepareFrame();
            renderFrame();
            submitFrame();
            std::this_thread::sleep_for (std::chrono::seconds(1));
        }
    }

    void prepareFrame() {
        // Acquire the next image from the swap chaing
        currentBuffer = swapChain.acquireNextImage(semaphores.presentComplete);
    }

    void renderFrame() {
        // Wait for color attachment output to finish before rendering the text overlay
        vk::PipelineStageFlags stageFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submitInfo;
        submitInfo.pWaitDstStageMask = &stageFlags;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = commandBuffers.data() + currentBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &semaphores.renderComplete;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &semaphores.presentComplete;
        queue.submit(submitInfo, VK_NULL_HANDLE);
    }

    void submitFrame() {
        swapChain.queuePresent(queue, currentBuffer, semaphores.renderComplete);
        queue.waitIdle();
    }
};

RUN_EXAMPLE(InitSwapchainExample)

