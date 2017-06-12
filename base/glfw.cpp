#include "glfw.hpp"

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

    void Window::prepareWindow() {
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, KeyboardHandler);
        glfwSetMouseButtonCallback(window, MouseButtonHandler);
        glfwSetCursorPosCallback(window, MouseMoveHandler);
        glfwSetWindowCloseCallback(window, CloseHandler);
        glfwSetFramebufferSizeCallback(window, FramebufferSizeHandler);
        glfwSetScrollCallback(window, MouseScrollHandler);
    }

    vk::SurfaceKHR createWindowSurface(vk::Instance instance, GLFWwindow* window, const vk::AllocationCallbacks* allocator) {
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
        example->mouseScrolled(yoffset);
    }

    void Window::CloseHandler(GLFWwindow* window) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->closeWindow();
    }

    void Window::FramebufferSizeHandler(GLFWwindow* window, int width, int height) {
        Window* example = (Window*)glfwGetWindowUserPointer(window);
        example->resizeWindow(glm::uvec2(width, height));
    }

}