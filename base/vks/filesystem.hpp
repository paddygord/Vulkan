#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vks { namespace util {

    void withBinaryFileContexts(const std::string& filename, std::function<void(size_t size, const void* data)> handler);

    //std::vector<uint8_t> readBinaryFile(const std::string& filename);

    std::string readTextFile(const std::string& fileName);

} }
