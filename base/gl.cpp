#include "gl.hpp"
#include <mutex>

typedef PROC(APIENTRYP PFNWGLGETPROCADDRESS)(LPCSTR);
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

void gl::init() {
    static std::once_flag once;
    std::call_once(once, [] {
        gladLoadGL();
        //gladLoadGLLoader(getGlProcessAddress);
    });
}
