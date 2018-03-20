#pragma once

#include <string>

namespace vkx {
    const std::string& getAssetPath();

    enum class LogLevel {
        LOG_DEBUG = 0,
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR,
    };

    void logMessage(LogLevel level, const char* format, ...);

    inline bool ends_with(const std::string& value, const std::string& ending) {
        if (ending.size() > value.size())
            return false;
        return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
    }

}
