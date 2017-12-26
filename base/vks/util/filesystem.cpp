#include "filesystem.hpp"

#include <fstream>
#include <istream>
#include <iterator>

namespace vks { namespace util {


std::vector<uint8_t> readBinaryFile(const std::string& filename) {
#if defined(__ANDROID__)
    // Load shader from compressed asset
    AAsset* asset = AAssetManager_open(global_android_app->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
    assert(asset);
    size_t size = AAsset_getLength(asset);
    assert(size > 0);
    std::vector<uint8_t> result;
    result.resize(size);
    AAsset_read(asset, result.data(), size);
    AAsset_close(asset);
    return result;
#else
    // open the file:
    std::ifstream file(filename, std::ios::binary);
    // Stop eating new lines in binary mode!!!
    file.unsetf(std::ios::skipws);

    // get its size:
    std::streampos fileSize;

    file.seekg(0, std::ios::end);
    fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // reserve capacity
    std::vector<uint8_t> vec;
    vec.reserve(fileSize);

    // read the data:
    vec.insert(vec.begin(),
        std::istream_iterator<uint8_t>(file),
        std::istream_iterator<uint8_t>());

    return vec;
#endif
}

std::string readTextFile(const std::string& fileName) {
    std::string fileContent;
    std::ifstream fileStream(fileName, std::ios::in);

    if (!fileStream.is_open()) {
        throw std::runtime_error("File " + fileName + " not found");
    }
    std::string line = "";
    while (!fileStream.eof()) {
        getline(fileStream, line);
        fileContent.append(line + "\n");
    }
    fileStream.close();
    return fileContent;
}

} }
