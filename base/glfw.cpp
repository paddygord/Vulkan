#include "glfw.hpp"
#include <mutex>

namespace glfw {
    std::set<std::string> getRequiredInstanceExtensions() {
        std::set<std::string> result;
        uint32_t count = 0;
        const char** names = glfwGetRequiredInstanceExtensions(&count);
        if (names && count) {
            for (uint32_t i = 0; i < count; ++i) {
                result.insert(names[i]);
            }
        }
        return result;
    }

    struct Destroyer {

        ~Destroyer() {
            glfwTerminate();
        }
    };

    static std::shared_ptr<Destroyer> DESTROYER;

    Window::Window() {
        static std::once_flag once;
        std::call_once(once, [] {
            if (GLFW_TRUE == glfwInit()) {
                DESTROYER.reset(new Destroyer());
            }
        });
    }

    void Window::updateJoysticks() {
        if (glfwJoystickPresent(0)) {
            // FIXME implement joystick handling
            int axisCount { 0 };
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
            uint32_t newButtons { 0 };
            static uint32_t oldButtons { 0 };
            {
                int buttonCount { 0 };
                const uint8_t* buttons = glfwGetJoystickButtons(0, &buttonCount);
                for (uint8_t i = 0; i < buttonCount && i < 64; ++i) {
                    if (buttons[i]) {
                        newButtons |= (1 << i);
                    }
                }
            }
            auto changedButtons = newButtons & ~oldButtons;
            if (changedButtons & 0x01) {
                keyEvent(GAMEPAD_BUTTON_A, 0, GLFW_PRESS, 0);
            }
            if (changedButtons & 0x02) {
                keyEvent(GAMEPAD_BUTTON_B, 0, GLFW_PRESS, 0);
            }
            if (changedButtons & 0x04) {
                keyEvent(GAMEPAD_BUTTON_X, 0, GLFW_PRESS, 0);
            }
            if (changedButtons & 0x08) {
                keyEvent(GAMEPAD_BUTTON_Y, 0, GLFW_PRESS, 0);
            }
            if (changedButtons & 0x10) {
                keyEvent(GAMEPAD_BUTTON_L1, 0, GLFW_PRESS, 0);
            }
            if (changedButtons & 0x20) {
                keyEvent(GAMEPAD_BUTTON_R1, 0, GLFW_PRESS, 0);
            }
            auto releasedButtons = oldButtons & ~newButtons;
            if (releasedButtons & 0x01) {
                keyEvent(GAMEPAD_BUTTON_A, 0, GLFW_RELEASE, 0);
            }
            if (releasedButtons & 0x02) {
                keyEvent(GAMEPAD_BUTTON_B, 0, GLFW_RELEASE, 0);
            }
            if (releasedButtons & 0x04) {
                keyEvent(GAMEPAD_BUTTON_X, 0, GLFW_RELEASE, 0);
            }
            if (releasedButtons & 0x08) {
                keyEvent(GAMEPAD_BUTTON_Y, 0, GLFW_RELEASE, 0);
            }
            if (releasedButtons & 0x10) {
                keyEvent(GAMEPAD_BUTTON_L1, 0, GLFW_RELEASE, 0);
            }
            if (releasedButtons & 0x20) {
                keyEvent(GAMEPAD_BUTTON_R1, 0, GLFW_RELEASE, 0);
            }

            oldButtons = newButtons;
        } else {
            memset(&gamePadState.axes, 0, sizeof(gamePadState.axes));
        }
    }

    void Window::runWindowLoop(const std::function<void()>& frameHandler) {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            updateJoysticks();
            frameHandler();
        }
    }


    void Window::createWindow(const glm::uvec2& size, const glm::ivec2& position) {
        // Disable window resize
        window = glfwCreateWindow(size.x, size.y, "Window Title", NULL, NULL);
        if (position != glm::ivec2 { INT_MIN, INT_MIN }) {
            glfwSetWindowPos(window, position.x, position.y);
        }
        prepareWindow();
    }

    void Window::setSizeLimits(const glm::uvec2& minSize, const glm::uvec2& maxSize) {
        glfwSetWindowSizeLimits(window, minSize.x, minSize.y, maxSize.x ? maxSize.x : minSize.x, maxSize.y ? maxSize.y : minSize.y);
    }

    void Window::showWindow(bool show) {
        if (show) {
            glfwShowWindow(window);
        } else {
            glfwHideWindow(window);
        }
    }

    void Window::prepareWindow() {
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, KeyboardHandler);
        glfwSetMouseButtonCallback(window, MouseButtonHandler);
        glfwSetCursorPosCallback(window, MouseMoveHandler);
        glfwSetWindowCloseCallback(window, CloseHandler);
        glfwSetFramebufferSizeCallback(window, FramebufferSizeHandler);
        glfwSetScrollCallback(window, MouseScrollHandler);
    }

    void Window::destroyWindow() {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    vk::SurfaceKHR createWindowSurface(const vk::Instance& instance, GLFWwindow* window, const vk::AllocationCallbacks* allocator) {
        VkSurfaceKHR rawSurface;
        vk::Result result = static_cast<vk::Result>(glfwCreateWindowSurface((VkInstance)instance, window, reinterpret_cast<const VkAllocationCallbacks*>(allocator), &rawSurface));
        return vk::createResultValue( result, rawSurface, "vk::CommandBuffer::begin" );
    }

    void Window::KeyboardHandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->keyEvent(key, scancode, action, mods);
    }

    void Window::MouseButtonHandler(GLFWwindow* window, int button, int action, int mods) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->mouseButtonEvent(button, action, mods);
    }

    void Window::MouseMoveHandler(GLFWwindow* window, double posx, double posy) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->mouseMoved(glm::vec2(posx, posy));
    }

    void Window::MouseScrollHandler(GLFWwindow* window, double xoffset, double yoffset) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->mouseScrolled((float)yoffset);
    }

    void Window::CloseHandler(GLFWwindow* window) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->windowClosed();
    }

    void Window::FramebufferSizeHandler(GLFWwindow* window, int width, int height) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->windowResized(glm::uvec2(width, height));
    }

}