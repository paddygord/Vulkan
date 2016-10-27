#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

namespace glfw {
    std::vector<const char*> getRequiredInstanceExtensions();
    vk::SurfaceKHR createWindowSurface(vk::Instance, GLFWwindow* window, const vk::AllocationCallbacks* pAllocator = nullptr);
}