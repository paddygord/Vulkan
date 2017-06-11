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

    vk::SurfaceKHR createWindowSurface(vk::Instance instance, GLFWwindow* window, const vk::AllocationCallbacks* allocator) {
        VkSurfaceKHR rawSurface;
        vk::Result result = static_cast<vk::Result>(glfwCreateWindowSurface((VkInstance)instance, window, reinterpret_cast<const VkAllocationCallbacks*>(allocator), &rawSurface));
        return vk::createResultValue( result, rawSurface, "vk::CommandBuffer::begin" );
    }
}