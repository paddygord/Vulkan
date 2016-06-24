#include "common.hpp"
#include "vulkanContext.hpp"

namespace vkx {
    struct SwapChainImage {
        vk::Image image;
        vk::ImageView view;
        vk::Fence fence;
    };

    class SwapChain {
    private:
        const vkx::Context& context;
        vk::SurfaceKHR surface;
        vk::SwapchainKHR swapChain;
        std::vector<SwapChainImage> images;
        vk::PresentInfoKHR presentInfo;

    public:
        vk::Format colorFormat;
        vk::ColorSpaceKHR colorSpace;
        uint32_t imageCount{ 0 };
        uint32_t currentImage{ 0 };
        // Index of the deteced graphics and presenting device queue
        uint32_t queueNodeIndex = UINT32_MAX;

        SwapChain(const vkx::Context& context) : context(context) {
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapChain;
            presentInfo.pImageIndices = &currentImage;
        }

        // Creates an os specific surface
        // Tries to find a graphics and a present queue
        void create(
#ifdef __ANDROID__
            ANativeWindow* window,
#else
            GLFWwindow* window,
#endif
            vk::Extent2D& size
        ) {
            // Create surface depending on OS
#ifdef __ANDROID__
            vk::AndroidSurfaceCreateInfoKHR surfaceCreateInfo;
            surfaceCreateInfo.window = window;
            surface = instance.createAndroidSurfaceKHR(surfaceCreateInfo);
#else
#ifdef WIN32
            // Create surface depending on OS
            vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo;
            surfaceCreateInfo.hinstance = GetModuleHandle(NULL);
            surfaceCreateInfo.hwnd = glfwGetWin32Window(window);
            surface = context.instance.createWin32SurfaceKHR(surfaceCreateInfo);
#else
            //vk::XcbSurfaceCreateInfoKHR surfaceCreateInfo;
            //surfaceCreateInfo.connection = connection;
            //surfaceCreateInfo.window = window;
            //surface = context.instance.createXcbSurfaceKHR(surfaceCreateInfo);

            VkSurfaceKHR vk_surf;
            vk::Result result = static_cast<vk::Result>(glfwCreateWindowSurface(context.instance, window, NULL, &vk_surf));
            if (result != vk::Result::eSuccess) {
                std::cerr << "Window surface creation failed: " << vk::to_string(result);
                throw std::error_code(result);
            }
            surface = vk_surf;
#endif
#endif

            // Get list of supported surface formats
            std::vector<vk::SurfaceFormatKHR> surfaceFormats = context.physicalDevice.getSurfaceFormatsKHR(surface);
            auto formatCount = surfaceFormats.size();


            // If the surface format list only includes one entry with  vk::Format::eUndefined,
            // there is no preferered format, so we assume  vk::Format::eB8G8R8A8Unorm
            if ((formatCount == 1) && (surfaceFormats[0].format ==  vk::Format::eUndefined)) {
                colorFormat =  vk::Format::eB8G8R8A8Unorm;
            } else {
                // Always select the first available color format
                // If you need a specific format (e.g. SRGB) you'd need to
                // iterate over the list of available surface format and
                // check for it's presence
                colorFormat = surfaceFormats[0].format;
            }
            colorSpace = surfaceFormats[0].colorSpace;

            // Find a queue for both present and graphics
            queueNodeIndex = context.findQueue(vk::QueueFlagBits::eGraphics, surface);

            vk::SwapchainKHR oldSwapchain = swapChain;

            // Get physical device surface properties and formats
            vk::SurfaceCapabilitiesKHR surfCaps = context.physicalDevice.getSurfaceCapabilitiesKHR(surface);
            // Get available present modes
            std::vector<vk::PresentModeKHR> presentModes = context.physicalDevice.getSurfacePresentModesKHR(surface);
            auto presentModeCount = presentModes.size();

            vk::Extent2D swapchainExtent;
            // width and height are either both -1, or both not -1.
            if (surfCaps.currentExtent.width == -1) {
                // If the surface size is undefined, the size is set to
                // the size of the images requested.
                swapchainExtent = size;
            } else {
                // If the surface size is defined, the swap chain size must match
                swapchainExtent = surfCaps.currentExtent;
                size = surfCaps.currentExtent;
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

            vk::SurfaceTransformFlagBitsKHR preTransform;
            if (surfCaps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
                preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
            } else {
                preTransform = surfCaps.currentTransform;
            }


            auto imageFormat = context.physicalDevice.getImageFormatProperties(colorFormat, vk::ImageType::e2D, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment, vk::ImageCreateFlags());

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
            swapchainCI.queueFamilyIndexCount = 0;
            swapchainCI.pQueueFamilyIndices = NULL;
            swapchainCI.presentMode = swapchainPresentMode;
            swapchainCI.oldSwapchain = oldSwapchain;
            swapchainCI.clipped = true;
            swapchainCI.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;

            swapChain = context.device.createSwapchainKHR(swapchainCI);


            // If an existing sawp chain is re-created, destroy the old swap chain
            // This also cleans up all the presentable images
            if (oldSwapchain) {
                for (uint32_t i = 0; i < imageCount; i++) {
                    context.device.destroyImageView(images[i].view);
                }
                context.device.destroySwapchainKHR(oldSwapchain);
            }

            vk::ImageViewCreateInfo colorAttachmentView;
            colorAttachmentView.format = colorFormat;
            colorAttachmentView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            colorAttachmentView.subresourceRange.levelCount = 1;
            colorAttachmentView.subresourceRange.layerCount = 1;
            colorAttachmentView.viewType = vk::ImageViewType::e2D;

            // Get the swap chain images
            auto swapChainImages = context.device.getSwapchainImagesKHR(swapChain);
            imageCount = (uint32_t)swapChainImages.size();

            // Get the swap chain buffers containing the image and imageview
            images.resize(imageCount);
            for (uint32_t i = 0; i < imageCount; i++) {
                images[i].image = swapChainImages[i];
                colorAttachmentView.image = swapChainImages[i];
                images[i].view = context.device.createImageView(colorAttachmentView);
            }
        }

        std::vector<vk::Framebuffer> createFramebuffers(vk::FramebufferCreateInfo framebufferCreateInfo) {
            // Verify that the first attachment is null
            assert(framebufferCreateInfo.pAttachments[0] == vk::ImageView());


            std::vector<vk::ImageView> attachments;
            attachments.resize(framebufferCreateInfo.attachmentCount);
            for (size_t i = 0; i < framebufferCreateInfo.attachmentCount; ++i) {
                attachments[i] = framebufferCreateInfo.pAttachments[i];
            }
            framebufferCreateInfo.pAttachments = attachments.data();

            std::vector<vk::Framebuffer> framebuffers;
            framebuffers.resize(imageCount);
            for (uint32_t i = 0; i < imageCount; i++) {
                attachments[0] = images[i].view;
                framebuffers[i] = context.device.createFramebuffer(framebufferCreateInfo);
            }
            return framebuffers;
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

            currentImage = resultValue.value;
            return currentImage;
        }
        
        vk::Fence getSubmitFence() {
            auto& image = images[currentImage];
            while (image.fence) {
                vk::Result fenceRes = context.device.waitForFences(image.fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT);
                if (fenceRes == vk::Result::eSuccess) {
                    image.fence = vk::Fence();
                }
            }

            image.fence = context.device.createFence(vk::FenceCreateFlags());
            return image.fence;
        }

        // Present the current image to the queue
        vk::Result queuePresent(vk::Semaphore waitSemaphore) {
            presentInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
            presentInfo.pWaitSemaphores = &waitSemaphore;
            return context.queue.presentKHR(presentInfo);
        }

        // Free all Vulkan resources used by the swap chain
        void cleanup() {
            for (uint32_t i = 0; i < imageCount; i++) {
                context.device.destroyImageView(images[i].view);
            }
            context.device.destroySwapchainKHR(swapChain);
            context.instance.destroySurfaceKHR(surface);
        }
    };
}

class SwapchainExample : public vkx::Context {
    GLFWwindow* window{ nullptr };
    vkx::SwapChain swapChain;
    // The currently active swap chain image
    uint32_t currentBuffer{ 0 };
    vk::Extent2D size;

    vk::RenderPass renderPass;
    // List of available frame buffers (same as number of swap chain images)
    std::vector<vk::Framebuffer> framebuffers;
    // List of command buffers (same as number of swap chain images)
    std::vector<vk::CommandBuffer> commandBuffers;

    // A list of fences associated with each image in the swap chain,
    // and thus with each command buffer.  
    //
    // In order to ensure a command buffer isn't used simultaneously by the queue,
    // we ensure that the fence associated with any prior submission is signalled
    // before submitting it again.  
    std::vector<vk::Fence> submitFences;
    struct {
        vk::Semaphore acquireComplete;
        vk::Semaphore renderComplete;
    } semaphores;

public:
    SwapchainExample() : swapChain(*this) {
        // Construct the Vulkan instance just as we did in the init_context example
        bool enableValidation = true;
        createContext(enableValidation);

        // Construct the window.  The window doesn't need any special attributes, it just 
        // need to be a native Win32 or XCB window surface. Window is independent of the contenxt and
        // RenderPass creation.  It can creation can occur before or after them.
        createWindow();

        // Using the window surface, construct the swap chain.  The swap chain is dependent on both 
        // the Vulkan instance as well as the window surface, so it needs to happen after
        swapChain.create(window, size);
        submitFences.resize(swapChain.imageCount);

        // Create a renderpass.  
        //
        // A renderpass defines what combination of input and output attachments types will be used 
        // during a given set of rendering operations, as well as what subpasses 
        // 
        // Note, it doesn't reference the actual images, just defines the kinds of images, they're 
        // layouts, and how the layouts will change over the course of executing commands during the 
        // renderpass.  Therefore it can be created almost immediately after the context and typically
        // doesn't need to change over time in response to things like window resizing , or rendering a 
        // different set of objects, or using different pipelines for rendering.  
        //
        // A RenderPass is required for creating framebuffers and pipelines, which can then only be used
        // with that specific RenderPass OR another RenderPass that is considered compatible.  
        // 
        // Creation of the RenderPass is dependent on the Vulkan context creation, but not on the window
        // or the SwapChain.  
        createRenderPass();

        // Create the Framebuffers to which we will render output that will be presented to the screen.  
        // As noted above, any FrameBuffer is dependent on a RenderPass and can only be used with that 
        // RenderPass or another RenderPass compatible with it.  It's also typically dpenedent on the 
        // Window, since usually you'll be creating at least one set of Framebuffers specifically for 
        // presentation to the window surface, and that set (which we are creating here) must must be using
        // the images acquired from the SwapChain, and must match the size of those images.
        // 
        // Common practice is to create an individual Framebuffer for each of the SwapChain images,
        // although all of them can typically share the same depth image, since they will not be 
        // in use concurrently
        createFramebuffers();

        // Create the CommandBuffer objects which will contain the commands we execute for our rendering.  
        // 
        // Similar to the Framebuffers, we will create one for each of the swap chain images.  
        createCommandBuffers();

        // Finally, we need to create a number of Sempahores.  Semaphores are used for GPU<->GPU 
        // synchronization.  Tyipically this means that you include them in certain function calls to 
        // tell the GPU to wait until the semaphore is signalled before actually executing the commands
        // or that once it's completed the commands, it should signal the semaphore, or both.  
        vk::SemaphoreCreateInfo semaphoreCreateInfo;
        // Create a semaphore used to synchronize image presentation
        // This semaphore will be signaled when the system actually displays an image.  By waiting on this
        // semaphore, we can ensure that the GPU doesn't start working on the next frame until the image 
        // for it has been acquired (typically meaning that it's previous contents have been presented to the screen)
        semaphores.acquireComplete = device.createSemaphore(semaphoreCreateInfo);
        // Create a semaphore used to synchronize command submission
        // This semaphore is used to ensure that before we submit a given image for presentation, all the rendering 
        // command for generating the image have been completed.
        semaphores.renderComplete = device.createSemaphore(semaphoreCreateInfo);
    }

    ~SwapchainExample() {
        glfwDestroyWindow(window);
        glfwTerminate();
        destroyContext();
    }

    void createWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        auto monitor = glfwGetPrimaryMonitor();
        auto mode = glfwGetVideoMode(monitor);
        uint32_t screenWidth = mode->width;
        uint32_t screenHeight = mode->height;
        size = vk::Extent2D{ screenWidth / 2, screenHeight / 2 };
        window = glfwCreateWindow(size.width, size.height, "Window Title", NULL, NULL);
        // Disable window resize
        glfwSetWindowSizeLimits(window, size.width, size.height, size.width, size.height);
        glfwSetWindowUserPointer(window, this);
        glfwSetWindowPos(window, 100, 100);
        glfwShowWindow(window);
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
        std::array<vk::ImageView, 1> imageViews;
        vk::FramebufferCreateInfo framebufferCreateInfo;
        framebufferCreateInfo.renderPass = renderPass;
        // Create a placeholder image view for the swap chain color attachments
        framebufferCreateInfo.attachmentCount = (uint32_t)imageViews.size();
        framebufferCreateInfo.pAttachments = imageViews.data();
        framebufferCreateInfo.width = size.width;
        framebufferCreateInfo.height = size.height;
        framebufferCreateInfo.layers = 1;

        // Create frame buffers for every swap chain image
        framebuffers =  swapChain.createFramebuffers(framebufferCreateInfo);
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
        renderPassBeginInfo.renderArea.extent = size;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearValue;
        vk::Viewport viewport = vkx::viewport(size);
        vk::Rect2D scissor = vkx::rect2D(size);
        vk::DeviceSize offset = 0;

        for (size_t i = 0; i < swapChain.imageCount; ++i) {
            const auto& commandBuffer = commandBuffers[i];
            clearValue.color = CLEAR_COLORS[i % CLEAR_COLORS.size()];
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = framebuffers[i];
            commandBuffer.begin(cmdBufInfo);
            commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            commandBuffer.endRenderPass();
            commandBuffer.end();
        }
    }

    void prepareFrame() {
        // Acquire the next image from the swap chaing
        currentBuffer = swapChain.acquireNextImage(semaphores.acquireComplete);
    }

    void renderFrame() {
        vk::PipelineStageFlags stageFlags = vk::PipelineStageFlagBits::eBottomOfPipe;
        vk::SubmitInfo submitInfo;
        submitInfo.pWaitDstStageMask = &stageFlags;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = commandBuffers.data() + currentBuffer;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &semaphores.acquireComplete;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &semaphores.renderComplete;

        vk::Fence submitFence = swapChain.getSubmitFence();
        if (submitFences[currentBuffer]) {
            device.destroyFence(submitFences[currentBuffer]);
        }
        submitFences[currentBuffer] = submitFence;
        queue.submit(submitInfo, submitFence);
    }

    void submitFrame() {
        swapChain.queuePresent(semaphores.renderComplete);
    }

    void run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            prepareFrame();
            renderFrame();
            submitFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

};

RUN_EXAMPLE(SwapchainExample)

