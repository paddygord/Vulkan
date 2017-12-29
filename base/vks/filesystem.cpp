#include "filesystem.hpp"

#include <fstream>
#include <istream>
#include <iterator>
#include <functional>

#ifdef WIN32
#include <Windows.h>
#endif

namespace vks { namespace util {

void withBinaryFileContexts(const std::string& filename, std::function<void(size_t size, const void* data)> handler) {
#if defined(__ANDROID__)
    // Load shader from compressed asset
    AAsset* asset = AAssetManager_open(global_android_app->activity->assetManager, filename.c_str(), AASSET_MODE_BUFFER);
    assert(asset);
    size_t size = AAsset_getLength(asset);
    assert(size > 0);
    const void* buffer = AAsset_getBuffer(asset);
    if (buffer != NULL) {
        handler(size, buffer);
    } else {
        std::vector<uint8_t> result;
        result.resize(size);
        AAsset_read(asset, result.data(), size);
    }
    AAsset_close(asset);
    result.resize(size);
    AAsset_read(asset, result.data(), size);
    AAsset_close(asset);
    return result;
#elif (WIN32)
    auto hFile = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    size_t fileSize; 
    {
        DWORD dwFileSizeHigh;
        fileSize = GetFileSize(hFile, &dwFileSizeHigh);
        fileSize += (((size_t)dwFileSizeHigh) << 32);
    }
    auto hMapFile = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMapFile == NULL) {
        throw std::runtime_error("Failed to map file");
    }
    auto data = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0);
    handler(fileSize, data);
    UnmapViewOfFile(data);
    CloseHandle(hMapFile);
    CloseHandle(hFile);
#else
    // FIXME move to memory mapped files
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

    handler(vec.size(), vec.data());
#endif
}

std::vector<uint8_t> readBinaryFile(const std::string& filename) {
    std::vector<uint8_t> result;
    withBinaryFileContexts(filename, [&](size_t size, const void* data) {
        result.resize(size);
        memcpy(result.data(), data, size);
    });
    return result;
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
