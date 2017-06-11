#pragma once

#include <string>
#include <vector>
#include <set>
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

namespace glfw {
    std::set<std::string> getRequiredInstanceExtensions();
    vk::SurfaceKHR createWindowSurface(vk::Instance, GLFWwindow* window, const vk::AllocationCallbacks* pAllocator = nullptr);
}