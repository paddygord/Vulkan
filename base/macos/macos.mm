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


namespace vk {
    namespace mvk {
        static PFN_vkGetMTLDeviceMVK vkGetMTLDeviceMVK{nullptr};
        static PFN_vkSetMTLTextureMVK vkSetMTLTextureMVK{nullptr};
        static PFN_vkGetMTLTextureMVK vkGetMTLTextureMVK{nullptr};
        
        void init(const vk::Device& device) {
            if (!vkGetMTLDeviceMVK) {
                 vkGetMTLDeviceMVK = PFN_vkGetMTLDeviceMVK(device.getProcAddr("vkGetMTLDeviceMVK"));
                vkSetMTLTextureMVK = PFN_vkSetMTLTextureMVK(device.getProcAddr("vkSetMTLTextureMVK"));
                vkGetMTLTextureMVK = PFN_vkGetMTLTextureMVK(device.getProcAddr("vkGetMTLTextureMVK"));
            }
        }
    }
}
//void* x = vkGetMTLDeviceMVK;

//static PFN_vkGetMTLDeviceMVK vkGetMTLDeviceMVK = nullptr;

//static PFN_vkGetMTLDeviceMVK vkGetMTLDeviceMVK = nullptr;


struct AAPLTextureFormatInfo {
    int                 cvPixelFormat;
    MTLPixelFormat      mtlFormat;
    vk::Format          vkFormat;
    GLuint              glInternalFormat;
    GLuint              glFormat;
    GLuint              glType;
    static const AAPLTextureFormatInfo*const findFormat(vk::Format format);
    static const AAPLTextureFormatInfo*const findFormat(VkFormat format) {
        return findFormat((vk::Format)format);
    }
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

@interface GLInterop : NSObject

- (nonnull instancetype)initWithVulkanDevice:(VkPhysicalDevice) vkDevice
                           vkPixelFormat:(VkFormat)vkPixelFormat
                                       size:(CGSize)size;
@property (readonly, nonnull, nonatomic) id<MTLDevice> metalDevice;
@property (readonly, nonnull, nonatomic) id<MTLTexture> metalTexture;
@property (readonly, nonnull, nonatomic) NSOpenGLContext *openGLContext;
@property (readonly, nonatomic) GLuint openGLTexture;
@property (readonly, nonatomic) CGSize size;
@end

@implementation GLInterop
{
    const AAPLTextureFormatInfo *_formatInfo;
    CVPixelBufferRef _CVPixelBuffer;
    CVMetalTextureRef _CVMTLTexture;
    CVOpenGLTextureCacheRef _CVGLTextureCache;
    CVOpenGLTextureRef _CVGLTexture;
    CGLPixelFormatObj _CGLPixelFormat;
    // Metal
    CVMetalTextureCacheRef _CVMTLTextureCache;
    CGSize _size;
}

- (nonnull instancetype)initWithVulkanDevice:(VkPhysicalDevice) vkDevice
                           vkPixelFormat:(VkFormat)vkFormat
                                       size:(CGSize)size
{
    self = [super init];
    if(self)
    {
        _formatInfo = AAPLTextureFormatInfo::findFormat(vkFormat);
        
        if(!_formatInfo)
        {
            assert(!"Metal Format supplied not supported in this sample");
            return nil;
        }
        
        _size = size;
        vk::mvk::vkGetMTLDeviceMVK(vkDevice, &_metalDevice);
        _openGLContext = [NSOpenGLContext currentContext];
        _CGLPixelFormat = _openGLContext.pixelFormat.CGLPixelFormatObj;
        
        NSDictionary* cvBufferProperties = @{
                                             (__bridge NSString*)kCVPixelBufferOpenGLCompatibilityKey : @YES,
                                             (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey : @YES,
                                             };
        CVReturn cvret = CVPixelBufferCreate(kCFAllocatorDefault,
                                             size.width, size.height,
                                             _formatInfo->cvPixelFormat,
                                             (__bridge CFDictionaryRef)cvBufferProperties,
                                             &_CVPixelBuffer);
        
        if(cvret != kCVReturnSuccess)
        {
            assert(!"Failed to create CVPixelBufferf");
            return nil;
        }
        
        [self createGLTexture];
        [self createMetalTexture];
    }
    return self;
}

- (BOOL)createGLTexture
{
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
        assert(!"Failed to create OpenGL Texture Cache");
        return NO;
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
        assert(!"Failed to create OpenGL Texture From Image");
        return NO;
    }
    // 3. Get an OpenGL texture name from the CVPixelBuffer-backed OpenGL texture image.
    _openGLTexture = CVOpenGLTextureGetName(_CVGLTexture);
    
    return YES;
}

- (BOOL)createMetalTexture
{
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
        return NO;
    }
    // 2. Create a CoreVideo pixel buffer backed Metal texture image from the texture cache.
    cvret = CVMetalTextureCacheCreateTextureFromImage(
                                                      kCFAllocatorDefault,
                                                      _CVMTLTextureCache,
                                                      _CVPixelBuffer, nil,
                                                      _formatInfo->mtlFormat,
                                                      _size.width, _size.height,
                                                      0,
                                                      &_CVMTLTexture);
    if(cvret != kCVReturnSuccess)
    {
        assert(!"Failed to create Metal texture cache");
        return NO;
    }
    // 3. Get a Metal texture using the CoreVideo Metal texture reference.
    _metalTexture = CVMetalTextureGetTexture(_CVMTLTexture);
    // Get a Metal texture object from the Core Video pixel buffer backed Metal texture image
    if(!_metalTexture)
    {
        assert(!"Failed to get metal texture from CVMetalTextureRef");
        return NO;
    };
    
    return YES;
}

@end

namespace vks { namespace gl { namespace impl {

struct SharedTextureImpl : public SharedTexture {
    static void init(const vks::Context& context);
    SharedTextureImpl(const vks::Context& context, const glm::uvec2& size, vk::Format format);
    ~SharedTextureImpl();
    void destroy() override;
//    void createPixelBuffer();
//    void createGLTexture();
//    void createMTLTexture();
//    void createVKImage();

    const glm::uvec2 _size;
    const vks::Context& _vkContext;
    GLInterop* _interopTexture { nullptr };
//    const AAPLTextureFormatInfo* _formatInfo;
//    CVOpenGLTextureCacheRef _CVGLTextureCache;
//    CVMetalTextureCacheRef _CVMTLTextureCache;
//    CVPixelBufferRef _CVPixelBuffer;
//    CVMetalTextureRef _CVMTLTexture;
//    CVOpenGLTextureRef _CVGLTexture;
//    id<MTLTexture> _metalTexture{ nil };
//    const NSOpenGLContext* _openGLContext { nullptr };
//    CGLPixelFormatObj _CGLPixelFormat{ nullptr };
//    MTLDevice* _metalDevice{ nil };
    static std::once_flag setupFlag;
    static PFN_vkSetMTLTextureMVK vkSetMTLTextureMVK;
    static PFN_vkGetMTLTextureMVK vkGetMTLTextureMVK;
};
    
std::once_flag SharedTextureImpl::setupFlag;

} } }

using namespace vks::gl;

vks::gl::SharedTexture::Pointer vks::gl::SharedTexture::create(const vks::Context& context, const glm::uvec2& size, vk::Format format) {
    using namespace vks::gl::impl;
    auto result =std::make_shared<SharedTextureImpl>(context, size, format);
    return std::static_pointer_cast<SharedTexture>(result);
}

vks::gl::SharedTexture::~SharedTexture() {
    assert(glTexture == 0);
    assert(!vkImage);
}

using namespace vks::gl::impl;

void SharedTextureImpl::init(const vks::Context& context) {

}

SharedTextureImpl::SharedTextureImpl(const vks::Context& context, const glm::uvec2& size, vk::Format format)
    : _size(size)
    , _vkContext(context) {
    static std::once_flag once;
    std::call_once(setupFlag, [&]{
        vk::mvk::init(context.device);
    });
    CGSize cgsize{ (float)size.x, (float)size.y };
    VkFormat vkFormat = (VkFormat)(format);
    _interopTexture = [[GLInterop alloc] initWithVulkanDevice:context.physicalDevice
                                                vkPixelFormat:vkFormat
                                                         size:cgsize];
    //createVKImage();
}

SharedTextureImpl::~SharedTextureImpl() {
    destroy();
}

void SharedTextureImpl::destroy() {
}

#if 0
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
#endif


