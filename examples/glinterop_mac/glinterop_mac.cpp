/*
* Vulkan Example - OpenGL interoperability example
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>

#if __APPLE__

#include <vks/texture.hpp>
#include <unordered_map>
#include <macos/macos.h>
#include <MoltenVK/vk_mvk_moltenvk.h>


#define SHOW_GL_WINDOW 1

// Indices into the semaphores
#define READY 0
#define COMPLETE 1
#define SEMAPHORE_COUNT 2

class TextureGenerator {
public:
    static const std::string VERTEX_SHADER;
    static const std::string FRAGMENT_SHADER;
    static void glfwErrorCallback(int, const char* message) {
        std::cerr << message << std::endl;
    }

    void init(const glm::uvec2& dimensions) {
        if (!glfw::Window::init()) {
            throw std::runtime_error("Could not initialize GLFW");
        }
        glfwSetErrorCallback(glfwErrorCallback);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

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
        glGenFramebuffers(1, &fbo);

        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexStorage2D(GL_TEXTURE_RECTANGLE, 1, GL_RGBA8, dimensions.x, dimensions.y);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        program = gl::buildProgram(VERTEX_SHADER, FRAGMENT_SHADER);
        locations.rez = glGetUniformLocation(program, "iResolution");
        locations.time = glGetUniformLocation(program, "iTime");
        
    }

    void destroy() {
        glBindVertexArray(0);
        glUseProgram(0);

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
        glProgramUniform1f(program, locations.time, time);
        glProgramUniform3f(program, locations.rez, (float)dimensions.x, (float)dimensions.y, 0.0f);
//        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glViewport(0, 0, dimensions.x, dimensions.y);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
//        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);


        // Wait (on the GPU side) for the Vulkan semaphore to be signaled
        // Tell OpenGL what Vulkan layout to expect the image to be in at
        // signal time, so that it can internally transition to the appropriate
        // GL state
        //semaphores[READY].wait(nullptr, texture, GL_LAYOUT_COLOR_ATTACHMENT_EXT);
        // Once the semaphore is signaled, copy the GL texture to the shared texture
        // glCopyImageSubData(color, GL_TEXTURE_2D, 0, 0, 0, 0, texture.texture, GL_TEXTURE_2D, 0, 0, 0, 0, dimensions.x, dimensions.y, 1);
        // Once the copy is complete, signal Vulkan that the image can be used again
        //semaphores[COMPLETE].signal(nullptr, texture, GL_LAYOUT_COLOR_ATTACHMENT_EXT);

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
//        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
//        glBlitFramebuffer(0, 0, dimensions.x, dimensions.y, 0, 0, dimensions.x, dimensions.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
//        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        window.present();
#endif
    }
    

private:
    GLuint fbo = 0;
    GLuint color = 0;
    GLuint vao = 0;
    GLuint program = 0;
    struct Locations {
        GLint rez{ -1 };
        GLint time{ -1 };
    } locations;
    double startTime;
    glfw::Window window;
};

const std::string TextureGenerator::VERTEX_SHADER = R"SHADER(
#version 410 core

const vec4 VERTICES[] = vec4[](
    vec4(-1.0, -1.0, 0.0, 1.0), 
    vec4( 1.0, -1.0, 0.0, 1.0),    
    vec4(-1.0,  1.0, 0.0, 1.0),
    vec4( 1.0,  1.0, 0.0, 1.0)
);   

void main() { gl_Position = VERTICES[gl_VertexID]; }

)SHADER";

const std::string TextureGenerator::FRAGMENT_SHADER = R"SHADER(
#version 410 core

const vec4 iMouse = vec4(0.0); 

layout(location = 0) out vec4 outColor;

uniform vec3 iResolution;
uniform float iTime;

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
    static const uint32_t SHARED_TEXTURE_DIMENSION = 256;
    vk::DispatchLoaderDynamic dynamicLoader;

public:
    PFN_vkGetMoltenVKConfigurationMVK vkGetMoltenVKConfigurationMVK{ nullptr };
    PFN_vkSetMoltenVKConfigurationMVK vkSetMoltenVKConfigurationMVK{ nullptr };

    TextureGenerator texGenerator;
    vks::gl::SharedTexture::Pointer sharedTexture;

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
    }

    ~OpenGLInteropExample() {
        sharedTexture.reset();
        
        device.destroyPipeline(pipelines.solid);
        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        geometry.vertices.destroy();
        geometry.indices.destroy();

        device.destroyBuffer(uniformDataVS.buffer);
        device.freeMemory(uniformDataVS.memory);
    }

    void buildExportableImage() {
        vkGetMoltenVKConfigurationMVK = (PFN_vkGetMoltenVKConfigurationMVK)context.device.getProcAddr("vkGetMoltenVKConfigurationMVK");
        vkSetMoltenVKConfigurationMVK = (PFN_vkSetMoltenVKConfigurationMVK)context.device.getProcAddr("vkSetMoltenVKConfigurationMVK");

        MVKConfiguration mvkConfig{};
        vkGetMoltenVKConfigurationMVK(context.device, &mvkConfig);
        mvkConfig.synchronousQueueSubmits = VK_TRUE;
        vkSetMoltenVKConfigurationMVK(context.device, &mvkConfig);
        dynamicLoader.init(context.instance, device);
        texGenerator.init({ SHARED_TEXTURE_DIMENSION, SHARED_TEXTURE_DIMENSION });
        sharedTexture = vks::gl::SharedTexture::create(context, { SHARED_TEXTURE_DIMENSION, SHARED_TEXTURE_DIMENSION });

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
    }

    void updateCommandBufferPreDraw(const vk::CommandBuffer& cmdBuffer) override {
        context.setImageLayout(cmdBuffer, sharedTexture->vkImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal);
        context.setImageLayout(cmdBuffer, texture.image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal);
        vk::ImageCopy imageCopy{ vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                                 {},
                                 vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                                 {},
                                 vk::Extent3D{ SHARED_TEXTURE_DIMENSION, SHARED_TEXTURE_DIMENSION, 1} };
        //cmdBuffer.copyImage(sharedTexture->vkImage, vk::ImageLayout::eTransferSrcOptimal, texture.image, vk::ImageLayout::eTransferDstOptimal, imageCopy);
        context.setImageLayout(cmdBuffer, texture.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
        context.setImageLayout(cmdBuffer, sharedTexture->vkImage, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eTransferDstOptimal);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setViewport(0, vks::util::viewport(size));
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
//        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
        vk::DeviceSize offsets = 0;
//        cmdBuffer.bindVertexBuffers(0, geometry.vertices.buffer, offsets);
//        cmdBuffer.bindIndexBuffer(geometry.indices.buffer, 0, vk::IndexType::eUint32);
//        cmdBuffer.drawIndexed(geometry.count, 1, 0, 0, 0);
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
