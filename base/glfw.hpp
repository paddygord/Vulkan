#pragma once

#include <string>
#include <vector>
#include <set>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

namespace glfw {
    std::set<std::string> getRequiredInstanceExtensions();
    vk::SurfaceKHR createWindowSurface(vk::Instance, GLFWwindow* window, const vk::AllocationCallbacks* pAllocator = nullptr);

    class Window {

    protected:
        virtual void prepareWindow();
        virtual void keyEvent(int key, int scancode, int action, int mods) { }
        virtual void mouseButtonEvent(int button, int action, int mods) { }
        virtual void mouseMoved(const glm::vec2& newPos) { }
        virtual void mouseScrolled(float delta) { }
        virtual void resizeWindow(const glm::uvec2& newSize) { }
        virtual void closeWindow() { }

        static void KeyboardHandler(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void MouseButtonHandler(GLFWwindow* window, int button, int action, int mods);
        static void MouseMoveHandler(GLFWwindow* window, double posx, double posy);
        static void MouseScrollHandler(GLFWwindow* window, double xoffset, double yoffset);
        static void CloseHandler(GLFWwindow* window);
        static void FramebufferSizeHandler(GLFWwindow* window, int width, int height);

        GLFWwindow* window { nullptr };
    };

}