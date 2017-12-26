#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vks { namespace util {

    std::vector<uint8_t> readBinaryFile(const std::string& filename);

    std::string readTextFile(const std::string& fileName);

} }
