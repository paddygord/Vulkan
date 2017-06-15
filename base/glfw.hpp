#pragma once

#include <string>
#include <vector>
#include <functional>
#include <set>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#define GAMEPAD_BUTTON_A 0x1000
#define GAMEPAD_BUTTON_B 0x1001
#define GAMEPAD_BUTTON_X 0x1002
#define GAMEPAD_BUTTON_Y 0x1003
#define GAMEPAD_BUTTON_L1 0x1004
#define GAMEPAD_BUTTON_R1 0x1005
#define GAMEPAD_BUTTON_START 0x1006

namespace glfw {
    std::set<std::string> getRequiredInstanceExtensions();
    vk::SurfaceKHR createWindowSurface(const vk::Instance& instance, GLFWwindow* window, const vk::AllocationCallbacks* pAllocator = nullptr);

    class Window {
    protected:
        Window();

        virtual void createWindow(const glm::uvec2& size, const glm::ivec2& position = { INT_MIN, INT_MIN });
        void showWindow(bool show = true);
        void setSizeLimits(const glm::uvec2& minSize, const glm::uvec2& maxSize = {});
        virtual void prepareWindow();
        virtual void destroyWindow();
        void runWindowLoop(const std::function<void()>& frameHandler);
        
        //
        // Event handlers are called by the GLFW callback mechanism and should not be called directly
        //

        virtual void windowResized(const glm::uvec2& newSize) { }
        virtual void windowClosed() { }

        // Keyboard handling
        virtual void keyEvent(int key, int scancode, int action, int mods) { 
            switch(action) {
            case GLFW_PRESS:
                keyPressed(key, mods);
                break;

            case GLFW_RELEASE:
                keyReleased(key, mods);
                break;

            default:
                break;
            }
        }
        virtual void keyPressed(int key, int mods) { }
        virtual void keyReleased(int key, int mods) { }

        // Mouse handling 
        virtual void mouseButtonEvent(int button, int action, int mods) { 
            switch (action) {
            case GLFW_PRESS:
                mousePressed(button, mods);
                break;

            case GLFW_RELEASE:
                mouseReleased(button, mods);
                break;

            default:
                break;
            }
        }
        virtual void mousePressed(int button, int mods) { }
        virtual void mouseReleased(int button, int mods) { }
        virtual void mouseMoved(const glm::vec2& newPos) { }
        virtual void mouseScrolled(float delta) { }

        void updateJoysticks();
        static void KeyboardHandler(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void MouseButtonHandler(GLFWwindow* window, int button, int action, int mods);
        static void MouseMoveHandler(GLFWwindow* window, double posx, double posy);
        static void MouseScrollHandler(GLFWwindow* window, double xoffset, double yoffset);
        static void CloseHandler(GLFWwindow* window);
        static void FramebufferSizeHandler(GLFWwindow* window, int width, int height);

        GLFWwindow* window { nullptr };
        // Gamepad state (only one pad supported)
        struct GamePadState {
            struct Axes {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                float rz = 0.0f;
            } axes;
        } gamePadState;

    };

}