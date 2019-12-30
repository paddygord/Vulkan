#pragma once

#if !defined(ANDROID)
#include <string>
#include <vector>
#include <functional>
#include <set>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include <GLFW/glfw3.h>

namespace glfw {

class Window {
public:
    static bool init() { return GLFW_TRUE == glfwInit(); }
    static void terminate() { glfwTerminate(); }

    static std::vector<std::string> getRequiredInstanceExtensions() {
        std::vector<std::string> result;
        uint32_t count = 0;
        const char** names = glfwGetRequiredInstanceExtensions(&count);
        if (names && count) {
            result.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                result.emplace_back(names[i]);
            }
        }
        return result;
    }

    static vk::SurfaceKHR createWindowSurface(GLFWwindow* window, const vk::Instance& instance, const vk::AllocationCallbacks* pAllocator = nullptr) {
        VkSurfaceKHR rawSurface;
        vk::Result result = static_cast<vk::Result>(
            glfwCreateWindowSurface((VkInstance)instance, window, reinterpret_cast<const VkAllocationCallbacks*>(pAllocator), &rawSurface));
        return vk::createResultValue(result, rawSurface, "vk::CommandBuffer::begin");
    }
    vk::SurfaceKHR createSurface(const vk::Instance& instance, const vk::AllocationCallbacks* pAllocator = nullptr) {
        return createWindowSurface(window, instance, pAllocator);
    }

    void swapBuffers() const { glfwSwapBuffers(window); }

    void createWindow(const glm::uvec2& size, const glm::ivec2& position = { INT_MIN, INT_MIN }) {
        // Disable window resize
        window = glfwCreateWindow(size.x, size.y, "Window Title", nullptr, nullptr);
        if (position != glm::ivec2{ INT_MIN, INT_MIN }) {
            glfwSetWindowPos(window, position.x, position.y);
        }
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, KeyboardHandler);
        glfwSetMouseButtonCallback(window, MouseButtonHandler);
        glfwSetCursorPosCallback(window, MouseMoveHandler);
        glfwSetWindowCloseCallback(window, CloseHandler);
        glfwSetFramebufferSizeCallback(window, FramebufferSizeHandler);
        glfwSetScrollCallback(window, MouseScrollHandler);
    }

    void destroyWindow() {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    void makeCurrent() const { glfwMakeContextCurrent(window); }

    void present() const { glfwSwapBuffers(window); }

    void showWindow(bool show = true) {
        if (show) {
            glfwShowWindow(window);
        } else {
            glfwHideWindow(window);
        }
    }

    void setTitle(const std::string& title) { glfwSetWindowTitle(window, title.c_str()); }

    void setSizeLimits(const glm::uvec2& minSize, const glm::uvec2& maxSize = {}) {
        glfwSetWindowSizeLimits(window, minSize.x, minSize.y, (maxSize.x != 0) ? maxSize.x : minSize.x, (maxSize.y != 0) ? maxSize.y : minSize.y);
    }

    void runWindowLoop(const std::function<void()>& frameHandler) {
        while (0 == glfwWindowShouldClose(window)) {
            glfwPollEvents();
            frameHandler();
        }
    }

    //
    // Event handlers are called by the GLFW callback mechanism and should not be called directly
    //

    virtual void onWindowResized(const glm::uvec2& newSize) {}
    virtual void onWindowClosed() {}

    // Keyboard handling
    virtual void onKeyEvent(int key, int scancode, int action, int mods) {
        switch (action) {
            case GLFW_PRESS:
                onKeyPressed(key, mods);
                break;

            case GLFW_RELEASE:
                onKeyReleased(key, mods);
                break;

            default:
                break;
        }
    }

    virtual void onKeyPressed(int key, int mods) {}
    virtual void onKeyReleased(int key, int mods) {}

    // Mouse handling
    virtual void onMouseButtonEvent(int button, int action, int mods) {
        switch (action) {
            case GLFW_PRESS:
                onMousePressed(button, mods);
                break;

            case GLFW_RELEASE:
                onMouseReleased(button, mods);
                break;

            default:
                break;
        }
    }

    virtual void onMousePressed(int button, int mods) {}
    virtual void onMouseReleased(int button, int mods) {}
    virtual void onMouseMoved(const glm::vec2& newPos) {}
    virtual void onMouseScrolled(float delta) {}

private:
    static void KeyboardHandler(GLFWwindow* window, int key, int scancode, int action, int mods) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->onKeyEvent(key, scancode, action, mods);
    }
    static void MouseButtonHandler(GLFWwindow* window, int button, int action, int mods) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->onMouseButtonEvent(button, action, mods);
    }
    static void MouseMoveHandler(GLFWwindow* window, double posx, double posy) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->onMouseMoved(glm::vec2(posx, posy));
    }
    static void MouseScrollHandler(GLFWwindow* window, double xoffset, double yoffset) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->onMouseScrolled((float)yoffset);
    }
    static void CloseHandler(GLFWwindow* window) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->onWindowClosed();
    }
    static void FramebufferSizeHandler(GLFWwindow* window, int width, int height) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->onWindowResized(glm::uvec2(width, height));
    }

    GLFWwindow* window{ nullptr };
};
}  // namespace glfw

#endif
