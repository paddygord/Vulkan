/*
* Vulkan Example - OpenGL interoperability example
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>
#include <vks/texture.hpp>
#include <unordered_map>
#include <Windows.h>

#if 1
#include <D:/OVRAvatarSDK/include/OVR_Avatar.h>
#pragma comment(lib, "D:/OVRAvatarSDK/Windows/libovravatar.lib")


class OpenGLInteropExample {
public:
    static void log(const char* str) {
        OutputDebugStringA(str);
        OutputDebugStringA("\n");
    }

    ovrAvatar* avatar{ nullptr };
    std::unordered_map<ovrAvatarAssetID, ovrAvatarAsset*> assets;
    
     
    void onAvatarSpec(const ovrAvatarMessage_AvatarSpecification* spec) {
        avatar = ovrAvatar_Create(spec->avatarSpec, ovrAvatarCapability_All);

        auto avatarComponentCount = ovrAvatarComponent_Count(avatar);
        for (uint32_t i = 0; i < avatarComponentCount; ++i) {
            auto component = ovrAvatarComponent_Get(avatar, i);
            log("");
        }


        auto assetCount = ovrAvatar_GetReferencedAssetCount(avatar);
        for (uint32_t i = 0; i < assetCount; ++i) {
            auto assetId = ovrAvatar_GetReferencedAsset(avatar, i);
            if (0 == assets.count(assetId)) {
                assets.insert({ assetId, nullptr });
                ovrAvatarAsset_BeginLoading(assetId);
            }
        }
    }

    void onAvatarCombinedMesh(ovrAvatarAsset* asset) {
        uint32_t idCount;
        auto ids = ovrAvatarAsset_GetCombinedMeshIDs(asset, &idCount);
        auto meshData = ovrAvatarAsset_GetCombinedMeshData(asset);

        for (uint32_t i = 0; i < idCount; ++i) {
            auto destId = ids[i];
            assets[destId] = asset;
        }

        Sleep(1);
    }

    void onAvatarMesh(const ovrAvatarMeshAssetData* meshData) {
        Sleep(1);
    }

    void onAvatarTexture(const ovrAvatarTextureAssetData* textureData) {
        Sleep(1);
    }

    void onAvatarMaterial(const ovrAvatarMaterialState* materialState) {
        Sleep(1);
    }

    void onAvatarPbsMaterial(const ovrAvatarPBSMaterialState* materialState) {
        Sleep(1);
    }


    bool onAvatarAsset(const ovrAvatarMessage_AssetLoaded* assetLoadedMessage) {
        const auto& asset = assetLoadedMessage->asset;
        const auto& assetID = assetLoadedMessage->assetID;
        const auto& lod = assetLoadedMessage->lod;
        assets[assetID] = asset;
        auto type = ovrAvatarAsset_GetType(asset);
        switch (type) {
        case ovrAvatarAssetType_Mesh:
            log("Got mesh");
            onAvatarMesh(ovrAvatarAsset_GetMeshData(asset));
            break;

        case ovrAvatarAssetType_CombinedMesh:
            log("Got combined mesh");
            onAvatarCombinedMesh(asset);
            break;


        case ovrAvatarAssetType_Texture:
            log("Got texture");
            onAvatarTexture(ovrAvatarAsset_GetTextureData(asset));
            break;

        case ovrAvatarAssetType_Material:
            log("Got material");
            onAvatarMaterial(ovrAvatarAsset_GetMaterialData(asset));
            break;

        case ovrAvatarAssetType_PBSMaterial:
            log("Got PBS material");
            onAvatarPbsMaterial(ovrAvatarAsset_GetPBSMaterialData(asset));
            break;

        case ovrAvatarAssetType_FailedLoad:
            throw std::runtime_error("Failed load");
        case ovrAvatarAssetType_Pose:
        default:
            throw std::runtime_error("Unknown asset type");
        }

        bool full = true;
        for (const auto& entry : assets) {
            if (nullptr == entry.second) {
                full = false;
            }
        }
        return full;
    }

    void run() {
        ovrAvatar_Initialize("Test");
        ovrAvatar_RegisterLoggingCallback(&OpenGLInteropExample::log);
        ovrAvatar_SetLoggingLevel(ovrAvatarLogLevel_Verbose);

        {
            auto specRequest = ovrAvatarSpecificationRequest_Create(10150022857785745);
            ovrAvatarSpecificationRequest_SetCombineMeshes(specRequest, true);
            ovrAvatar_RequestAvatarSpecificationFromSpecRequest(specRequest);
            ovrAvatarSpecificationRequest_Destroy(specRequest);
        }

        // ovrAvatar_RequestAvatarSpecification(10150022857785745);
        bool loaded = false;
        while (!loaded) {
            ovrAvatarMessage* message = ovrAvatarMessage_Pop();
            if (nullptr == message) {
                Sleep(10);
                continue;
            }
            auto type = ovrAvatarMessage_GetType(message);
            switch (type) {
            case ovrAvatarMessageType_AvatarSpecification:
                onAvatarSpec(ovrAvatarMessage_GetAvatarSpecification(message));
                break;
            case ovrAvatarMessageType_AssetLoaded:
                loaded = onAvatarAsset(ovrAvatarMessage_GetAssetLoaded(message));
                break;
            default:
                throw std::runtime_error("Bad message");

            }
            OutputDebugStringA("Message \n");
            ovrAvatarMessage_Free(message);
        }

        if (avatar != nullptr) {
            ovrAvatar_Destroy(avatar);
            avatar = nullptr;
        }
        ovrAvatar_Shutdown();
    }
};

#elif !defined(__ANDROID__)

#define SHOW_GL_WINDOW 0

// Indices into the semaphores
#define READY 0
#define COMPLETE 1
#define SEMAPHORE_COUNT 2

#ifdef WIN32
const auto semaphoreHandleType = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
const auto memoryHandleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
const auto semaphoreHandleType = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
const auto memoryHandleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif

namespace gl { namespace import {

#if defined(WIN32)
    using HandleType = HANDLE;
#else
    using HandleType = int;
#endif

std::set<vk::ImageTiling> getSupportedTiling() {
    GLint numTilingTypes{ 0 };

    glGetInternalformativ(GL_TEXTURE_2D, GL_RGBA8, GL_NUM_TILING_TYPES_EXT, 1, &numTilingTypes);
    std::vector<GLint> glTilingTypes;
    {
        glTilingTypes.resize(numTilingTypes);
        glGetInternalformativ(GL_TEXTURE_2D, GL_RGBA8, GL_TILING_TYPES_EXT, numTilingTypes, glTilingTypes.data());
    }

    std::set<vk::ImageTiling> result;
    for (const auto& glTilingType : glTilingTypes) {
        switch (glTilingType) {
            case GL_LINEAR_TILING_EXT:
                result.insert(vk::ImageTiling::eLinear);
                break;

            case GL_OPTIMAL_TILING_EXT:
                result.insert(vk::ImageTiling::eOptimal);
                break;

            default:
                break;
        }
    }
    return result;
}

struct SharedHandle {
    HandleType handle{ INVALID_HANDLE_VALUE };
};

struct Memory : public SharedHandle {
    GLuint memory{ 0 };
    void import(HandleType handle, GLuint64 size, bool dedicated = false) {
        this->handle = handle;
        // Import memory
        glCreateMemoryObjectsEXT(1, &memory);
        if (dedicated) {
            static const GLint DEDICATED_FLAG = GL_TRUE;
            glMemoryObjectParameterivEXT(memory, GL_DEDICATED_MEMORY_OBJECT_EXT, &DEDICATED_FLAG);
        }
        // Platform specific import.  On non-Win32 systems use glImportMemoryFdEXT instead
#if WIN32
        glImportMemoryWin32HandleEXT(memory, size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle);
#else
        glImportMemoryFdEXT(memory, size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle);
#endif
    }
    void destroy() {
        glDeleteMemoryObjectsEXT(1, &memory);
        memory = 0;
    }
};

struct Texture : public Memory {
    GLuint texture{ 0 };
    GLenum srcLayout{ GL_LAYOUT_GENERAL_EXT };
    GLenum dstLayout{ GL_LAYOUT_GENERAL_EXT };

    void import(HandleType handle, GLuint64 size, const uvec2& dimensions, vk::ImageTiling tiling, bool dedicated = false) {
        Memory::import(handle, size, dedicated);

        GLuint glTiling = tiling == vk::ImageTiling::eLinear ? GL_LINEAR_TILING_EXT : GL_OPTIMAL_TILING_EXT;
        // Use the imported memory as backing for the OpenGL texture.  The internalFormat, dimensions
        // and mip count should match the ones used by Vulkan to create the image and determine it's memory
        // allocation.
        glCreateTextures(GL_TEXTURE_2D, 1, &texture);
        glTextureParameteri(texture, GL_TEXTURE_TILING_EXT, glTiling);
        glTextureStorageMem2DEXT(texture, 1, GL_RGBA8, dimensions.x, dimensions.y, memory, 0);
    }

    void destroy() {
        glDeleteTextures(1, &texture);
        texture = 0;
        Memory::destroy();
    }
};

struct Buffer : public Memory {
    GLuint buffer{ 0 };

    void import(HandleType handle, GLuint64 size, bool dedicated = false) {
        Memory::import(handle, size, dedicated);
        glCreateBuffers(1, &buffer);
        glNamedBufferStorageMemEXT(buffer, 0, memory, 0);
    }

    void destroy() {
        glDeleteBuffers(1, &buffer);
        buffer = 0;
        Memory::destroy();
    }
};

struct Semaphore : public SharedHandle {
    GLuint semaphore{ 0 };
    void import(HandleType handle) {
        this->handle = handle;
        // Import semaphores
        glGenSemaphoresEXT(1, &semaphore);
        // Platform specific import.  On non-Win32 systems use glImportSemaphoreFdEXT instead
#if WIN32
        glImportSemaphoreWin32HandleEXT(semaphore, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle);
#else
        glImportSemaphoreFdEXT(semaphore, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle)
#endif
    }

    void wait(const vk::ArrayProxy<const GLuint>& buffers, const vk::ArrayProxy<const GLuint>& textures, const vk::ArrayProxy<const GLenum>& layouts) const {
        GLuint textureCount = textures.size();
        if (layouts.size() != textureCount) {
            throw std::runtime_error("Layouts count must match textures count");
        }
        GLuint bufferCount = buffers.size();
        const GLuint* buffersPtr = nullptr;
        if (bufferCount != 0) {
            buffersPtr = buffers.data();
        }
        const GLuint* texturesPtr = nullptr;
        const GLenum* layoutsPtr = nullptr;
        if (textureCount != 0) {
            texturesPtr = textures.data();
            layoutsPtr = layouts.data();
        }
        glWaitSemaphoreEXT(semaphore, bufferCount, buffersPtr, textureCount, texturesPtr, layoutsPtr);
    }

    void wait(const vk::ArrayProxy<const Buffer>& buffers, const vk::ArrayProxy<const Texture>& textures, const vk::ArrayProxy<const GLenum>& layouts) const {
        std::vector<GLuint> textureIds;
        std::transform(textures.begin(), textures.end(), std::back_inserter(textureIds), [](const Texture& t) { return t.texture; });
        std::vector<GLuint> bufferIds;
        std::transform(buffers.begin(), buffers.end(), std::back_inserter(bufferIds), [](const Buffer& b) { return b.buffer; });
        wait(bufferIds, textureIds, layouts);
    }

    void signal(const vk::ArrayProxy<const GLuint>& buffers, const vk::ArrayProxy<const GLuint>& textures, const vk::ArrayProxy<const GLenum>& layouts) {
        GLuint textureCount = textures.size();
        if (layouts.size() != textureCount) {
            throw std::runtime_error("Layouts count must match textures count");
        }
        GLuint bufferCount = buffers.size();
        const GLuint* buffersPtr = nullptr;
        if (bufferCount != 0) {
            buffersPtr = buffers.data();
        }
        const GLuint* texturesPtr = nullptr;
        const GLenum* layoutsPtr = nullptr;
        if (textureCount != 0) {
            texturesPtr = textures.data();
            layoutsPtr = layouts.data();
        }
        glSignalSemaphoreEXT(semaphore, bufferCount, buffersPtr, textureCount, texturesPtr, layoutsPtr);
    }

    void signal(const vk::ArrayProxy<const Buffer>& buffers, const vk::ArrayProxy<const Texture>& textures, const vk::ArrayProxy<const GLenum>& layouts) {
        std::vector<GLuint> textureIds;
        std::transform(textures.begin(), textures.end(), std::back_inserter(textureIds), [](const Texture& t) { return t.texture; });
        std::vector<GLuint> bufferIds;
        std::transform(buffers.begin(), buffers.end(), std::back_inserter(bufferIds), [](const Buffer& b) { return b.buffer; });
        signal(bufferIds, textureIds, layouts);
    }

    void destroy() {
        glDeleteSemaphoresEXT(1, &semaphore);
        semaphore = 0;
    }
};

}}  // namespace gl::import

class TextureGenerator {
public:
    static const std::string VERTEX_SHADER;
    static const std::string FRAGMENT_SHADER;

    void init(const glm::uvec2& dimensions) {
        glfw::Window::init();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);

        // Window doesn't need to be large, it only exists to give us a GL context
        window.createWindow(dimensions);
        window.makeCurrent();

        startTime = glfwGetTime();

        gl::init();
        gl::setupDebugLogging();
#if !SHOW_GL_WINDOW
        window.showWindow(false);
#endif

        // The remaining initialization code is all standard OpenGL
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glCreateFramebuffers(1, &fbo);

        glCreateTextures(GL_TEXTURE_2D, 1, &color);
        glTextureStorage2D(color, 1, GL_RGBA8, dimensions.x, dimensions.y);
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color, 0);

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        program = gl::buildProgram(VERTEX_SHADER, FRAGMENT_SHADER);
    }

    void destroy() {
        glBindVertexArray(0);
        glUseProgram(0);

        semaphores[READY].destroy();
        semaphores[COMPLETE].destroy();
        texture.destroy();

        glDeleteFramebuffers(1, &fbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
        glFlush();
        glFinish();
        window.destroyWindow();
    }

    void render(const glm::uvec2& dimensions) {
        // Basic GL rendering code to render animated noise to a texture
        auto time = (float)(glfwGetTime() - startTime);
        glUseProgram(program);
        glProgramUniform1f(program, 1, time);
        glProgramUniform3f(program, 0, (float)dimensions.x, (float)dimensions.y, 0.0f);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glViewport(0, 0, dimensions.x, dimensions.y);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);


        // Wait (on the GPU side) for the Vulkan semaphore to be signaled
        // Tell OpenGL what Vulkan layout to expect the image to be in at
        // signal time, so that it can internally transition to the appropriate
        // GL state
        semaphores[READY].wait(nullptr, texture, GL_LAYOUT_COLOR_ATTACHMENT_EXT);
        // Once the semaphore is signaled, copy the GL texture to the shared texture
        glCopyImageSubData(color, GL_TEXTURE_2D, 0, 0, 0, 0, texture.texture, GL_TEXTURE_2D, 0, 0, 0, 0, dimensions.x, dimensions.y, 1);
        // Once the copy is complete, signal Vulkan that the image can be used again
        semaphores[COMPLETE].signal(nullptr, texture, GL_LAYOUT_COLOR_ATTACHMENT_EXT);

        // When using synchronization across multiple GL context, or in this case
        // across OpenGL and another API, it's critical that an operation on a
        // synchronization object that will be waited on in another context or API
        // is flushed to the GL server.
        //
        // Failure to flush the operation can cause the GL driver to sit and wait for
        // sufficient additional commands in the buffer before it flushes automatically
        // but depending on how the waits and signals are structured, this may never
        // occur.
        glFlush();

#if SHOW_GL_WINDOW
        glBlitNamedFramebuffer(fbo, 0, 0, 0, dimensions.x, dimensions.y, 0, 0, dimensions.x, dimensions.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        window.present();
#endif
    }

    gl::import::Texture texture;
    gl::import::Semaphore semaphores[2];

private:
    GLuint fbo = 0;
    GLuint color = 0;
    GLuint vao = 0;
    GLuint program = 0;
    double startTime;
    glfw::Window window;
};

const std::string TextureGenerator::VERTEX_SHADER = R"SHADER(
#version 450 core

const vec4 VERTICES[] = vec4[](
    vec4(-1.0, -1.0, 0.0, 1.0), 
    vec4( 1.0, -1.0, 0.0, 1.0),    
    vec4(-1.0,  1.0, 0.0, 1.0),
    vec4( 1.0,  1.0, 0.0, 1.0)
);   

void main() { gl_Position = VERTICES[gl_VertexID]; }

)SHADER";

const std::string TextureGenerator::FRAGMENT_SHADER = R"SHADER(
#version 450 core

const vec4 iMouse = vec4(0.0); 

layout(location = 0) out vec4 outColor;

layout(location = 0) uniform vec3 iResolution;
layout(location = 1) uniform float iTime;

vec3 hash3( vec2 p )
{
    vec3 q = vec3( dot(p,vec2(127.1,311.7)), 
                   dot(p,vec2(269.5,183.3)), 
                   dot(p,vec2(419.2,371.9)) );
    return fract(sin(q)*43758.5453);
}

float iqnoise( in vec2 x, float u, float v )
{
    vec2 p = floor(x);
    vec2 f = fract(x);
        
    float k = 1.0+63.0*pow(1.0-v,4.0);
    
    float va = 0.0;
    float wt = 0.0;
    for( int j=-2; j<=2; j++ )
    for( int i=-2; i<=2; i++ )
    {
        vec2 g = vec2( float(i),float(j) );
        vec3 o = hash3( p + g )*vec3(u,u,1.0);
        vec2 r = g - f + o.xy;
        float d = dot(r,r);
        float ww = pow( 1.0-smoothstep(0.0,1.414,sqrt(d)), k );
        va += o.z*ww;
        wt += ww;
    }
    
    return va/wt;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xx;

    vec2 p = 0.5 - 0.5*sin( iTime*vec2(1.01,1.71) );
    
    if( iMouse.w>0.001 ) p = vec2(0.0,1.0) + vec2(1.0,-1.0)*iMouse.xy/iResolution.xy;
    
    p = p*p*(3.0-2.0*p);
    p = p*p*(3.0-2.0*p);
    p = p*p*(3.0-2.0*p);
    
    float f = iqnoise( 24.0*uv, p.x, p.y );
    
    fragColor = vec4( f, f, f, 1.0 );
}

void main() { mainImage(outColor, gl_FragCoord.xy); }

)SHADER";

// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
    float normal[3];
};

// The bulk of this example is the same as the existing texture example.
// However, instead of loading a texture from a file, it relies on an OpenGL
// shader to populate the texture.
class OpenGLInteropExample : public vkx::ExampleBase {
    using Parent = ExampleBase;
    static const uint32_t SHARED_TEXTURE_DIMENSION = 512;
    vk::DispatchLoaderDynamic dynamicLoader;

public:
    struct SharedResources {
        vks::Image image;
        bool dedicated{ false };
        vk::ImageTiling tiling = vk::ImageTiling::eLinear;
        std::array<vk::Semaphore, SEMAPHORE_COUNT> semaphores;
        vk::Device device;

        void init(const vks::Context& context, const vk::DispatchLoaderDynamic& dynamicLoader) {
            device = context.device;
            {
                {
                    vk::SemaphoreCreateInfo sci;
                    vk::ExportSemaphoreCreateInfo esci;
                    sci.pNext = &esci;
                    esci.handleTypes = semaphoreHandleType;
                    semaphores[READY] = device.createSemaphore(sci);
                    semaphores[COMPLETE] = device.createSemaphore(sci);
                }
            }

            // Prefer optimal if available
            auto supportedTiling = gl::import::getSupportedTiling();
            if (supportedTiling.end() != supportedTiling.find(vk::ImageTiling::eOptimal)) {
                tiling = vk::ImageTiling::eOptimal;
            }

            // Optimal works with nVidia (1070), but produces garbled results on my AMD RX 580
            // Linear produces non-garbled results on the AMD, but isn't supported on nVidia in combination
            // with the eColorAttachment usage flag.  Without the eColorAttachment usage flag, the nVidia
            // shared image will not properly act as a framebuffer target
            using vIU = vk::ImageUsageFlagBits;
            vk::PhysicalDeviceImageFormatInfo2 imageFormatInfo{
                vk::Format::eR8G8B8A8Unorm,
                vk::ImageType::e2D,
                tiling,
                vIU::eTransferSrc | vIU::eTransferDst | vIU::eColorAttachment | vIU::eStorage | vIU::eSampled,
            };

            {
                using QueryChain = vk::StructureChain<vk::PhysicalDeviceImageFormatInfo2, vk::PhysicalDeviceExternalImageFormatInfo>;
                vk::PhysicalDeviceExternalImageFormatInfo formatInfo{ memoryHandleType };

                QueryChain chain{ imageFormatInfo, formatInfo };
                using ResultChain = vk::StructureChain<vk::ImageFormatProperties2, vk::ExternalImageFormatProperties>;
                const auto& resolvedImageFormatInfo = chain.get<vk::PhysicalDeviceImageFormatInfo2>();
                ResultChain result =
                    context.physicalDevice.getImageFormatProperties2<vk::ImageFormatProperties2, vk::ExternalImageFormatProperties>(resolvedImageFormatInfo);
                vk::ImageFormatProperties2 imageFormatProperties;
                vk::ExternalImageFormatProperties externalImageFormatProperties;
                imageFormatProperties = result.get<vk::ImageFormatProperties2>();
                externalImageFormatProperties = result.get<vk::ExternalImageFormatProperties>();

                dedicated = true;
                if (externalImageFormatProperties.externalMemoryProperties.externalMemoryFeatures & vk::ExternalMemoryFeatureFlagBits::eDedicatedOnly) {
                    dedicated = true;
                }
            }

            {
                vk::ImageCreateInfo imageCreateInfo;
                imageCreateInfo.imageType = vk::ImageType::e2D;
                imageCreateInfo.format = imageFormatInfo.format;
                imageCreateInfo.tiling = imageFormatInfo.tiling;
                imageCreateInfo.mipLevels = 1;
                imageCreateInfo.arrayLayers = 1;
                imageCreateInfo.extent.depth = 1;
                imageCreateInfo.extent.width = SHARED_TEXTURE_DIMENSION;
                imageCreateInfo.extent.height = SHARED_TEXTURE_DIMENSION;
                imageCreateInfo.usage = imageFormatInfo.usage;
                image.image = device.createImage(imageCreateInfo);
                image.device = device;
                image.format = imageCreateInfo.format;
                image.extent = imageCreateInfo.extent;
            }

            {
                auto memReqs = device.getImageMemoryRequirements(image.image);
                vk::MemoryAllocateInfo memAllocInfo;
                memAllocInfo.allocationSize = image.allocSize = memReqs.size;
                memAllocInfo.memoryTypeIndex = context.getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

                // Always add the export info to the memory allocation chain
                vk::ExportMemoryAllocateInfo exportAllocInfo{ memoryHandleType };
                memAllocInfo.pNext = &exportAllocInfo;

                // Potentially add the dedicated memory allocation
                vk::MemoryDedicatedAllocateInfo dedicatedMemAllocInfo{ image.image };
                if (dedicated) {
                    exportAllocInfo.pNext = &dedicatedMemAllocInfo;
                }

                image.memory = device.allocateMemory(memAllocInfo);
                device.bindImageMemory(image.image, image.memory, 0);
            }

            // Move the image to it's target layout, and make sure the semaphore that GL will wait on is initially signalled.
            context.withPrimaryCommandBuffer(
                [&](const vk::CommandBuffer& cmdBuffer) {
                    context.setImageLayout(cmdBuffer, image.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
                },
                semaphores[READY]);
        }

        void destroy() {
            image.destroy();
            device.destroy(semaphores[READY]);
            device.destroy(semaphores[COMPLETE]);
        }
    } shared;

    TextureGenerator texGenerator;

    struct Geometry {
        uint32_t count{ 0 };
        vks::Buffer indices;
        vks::Buffer vertices;
    } geometry;

    vks::Buffer uniformDataVS;

    struct UboVS {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec4 viewPos;
        float lodBias = 0.0f;
    } uboVS;

    struct {
        vk::Pipeline solid;
    } pipelines;

    vks::Image texture;
    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    OpenGLInteropExample() {
        enableVsync = true;
        camera.setRotation({ 0.0f, 15.0f, 0.0f });
        camera.dolly(-2.5f);
        title = "Vulkan Example - Texturing";

        context.requireExtensions({ VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME });

        context.requireDeviceExtensions({
#if defined(WIN32)
            VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#else
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#endif
        });
    }

    ~OpenGLInteropExample() {
        shared.destroy();

        device.destroyPipeline(pipelines.solid);
        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        geometry.vertices.destroy();
        geometry.indices.destroy();

        device.destroyBuffer(uniformDataVS.buffer);
        device.freeMemory(uniformDataVS.memory);
    }

    void buildExportableImage() {
        dynamicLoader.init(context.instance, device);
        texGenerator.init({ SHARED_TEXTURE_DIMENSION, SHARED_TEXTURE_DIMENSION });
        shared.init(context, dynamicLoader);

        {
            auto handle = device.getMemoryWin32HandleKHR({ shared.image.memory, memoryHandleType }, dynamicLoader);
            texGenerator.texture.import(handle, shared.image.allocSize, { SHARED_TEXTURE_DIMENSION, SHARED_TEXTURE_DIMENSION }, shared.tiling,
                                        shared.dedicated);
        }

        {
            auto handle = device.getSemaphoreWin32HandleKHR({ shared.semaphores[READY], semaphoreHandleType }, dynamicLoader);
            texGenerator.semaphores[READY].import(handle);
        }

        {
            auto handle = device.getSemaphoreWin32HandleKHR({ shared.semaphores[COMPLETE], semaphoreHandleType }, dynamicLoader);
            texGenerator.semaphores[COMPLETE].import(handle);
        }

        {
            vk::ImageCreateInfo imageCreateInfo;
            imageCreateInfo.imageType = vk::ImageType::e2D;
            imageCreateInfo.format = vk::Format::eR8G8B8A8Unorm;
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.extent.depth = 1;
            imageCreateInfo.extent.width = SHARED_TEXTURE_DIMENSION;
            imageCreateInfo.extent.height = SHARED_TEXTURE_DIMENSION;
            imageCreateInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            texture = context.createImage(imageCreateInfo);
        }

        {
            // Create sampler
            vk::SamplerCreateInfo samplerCreateInfo;
            samplerCreateInfo.magFilter = vk::Filter::eLinear;
            samplerCreateInfo.minFilter = vk::Filter::eLinear;
            samplerCreateInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
            // Max level-of-detail should match mip level count
            samplerCreateInfo.maxLod = (float)1;
            // Only enable anisotropic filtering if enabled on the device
            samplerCreateInfo.maxAnisotropy = context.deviceFeatures.samplerAnisotropy ? context.deviceProperties.limits.maxSamplerAnisotropy : 1.0f;
            samplerCreateInfo.anisotropyEnable = context.deviceFeatures.samplerAnisotropy;
            samplerCreateInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
            texture.sampler = device.createSampler(samplerCreateInfo);
        }

        {
            // Create image view
            vk::ImageViewCreateInfo viewCreateInfo;
            viewCreateInfo.viewType = vk::ImageViewType::e2D;
            viewCreateInfo.image = texture.image;
            viewCreateInfo.format = texture.format;
            viewCreateInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
            texture.view = context.device.createImageView(viewCreateInfo);
        }

        context.setImageLayout(texture.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        addRenderWaitSemaphore(shared.semaphores[COMPLETE]);
        renderSignalSemaphores.push_back(shared.semaphores[READY]);
    }

    void updateCommandBufferPreDraw(const vk::CommandBuffer& cmdBuffer) override {
        context.setImageLayout(cmdBuffer, shared.image.image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal);
        context.setImageLayout(cmdBuffer, texture.image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal);
        vk::ImageCopy imageCopy{ vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                                 {},
                                 vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                                 {},
                                 shared.image.extent };
        cmdBuffer.copyImage(shared.image.image, vk::ImageLayout::eTransferSrcOptimal, texture.image, vk::ImageLayout::eTransferDstOptimal, imageCopy);
        context.setImageLayout(cmdBuffer, texture.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
        context.setImageLayout(cmdBuffer, shared.image.image, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eColorAttachmentOptimal);
    }

    void updateCommandBufferPostDraw(const vk::CommandBuffer& cmdBuffer) override {}

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setViewport(0, vks::util::viewport(size));
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
        vk::DeviceSize offsets = 0;
        cmdBuffer.bindVertexBuffers(0, geometry.vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(geometry.indices.buffer, 0, vk::IndexType::eUint32);

        cmdBuffer.drawIndexed(geometry.count, 1, 0, 0, 0);
    }

    void generateQuad() {
        // Setup vertices for a single uv-mapped quad
#define DIM 1.0f
#define NORMAL { 0.0f, 0.0f, 1.0f }
        std::vector<Vertex> vertexBuffer = { { { DIM, DIM, 0.0f }, { 1.0f, 1.0f }, NORMAL },
                                             { { -DIM, DIM, 0.0f }, { 0.0f, 1.0f }, NORMAL },
                                             { { -DIM, -DIM, 0.0f }, { 0.0f, 0.0f }, NORMAL },
                                             { { DIM, -DIM, 0.0f }, { 1.0f, 0.0f }, NORMAL } };
#undef DIM
#undef NORMAL
        geometry.vertices = context.stageToDeviceBuffer<Vertex>(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0, 1, 2, 2, 3, 0 };
        geometry.count = (uint32_t)indexBuffer.size();
        geometry.indices = context.stageToDeviceBuffer<uint32_t>(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
    }

    void setupDescriptorPool() {
        // Example uses one ubo and one image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 1 },
        };
        descriptorPool = device.createDescriptorPool({ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Vertex shader uniform buffer
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader image sampler
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &descriptorSetLayout })[0];
        // vk::Image descriptor for the color map texture
        vk::DescriptorImageInfo texDescriptor{ texture.sampler, texture.view, vk::ImageLayout::eShaderReadOnlyOptimal };
        device.updateDescriptorSets(
            {
                // Binding 0 : Vertex shader uniform buffer
                vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformDataVS.descriptor },
                // Binding 1 : Fragment shader texture sampler
                vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptor },
            },
            nullptr);
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayout, renderPass };
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelineBuilder.vertexInputState.bindingDescriptions = { { 0, sizeof(Vertex), vk::VertexInputRate::eVertex } };
        pipelineBuilder.vertexInputState.attributeDescriptions = {
            { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos) },
            { 1, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv) },
            { 2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal) },
        };
        pipelineBuilder.loadShader(getAssetPath() + "shaders/texture/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/texture/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines.solid = pipelineBuilder.create(context.pipelineCache);
    }

    void prepareUniformBuffers() {
        uniformDataVS = context.createUniformBuffer(uboVS);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uboVS.projection = camera.matrices.perspective;
        glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, camera.position.z));
        uboVS.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(camera.position.x, camera.position.y, 0));
        uboVS.model = uboVS.model * glm::inverse(camera.matrices.skyboxView);
        uboVS.viewPos = glm::vec4(0.0f, 0.0f, -camera.position.z, 0.0f);
        uniformDataVS.copy(uboVS);
    }

    void prepare() override {
        Parent::prepare();
        generateQuad();
        prepareUniformBuffers();
        buildExportableImage();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    void viewChanged() override { updateUniformBuffers(); }

    void draw() override {
        texGenerator.render({ SHARED_TEXTURE_DIMENSION, SHARED_TEXTURE_DIMENSION });

        prepareFrame();
        drawCurrentCommandBuffer();
        submitFrame();
    }
};
#else
class OpenGLInteropExample {
public:
    void run() {}
};
#endif

RUN_EXAMPLE(OpenGLInteropExample)
