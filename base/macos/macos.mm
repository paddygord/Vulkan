/*
 * Copyright (c) 2018 The Khronos Group Inc.
 * Copyright (c) 2018 Valve Corporation
 * Copyright (c) 2018 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Jeremy Kniager <jeremyk@lunarg.com>
 */

#include "macos.h"
#include <glad/glad.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>

#include <MoltenVK/vk_mvk_moltenvk.h>
#include <vulkan/vulkan.hpp>
#include <mutex>

@interface NativeMetalView : NSView
@end

@implementation NativeMetalView
- (id)initWithFrame:(NSRect) frame {
    if(self = [super initWithFrame: frame]){
        self.wantsLayer = YES;
    }
    return self;
}

- (CALayer*)makeBackingLayer {
    return [CAMetalLayer layer];
}
@end

void* CreateMetalView(uint32_t width, uint32_t height) {
    return [[NativeMetalView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
}

void DestroyMetalView(void* view) {
    [(NativeMetalView*)view dealloc];
}


namespace vks { namespace gl { namespace impl {


struct AAPLTextureFormatInfo {
    int                 cvPixelFormat;
    MTLPixelFormat      mtlFormat;
    vk::Format          vkFormat;
    GLuint              glInternalFormat;
    GLuint              glFormat;
    GLuint              glType;
    static const AAPLTextureFormatInfo*const findFormat(vk::Format format);
};

// Table of equivalent formats across CoreVideo, Metal, and OpenGL
static const AAPLTextureFormatInfo AAPLInteropFormatTable[] =
{
    // Core Video Pixel Format,               Metal Pixel Format,                                                   GL internalformat, GL format,   GL type
    { kCVPixelFormatType_32BGRA,              MTLPixelFormatBGRA8Unorm,      vk::Format::eB8G8R8A8Unorm,            GL_RGBA,           GL_BGRA_EXT, GL_UNSIGNED_INT_8_8_8_8_REV },
    { kCVPixelFormatType_ARGB2101010LEPacked, MTLPixelFormatBGR10A2Unorm,    vk::Format::eA2R10G10B10UnormPack32,   GL_RGB10_A2,       GL_BGRA,     GL_UNSIGNED_INT_2_10_10_10_REV },
    { kCVPixelFormatType_32BGRA,              MTLPixelFormatBGRA8Unorm_sRGB, vk::Format::eA8B8G8R8SrgbPack32,       0,                 GL_BGRA,     GL_UNSIGNED_INT_8_8_8_8_REV },
    { kCVPixelFormatType_64RGBAHalf,          MTLPixelFormatRGBA16Float,     vk::Format::eR16G16B16A16Sfloat,       0,                 GL_RGBA,     GL_HALF_FLOAT },
};

const AAPLTextureFormatInfo*const AAPLTextureFormatInfo::findFormat(vk::Format format) {
    static const NSUInteger AAPLNumInteropFormats = sizeof(AAPLInteropFormatTable) / sizeof(AAPLTextureFormatInfo);
    for(int i = 0; i < AAPLNumInteropFormats; i++) {
        if(format == AAPLInteropFormatTable[i].vkFormat) {
            return &AAPLInteropFormatTable[i];
        }
    }
    return nullptr;
}

struct SharedTextureImpl : public SharedTexture {
    static void init(const vks::Context& context);
    SharedTextureImpl(const vks::Context& context, const glm::uvec2& size, vk::Format format);
    ~SharedTextureImpl();
    void destroy() override;
    void createPixelBuffer();
    void createGLTexture();
    void createMTLTexture();
    void createVKImage();

    const glm::uvec2 _size;
    const AAPLTextureFormatInfo* _formatInfo;
    CVOpenGLTextureCacheRef _CVGLTextureCache;
    CVMetalTextureCacheRef _CVMTLTextureCache;
    CVPixelBufferRef _CVPixelBuffer;
    CVMetalTextureRef _CVMTLTexture;
    CVOpenGLTextureRef _CVGLTexture;
    id<MTLTexture> _metalTexture{ nil };
    const vks::Context& _vkContext;
    const NSOpenGLContext* _openGLContext { nullptr };
    CGLPixelFormatObj _CGLPixelFormat{ nullptr };
    id<MTLDevice> _metalDevice{ nil };
    static std::once_flag setupFlag;
    static PFN_vkGetMTLDeviceMVK vkGetMTLDeviceMVK;
    static PFN_vkSetMTLTextureMVK vkSetMTLTextureMVK;
    static PFN_vkGetMTLTextureMVK vkGetMTLTextureMVK;
};
    
std::once_flag SharedTextureImpl::setupFlag;

PFN_vkGetMTLDeviceMVK SharedTextureImpl::vkGetMTLDeviceMVK{nullptr};
PFN_vkSetMTLTextureMVK SharedTextureImpl::vkSetMTLTextureMVK{nullptr};
PFN_vkGetMTLTextureMVK SharedTextureImpl::vkGetMTLTextureMVK{nullptr};

} } }


using namespace vks::gl;
using namespace vks::gl::impl;

vks::gl::SharedTexture::Pointer vks::gl::SharedTexture::create(const vks::Context& context, const glm::uvec2& size, vk::Format format) {
    using namespace vks::gl::impl;
    auto result =std::make_shared<SharedTextureImpl>(context, size, format);
    return std::static_pointer_cast<SharedTexture>(result);
}

vks::gl::SharedTexture::~SharedTexture() {
    assert(glTexture == 0);
    assert(!vkImage);
}

void SharedTextureImpl::init(const vks::Context& context) {
    std::call_once(setupFlag, [&]{
        // Initialize the function pointers
        vkGetMTLDeviceMVK = PFN_vkGetMTLDeviceMVK(context.device.getProcAddr("vkGetMTLDeviceMVK"));
        vkSetMTLTextureMVK = PFN_vkSetMTLTextureMVK(context.device.getProcAddr("vkSetMTLTextureMVK"));
        vkGetMTLTextureMVK = PFN_vkGetMTLTextureMVK(context.device.getProcAddr("vkGetMTLTextureMVK"));
    });
}

void SharedTextureImpl::createPixelBuffer() {
    // Create the CoreVideo pixel buffer
    NSDictionary* cvBufferProperties = @{
                                         (__bridge NSString*)kCVPixelBufferOpenGLCompatibilityKey : @YES,
                                         (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey : @YES,
                                         };
    CVReturn cvret = CVPixelBufferCreate(kCFAllocatorDefault,
                                         _size.x, _size.y,
                                         _formatInfo->cvPixelFormat,
                                         (__bridge CFDictionaryRef)cvBufferProperties,
                                         &_CVPixelBuffer);
    
    if(cvret != kCVReturnSuccess) {
        throw std::runtime_error("Failed to create CVPixelBufferf");
    }
}

void SharedTextureImpl::createGLTexture(){

    CVReturn cvret;
    // 1. Create an OpenGL CoreVideo texture cache from the pixel buffer.
    cvret  = CVOpenGLTextureCacheCreate(
                                        kCFAllocatorDefault,
                                        nil,
                                        _openGLContext.CGLContextObj,
                                        _CGLPixelFormat,
                                        nil,
                                        &_CVGLTextureCache);
    if(cvret != kCVReturnSuccess)
    {
        throw std::runtime_error("Failed to create OpenGL Texture Cache");
    }
    // 2. Create a CVPixelBuffer-backed OpenGL texture image from the texture cache.
    cvret = CVOpenGLTextureCacheCreateTextureFromImage(
                                                       kCFAllocatorDefault,
                                                       _CVGLTextureCache,
                                                       _CVPixelBuffer,
                                                       nil,
                                                       &_CVGLTexture);
    if(cvret != kCVReturnSuccess)
    {
        throw std::runtime_error("Failed to create OpenGL Texture From Image");
    }
    // 3. Get an OpenGL texture name from the CVPixelBuffer-backed OpenGL texture image.
    glTexture = CVOpenGLTextureGetName(_CVGLTexture);
}

void SharedTextureImpl::createMTLTexture() {
    CVReturn cvret;
    // 1. Create a Metal Core Video texture cache from the pixel buffer.
    cvret = CVMetalTextureCacheCreate(
                                      kCFAllocatorDefault,
                                      nil,
                                      _metalDevice,
                                      nil,
                                      &_CVMTLTextureCache);
    if(cvret != kCVReturnSuccess)
    {
        throw std::runtime_error("Failed to create Metal texture cache");
    }
    // 2. Create a CoreVideo pixel buffer backed Metal texture image from the texture cache.
    cvret = CVMetalTextureCacheCreateTextureFromImage(
                                                      kCFAllocatorDefault,
                                                      _CVMTLTextureCache,
                                                      _CVPixelBuffer, nil,
                                                      _formatInfo->mtlFormat,
                                                      _size.x, _size.y,
                                                      0,
                                                      &_CVMTLTexture);
    if(cvret != kCVReturnSuccess)
    {
        throw std::runtime_error("Failed to create Metal texture cache");
    }
    // 3. Get a Metal texture using the CoreVideo Metal texture reference.
    _metalTexture = CVMetalTextureGetTexture(_CVMTLTexture);
    // Get a Metal texture object from the Core Video pixel buffer backed Metal texture image
    if(!_metalTexture)
    {
        throw std::runtime_error("Failed to get metal texture from CVMetalTextureRef");
    };
}

void SharedTextureImpl::createVKImage() {

    vk::ImageCreateInfo imageCreateInfo;
    imageCreateInfo.imageType = vk::ImageType::e2D;
    imageCreateInfo.format = vk::Format::eR8G8B8A8Unorm;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.extent.width = _size.x;
    imageCreateInfo.extent.height = _size.y;
    imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    vkImage = _vkContext.device.createImage(imageCreateInfo);
    if (!vkImage) {
        throw std::runtime_error("Unable to create Vulkan image");
    }
    if (_metalTexture) {
        vkSetMTLTextureMVK(vkImage, _metalTexture);
    }
}

SharedTextureImpl::SharedTextureImpl(const vks::Context& context, const glm::uvec2& size, vk::Format format) : _size(size), _formatInfo(AAPLTextureFormatInfo::findFormat(format)), _vkContext(context) {
    init(context);
    if(!_formatInfo) {
        throw std::runtime_error("Metal Format supplied not supported in this sample");
    }
    
    vkGetMTLDeviceMVK(_vkContext.physicalDevice, &_metalDevice);
    // Hack since the above call crashes when I try to create a CV Metal Texture Cache
    //_metalDevice = MTLCreateSystemDefaultDevice();
    //CFRetain(_metalDevice);
    _openGLContext = [NSOpenGLContext currentContext];
    _CGLPixelFormat = _openGLContext.pixelFormat.CGLPixelFormatObj;

    createPixelBuffer();
    createGLTexture();
    createMTLTexture();
    createVKImage();
}

SharedTextureImpl::~SharedTextureImpl() {
    destroy();
}

void SharedTextureImpl::destroy() {
}


