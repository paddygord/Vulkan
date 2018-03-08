/*
* Vulkan Example - Instanced mesh rendering, uses a separate vertex buffer for instanced data
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <glad/glad.h>
#include <vulkanExampleBase.h>
#include <shapes.h>


//
//  GPUConfig.h
//  libraries/gpu/src/gpu
//
//  Created by Sam Gateau on 12/4/14.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

typedef PROC (APIENTRYP PFNWGLGETPROCADDRESS)(LPCSTR);
PFNWGLGETPROCADDRESS glad_wglGetProcAddress;
#define wglGetProcAddress glad_wglGetProcAddress


static void* getGlProcessAddress(const char *namez) {
    static HMODULE glModule = nullptr;
    if (!glModule) {
        glModule = LoadLibraryW(L"opengl32.dll");
        glad_wglGetProcAddress = (PFNWGLGETPROCADDRESS)GetProcAddress(glModule, "wglGetProcAddress");
    }

    auto result = wglGetProcAddress(namez);
    if (!result) {
        result = GetProcAddress(glModule, namez);
    }
    if (!result) {
        OutputDebugStringA(namez);
        OutputDebugStringA("\n");
    }
    return (void*)result;
}


void initGl() {
    static std::once_flag once;
    std::call_once(once, [] {
        gladLoadGLLoader(getGlProcessAddress);
    });
}

class VulkanExample : public vkx::ExampleBase {
    using Parent = vkx::ExampleBase;

    void updateDrawCommandBuffer(const vk::CommandBuffer& buffer) override {
    }

    void initVulkan() override {
        context.requireDeviceExtensions({ VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME });
        Parent::initVulkan();
        auto extensions = context.physicalDevice.enumerateDeviceExtensionProperties();
        for (const auto& extension : extensions) {
            OutputDebugStringA("");
        }
    }

    void setupWindow() override {
        Parent::setupWindow();
        
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glwindow = glfwCreateWindow(100, 100, "Test", nullptr, nullptr);
        glfwMakeContextCurrent(glwindow);
        initGl();
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(glwindow);
        auto extensions = glGetString(GL_EXTENSIONS);
        glfwMakeContextCurrent(nullptr);
    }


    GLFWwindow * glwindow{ nullptr };
};

RUN_EXAMPLE(VulkanExample)
