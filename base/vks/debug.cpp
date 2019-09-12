/*
* Vulkan examples debug wrapper
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "debug.hpp"
#include <iostream>
#include <sstream>
#include <mutex>

#if 0 
#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "org.saintandreas.vulkan"
#endif

using namespace vks;

namespace vks { namespace debug {

#if defined(__ANDROID__)
std::list<std::string> validationLayerNames = { "VK_LAYER_GOOGLE_threading",      "VK_LAYER_LUNARG_parameter_validation",
                                                "VK_LAYER_LUNARG_object_tracker", "VK_LAYER_LUNARG_core_validation",
                                                "VK_LAYER_LUNARG_swapchain",      "VK_LAYER_GOOGLE_unique_objects" };
#else
std::list<std::string> validationLayerNames = {
    // This is a meta layer that enables all of the standard
    // validation layers in the correct order :
    // threading, parameter_validation, device_limits, object_tracker, image, core_validation, swapchain, and unique_objects
    "VK_LAYER_LUNARG_assistant_layer", "VK_LAYER_LUNARG_standard_validation"
};
#endif

static std::once_flag dispatcherInitFlag;
vk::DispatchLoaderDynamic dispatcher;
vk::DebugReportCallbackEXT msgCallback;

VkBool32 messageCallback(VkDebugReportFlagsEXT flags,
                         VkDebugReportObjectTypeEXT objType,
                         uint64_t srcObject,
                         size_t location,
                         int32_t msgCode,
                         const char* pLayerPrefix,
                         const char* pMsg,
                         void* pUserData) {
    std::string message;
    {
        std::stringstream buf;
        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
            buf << "ERROR: ";
        } else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
            buf << "WARNING: ";
        } else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
            buf << "PERF: ";
        } else {
            return false;
        }
        buf << "[" << pLayerPrefix << "] Code " << msgCode << " : " << pMsg;
        message = buf.str();
    }

    std::cout << message << std::endl;

#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_DEBUG, LOG_TAG, message.c_str());
#endif
#ifdef _MSC_VER
    OutputDebugStringA(message.c_str());
    OutputDebugStringA("\n");
#endif
    return false;
}

void setupDebugging(const vk::Instance& instance, const vk::DebugReportFlagsEXT& flags, const MessageHandler& handler) {
    std::call_once(dispatcherInitFlag, [&] { dispatcher.init(instance, &vkGetInstanceProcAddr); });
    vk::DebugReportCallbackCreateInfoEXT dbgCreateInfo = {};
    dbgCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)messageCallback;
    dbgCreateInfo.flags = flags;
    msgCallback = instance.createDebugReportCallbackEXT(dbgCreateInfo, nullptr, dispatcher);
}

void freeDebugCallback(const vk::Instance& instance) {
    std::call_once(dispatcherInitFlag, [&] { dispatcher.init(instance, &vkGetInstanceProcAddr); });
    instance.destroyDebugReportCallbackEXT(msgCallback, nullptr, dispatcher);
}

}}  // namespace vks::debug
#endif