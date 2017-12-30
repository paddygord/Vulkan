#include "utils.hpp"

#include <mutex>
#include <algorithm>

const std::string& vkx::getAssetPath() {
#if defined(__ANDROID__)
    static const std::string NOTHING;
    return NOTHING;
#else
    static std::string path;
    static std::once_flag once;
    std::call_once(once, [] {
        std::string file(__FILE__);
        std::replace(file.begin(), file.end(), '\\', '/');
        std::string::size_type lastSlash = file.rfind("/");
        file = file.substr(0, lastSlash);
        path = file + "/../data/";
    });
    return path;
#endif

}
