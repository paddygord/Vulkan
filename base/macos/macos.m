/*
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
 * Based on code from the Apple Metal/OpenGL interop example:
 * https://developer.apple.com/documentation/metal/mixing_metal_and_opengl_rendering_in_a_view
 *
 */

#include "macos.h"
#include <glad/glad.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>
#import <MoltenVK/vk_mvk_moltenvk.h>

static PFN_vkGetMTLDeviceMVK lvkGetMTLDeviceMVK = nullptr;
static PFN_vkSetMTLTextureMVK lvkSetMTLTextureMVK = nullptr;
static id<MTLDevice> _MTLDevice{ nil };

void InitSharedTextures(VkInstance instance, VkPhysicalDevice device) {
    if (!lvkGetMTLDeviceMVK) {
        lvkGetMTLDeviceMVK = (PFN_vkGetMTLDeviceMVK)vkGetInstanceProcAddr(instance, "vkGetMTLDeviceMVK");
        lvkGetMTLDeviceMVK(device, &_MTLDevice);
        [_MTLDevice retain];
    }
}

@interface SharedGLTexture : NSObject
- (nonnull instancetype)initWithFormat:(VkFormat)vkPixelFormat
                                device:(VkDevice)vkDevice
                                 width:(uint32_t)width
                                height:(uint32_t)height;
@property (readonly, nonatomic) GLuint glTexture;
@property (readonly, nonatomic) VkImage vkImage;
@property (readonly, nonatomic) uint32_t height;
@property (readonly, nonatomic) uint32_t width;
@end

struct AAPLTextureFormatInfo {
    int                 cvPixelFormat;
    MTLPixelFormat      mtlFormat;
    VkFormat            vkFormat;
    GLuint              glInternalFormat;
    GLuint              glFormat;
    GLuint              glType;
};

// Table of equivalent formats across CoreVideo, Metal, Vulkan and OpenGL
static const AAPLTextureFormatInfo AAPLInteropFormatTable[] =
{
    // Core Video Pixel Format,               Metal Pixel Format,            Vulkan Format                        GL internalformat, GL format,   GL type
    { kCVPixelFormatType_32BGRA,              MTLPixelFormatBGRA8Unorm,      VK_FORMAT_B8G8R8A8_UNORM,            GL_RGBA,           GL_BGRA,     GL_UNSIGNED_INT_8_8_8_8_REV },
    { kCVPixelFormatType_ARGB2101010LEPacked, MTLPixelFormatBGR10A2Unorm,    VK_FORMAT_A2R10G10B10_UNORM_PACK32,  GL_RGB10_A2,       GL_BGRA,     GL_UNSIGNED_INT_2_10_10_10_REV },
    { kCVPixelFormatType_32BGRA,              MTLPixelFormatBGRA8Unorm_sRGB, VK_FORMAT_A8B8G8R8_SRGB_PACK32,      GL_SRGB8_ALPHA8,   GL_BGRA,     GL_UNSIGNED_INT_8_8_8_8_REV },
    { kCVPixelFormatType_64RGBAHalf,          MTLPixelFormatRGBA16Float,     VK_FORMAT_R16G16B16A16_SFLOAT,       GL_RGBA,           GL_RGBA,     GL_HALF_FLOAT },
};

static const AAPLTextureFormatInfo*const findFormat(VkFormat format) {
    static const NSUInteger AAPLNumInteropFormats = sizeof(AAPLInteropFormatTable) / sizeof(AAPLTextureFormatInfo);
    for(int i = 0; i < AAPLNumInteropFormats; i++) {
        if(format == AAPLInteropFormatTable[i].vkFormat) {
            return &AAPLInteropFormatTable[i];
        }
    }
    return nullptr;
}


@implementation SharedGLTexture
{
    const AAPLTextureFormatInfo *_formatInfo;
    id<MTLTexture> _MTLTexture;
    VkDevice _vkDevice;
    NSOpenGLContext* _GLContext;
    CVPixelBufferRef _CVPixelBuffer;
    CVMetalTextureRef _CVMTLTexture;
    CVOpenGLTextureCacheRef _CVGLTextureCache;
    CVOpenGLTextureRef _CVGLTexture;
    CGLPixelFormatObj _CGLPixelFormat;
    CVMetalTextureCacheRef _CVMTLTextureCache;
    CGSize _size;
}

- (nonnull instancetype)initWithFormat:(VkFormat)vkFormat
                                device:(VkDevice)vkDevice
                                 width:(uint32_t)width
                                height:(uint32_t)height
{
    self = [super init];
    if(self)
    {
        _width = width;
        _height = height;
        _vkDevice = vkDevice;
        _formatInfo = findFormat(vkFormat);
        if(!_formatInfo)
        {
            assert(!"Metal Format supplied not supported in this sample");
            return nil;
        }
        

        _size = { (float)_width, (float)_height };
        _GLContext = [NSOpenGLContext currentContext];
        _CGLPixelFormat = _GLContext.pixelFormat.CGLPixelFormatObj;
        
        NSDictionary* cvBufferProperties = @{
                                             (__bridge NSString*)kCVPixelBufferOpenGLCompatibilityKey : @YES,
                                             (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey : @YES,
                                             };
        CVReturn cvret = CVPixelBufferCreate(kCFAllocatorDefault,
                                             _width, _height,
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
        [self createVkImage];
        
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
                                        _GLContext.CGLContextObj,
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
    _glTexture = CVOpenGLTextureGetName(_CVGLTexture);
    return YES;
}

- (BOOL)createMetalTexture
{
    if (_MTLDevice) {
        CVReturn cvret;
        // 1. Create a Metal Core Video texture cache from the pixel buffer.
        cvret = CVMetalTextureCacheCreate(
                                          kCFAllocatorDefault,
                                          nil,
                                          _MTLDevice,
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
        _MTLTexture = CVMetalTextureGetTexture(_CVMTLTexture);
        // Get a Metal texture object from the Core Video pixel buffer backed Metal texture image
        if(!_MTLTexture)
        {
            assert(!"Failed to get metal texture from CVMetalTextureRef");
            return NO;
        };
        [_MTLTexture retain];
    }
    return YES;
}

- (BOOL)createVkImage
{
    VkImageCreateInfo imageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = _formatInfo->vkFormat,
        .mipLevels = 1,
        .arrayLayers = 1,
        .extent.depth = 1,
        .extent.width = _width,
        .extent.height = _height,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    };

    VkResult result = vkCreateImage(_vkDevice, &imageCreateInfo, nullptr, &_vkImage);
    if ((result != VK_SUCCESS) || _vkImage == VK_NULL_HANDLE) {
        return NO;
    }

    if (!lvkSetMTLTextureMVK) {
        lvkSetMTLTextureMVK = (PFN_vkSetMTLTextureMVK)vkGetDeviceProcAddr(_vkDevice, "vkSetMTLTextureMVK");
    }

    result = lvkSetMTLTextureMVK(_vkImage, _MTLTexture);
    if (result != VK_SUCCESS) {
        return NO;
    }
    return YES;
}

@end




void* CreateSharedTexture(VkDevice vkDevice, uint32_t width, uint32_t height, VkFormat format) {
    SharedGLTexture* result = [[SharedGLTexture alloc] initWithFormat:format
                                                               device:vkDevice
                                                                width:width
                                                               height:height];
    return result;
}

uint32_t GetSharedGLTexture(void* sharedTexture) {
    SharedGLTexture* st = (SharedGLTexture*)sharedTexture;
    return st.glTexture;
}

VkImage GetSharedVkImage(void* sharedTexture) {
    SharedGLTexture* st = (SharedGLTexture*)sharedTexture;
    return st.vkImage;
}
void DestroySharedTexture(void* sharedTexture) {
    SharedGLTexture* st = (SharedGLTexture*)sharedTexture;
    [st dealloc];
}


#if 0

struct DevicePickerCriteria {
    vk::PhysicalDeviceProperties properties{};
    vk::PhysicalDeviceMemoryProperties memProperties{};
    
    DevicePickerCriteria() {}
    
    DevicePickerCriteria(const vk::PhysicalDevice& device)
        : properties(device.getProperties())
        , memProperties(device.getMemoryProperties()) { }
    
    vk::DeviceSize deviceLocalMemory() const {
        vk::DeviceSize result = 0;
        for (uint32_t i = 0; i < memProperties.memoryHeapCount; ++i) {
            const auto& heap = memProperties.memoryHeaps[i];
            if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
                result += heap.size;
            }
        }
        return result;
    }
    
    bool operator>(const DevicePickerCriteria& o) const {
        return (properties.deviceType <= vk::PhysicalDeviceType::eDiscreteGpu) && (properties.deviceType > o.properties.deviceType);
    }
};

vks::DevicePickerFunction vks::mvk::getMoltenVKDevicePicker() {
    return [&](const vk::Instance& instance, const std::vector<vk::PhysicalDevice>& devices) -> vk::PhysicalDevice {

        vk::PhysicalDevice result;
        DevicePickerCriteria resultCriteria;

        for (const auto& device : devices) {
            auto criteria = DevicePickerCriteria::create(device);
            if (criteria > resultCriteria) {
                result = device;
                resultCriteria = criteria;
                continue;
            }
        }
        return result;
    };
}

#endif
