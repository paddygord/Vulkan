#include "textures.hpp"

#include <filesystem>
#include <atomic>
#include <iostream>

#include "utils.hpp"
#include "vks/filesystem.hpp"
#include "vks/storage.hpp"

using namespace vkx::texture;
namespace fs = std::experimental::filesystem;
static const std::string PNG_EXTENSION = ".png";
static const std::string KTX_EXTENSION = ".ktx";

#if 0
struct OutputHandler : public nvtt::OutputHandler {

    void beginImage(int size, int width, int height, int depth, int face, int miplevel) override {
    }

    bool writeData(const void* data, int size) override {
        return true;
    }

    void endImage() override {
    }
};

struct MyErrorHandler : public nvtt::ErrorHandler {
    virtual void error(nvtt::Error e) override {
        throw std::runtime_error(nvtt::errorString(e));
    }
};

class SequentialTaskDispatcher : public nvtt::TaskDispatcher {
public:
    SequentialTaskDispatcher(const std::atomic<bool>& abortProcessing) : _abortProcessing(abortProcessing) {};
    const std::atomic<bool>& _abortProcessing;

    void dispatch(nvtt::Task* task, void* context, int count) override {
        for (int i = 0; i < count; i++) {
            if (!_abortProcessing.load()) {
                task(context, i);
            } else {
                break;
            }
        }
    }
};

void generateHDRMips(gpu::Texture* texture, QImage&& image, const std::atomic<bool>& abortProcessing, int face) {
    // Take a local copy to force move construction
    // https://github.com/isocpp/CppCoreGuidelines/blob/master/CppCoreGuidelines.md#f18-for-consume-parameters-pass-by-x-and-stdmove-the-parameter
    QImage localCopy = std::move(image);

    assert(localCopy.format() == QIMAGE_HDR_FORMAT);

    const int width = localCopy.width(), height = localCopy.height();
    std::vector<glm::vec4> data;
    std::vector<glm::vec4>::iterator dataIt;
    auto mipFormat = texture->getStoredMipFormat();
    std::function<glm::vec3(uint32)> unpackFunc;

    nvtt::InputFormat inputFormat = nvtt::InputFormat_RGBA_32F;
    nvtt::WrapMode wrapMode = nvtt::WrapMode_Mirror;
    nvtt::AlphaMode alphaMode = nvtt::AlphaMode_None;

    nvtt::CompressionOptions compressionOptions;
    compressionOptions.setQuality(nvtt::Quality_Production);

    if (mipFormat == gpu::Element::COLOR_COMPRESSED_HDR_RGB) {
        compressionOptions.setFormat(nvtt::Format_BC6);
    } else if (mipFormat == gpu::Element::COLOR_RGB9E5) {
        compressionOptions.setFormat(nvtt::Format_RGB);
        compressionOptions.setPixelType(nvtt::PixelType_Float);
        compressionOptions.setPixelFormat(32, 32, 32, 0);
    } else if (mipFormat == gpu::Element::COLOR_R11G11B10) {
        compressionOptions.setFormat(nvtt::Format_RGB);
        compressionOptions.setPixelType(nvtt::PixelType_Float);
        compressionOptions.setPixelFormat(32, 32, 32, 0);
    } else {
        qCWarning(imagelogging) << "Unknown mip format";
        Q_UNREACHABLE();
        return;
    }

    if (HDR_FORMAT == gpu::Element::COLOR_RGB9E5) {
        unpackFunc = glm::unpackF3x9_E1x5;
    } else if (HDR_FORMAT == gpu::Element::COLOR_R11G11B10) {
        unpackFunc = glm::unpackF2x11_1x10;
    } else {
        qCWarning(imagelogging) << "Unknown HDR encoding format in QImage";
        Q_UNREACHABLE();
        return;
    }

    data.resize(width * height);
    dataIt = data.begin();
    for (auto lineNb = 0; lineNb < height; lineNb++) {
        const uint32* srcPixelIt = reinterpret_cast<const uint32*>(localCopy.constScanLine(lineNb));
        const uint32* srcPixelEnd = srcPixelIt + width;

        while (srcPixelIt < srcPixelEnd) {
            *dataIt = glm::vec4(unpackFunc(*srcPixelIt), 1.0f);
            ++srcPixelIt;
            ++dataIt;
        }
    }
    assert(dataIt == data.end());

    // We're done with the localCopy, free up the memory to avoid bloating the heap
    localCopy = QImage(); // QImage doesn't have a clear function, so override it with an empty one.

    nvtt::OutputOptions outputOptions;
    outputOptions.setOutputHeader(false);
    std::unique_ptr<nvtt::OutputHandler> outputHandler;
    MyErrorHandler errorHandler;
    outputOptions.setErrorHandler(&errorHandler);
    nvtt::Context context;
    int mipLevel = 0;

    if (mipFormat == gpu::Element::COLOR_RGB9E5 || mipFormat == gpu::Element::COLOR_R11G11B10) {
        // Don't use NVTT (at least version 2.1) as it outputs wrong RGB9E5 and R11G11B10F values from floats
        outputHandler.reset(new PackedFloatOutputHandler(texture, face, mipFormat));
    } else {
        outputHandler.reset(new OutputHandler(texture, face));
    }

    outputOptions.setOutputHandler(outputHandler.get());

    nvtt::Surface surface;
    surface.setImage(inputFormat, width, height, 1, &(*data.begin()));
    surface.setAlphaMode(alphaMode);
    surface.setWrapMode(wrapMode);

    SequentialTaskDispatcher dispatcher(abortProcessing);
    nvtt::Compressor compressor;
    context.setTaskDispatcher(&dispatcher);

    context.compress(surface, face, mipLevel++, compressionOptions, outputOptions);
    while (surface.canMakeNextMipmap() && !abortProcessing.load()) {
        surface.buildNextMipmap(nvtt::MipmapFilter_Box);
        context.compress(surface, face, mipLevel++, compressionOptions, outputOptions);
    }
}

class PNG {
public:
    using Pointer = std::shared_ptr<PNG>;
    png_structp png;
    png_infop info;
    std::vector<png_bytep> rows;
    glm::uvec2 size;
    png_byte colorType;
    png_byte depth;

    static Pointer parse(const std::string& filename);

    virtual ~PNG() {
        clear();
    }

private:

    using OffsetStorage = std::pair<vks::storage::StoragePointer, size_t>;

    static void readStorage(png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
        png_voidp io_ptr = png_get_io_ptr(png_ptr);
        OffsetStorage& storage = *reinterpret_cast<OffsetStorage*>(io_ptr);
        memcpy(outBytes, storage.first->data() + storage.second, byteCountToRead);
        storage.second += byteCountToRead;
    }

    void clear() {
        for (const auto& row : rows) {
            delete[] row;
        }
        rows.clear();
    }
};

PNG::Pointer PNG::parse(const std::string& filename) {
    auto storage = vks::storage::Storage::readFile(filename);
    if (png_sig_cmp((uint8_t*)storage->data(), 0, 8)) {
        throw std::runtime_error("[read_png_file] File is not recognized as a PNG file");
    }

    PNG::Pointer result = std::make_shared<PNG>();
    auto& png = *result;

    /* initialize stuff */
    png.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png.png)
        throw std::runtime_error("[read_png_file] png_create_read_struct failed");

    png.info = png_create_info_struct(png.png);
    if (!png.info)
        throw std::runtime_error("[read_png_file] png_create_info_struct failed");

    OffsetStorage offsetStorage{ storage, 8 };
    png_set_read_fn(png.png, &offsetStorage, &PNG::readStorage);
    png_set_sig_bytes(png.png, 8);
    png_read_info(png.png, png.info);
    png.size.x = png_get_image_width(png.png, png.info);
    png.size.y = png_get_image_height(png.png, png.info);
    png.colorType = png_get_color_type(png.png, png.info);
    png.depth = png_get_bit_depth(png.png, png.info);
    auto number_of_passes = png_set_interlace_handling(png.png);
    png_read_update_info(png.png, png.info);
    png.rows.resize(png.size.y);
    auto rowBytes = png_get_rowbytes(png.png, png.info);
    for (uint32_t y = 0; y < png.size.y; ++y) {
        png.rows[y] = new png_byte[rowBytes];
//        png_read_row(png.png, y, png.rows[y]);
    }
    png_read_image(png.png, png.rows.data());
    return result;
}
#endif


std::string convertPngToKtx(const std::string& sourceFilename) {
    fs::path p(sourceFilename);
    std::string destFilename = p.filename().string();
    destFilename = destFilename.substr(0, destFilename.size() - PNG_EXTENSION.size()) + KTX_EXTENSION;
    auto destPath = p.parent_path().append(destFilename);
    destFilename = destPath.string();
    if (fs::exists(destPath)) {
        return destFilename;
    }

    static const std::string pvrTexTool = "c:/Imagination/PowerVR_Graphics/PowerVR_Tools/PVRTexTool/CLI/Windows_x86_64/PVRTexToolCLI.exe";
    std::string command = pvrTexTool;
    command += " -i ";  
    command += sourceFilename;
    command += " -o ";
    command += destFilename;
    command += " -m ";
    command += " -f r8g8b8a8,UBN,lRGB ";
    system(command.c_str());

    return destFilename;
}

Texture2DPtr vkx::texture::loadTexture2D(const vks::Context& context, const std::string& filename) {
    if (vkx::ends_with(filename, PNG_EXTENSION)) {
        std::string convertedFilename = convertPngToKtx(filename);
        return loadTexture2D(context, convertedFilename);
    }
    if (!vkx::ends_with(filename, KTX_EXTENSION)) {
        throw std::runtime_error("Can't load textures other than PNG and KTX");
    }

    auto result = std::make_shared<vks::texture::Texture2D>();
    result->loadFromFile(context, filename, vk::Format::eR8G8B8A8Unorm);
    return result;
}

