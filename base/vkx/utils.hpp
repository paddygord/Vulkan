#pragma once

#include <string>
#include <filesystem>

namespace vkx {
const std::string& getAssetPath();

enum class LogLevel
{
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
};

void logMessage(LogLevel level, const char* format, ...);
}  // namespace vkx

#include <mutex>
#include <algorithm>
#include <stdarg.h>

#if defined(__ANDROID__)
#include <memory>
#include <android/log.h>
#include <android/configuration.h>

#else
#ifdef WIN32
#include <Windows.h>
#endif
#include <iostream>
#endif

#if defined(__ANDROID__)
inline int logLevelToAndroidPriority(vkx::LogLevel level) {
    switch (level) {
        case vkx::LogLevel::LOG_DEBUG:
            return ANDROID_LOG_DEBUG;
        case vkx::LogLevel::LOG_INFO:
            return ANDROID_LOG_INFO;
        case vkx::LogLevel::LOG_WARN:
            return ANDROID_LOG_WARN;
        case vkx::LogLevel::LOG_ERROR:
            return ANDROID_LOG_ERROR;
    }
}
#endif

inline void vkx::logMessage(vkx::LogLevel level, const char* format, ...) {
    va_list arglist;
    va_start(arglist, format);

#if defined(__ANDROID__)
    int prio = logLevelToAndroidPriority(level);
    __android_log_vprint(prio, "vulkanExample", format, arglist);
#else
    char buffer[8192];
    vsnprintf(buffer, 8192, format, arglist);
#ifdef WIN32
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
#endif
    std::cout << buffer << std::endl;
#endif
    va_end(arglist);
}

inline const std::string& vkx::getAssetPath() {
#if defined(__ANDROID__)
    static const std::string NOTHING;
    return NOTHING;
#else
    static std::string path;
    static std::once_flag once;
    std::call_once(once, [] {
        std::filesystem::path p{ __FILE__ };
        p = p.parent_path().parent_path().parent_path();
        p = p / "data";
        path = p.string() + "/";
    });
    return path;
#endif
}
