/*
* Vulkan Example - OpenGL interoperability example
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>
#include <vks/texture.hpp>

struct ShareHandles {
    HANDLE memory{ INVALID_HANDLE_VALUE };
    // FIXME properly use semaphores for GL/VK sync
    /*
    HANDLE vkSemaphore{ INVALID_HANDLE_VALUE };
    HANDLE glSemaphore{ INVALID_HANDLE_VALUE };
    */
};

class TextureGenerator {
public:
    static const std::string VERTEX_SHADER;
    static const std::string FRAGMENT_SHADER;

    void init(ShareHandles& handles, uint64_t memorySize) {
        glfw::Window::init();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);

        window.createWindow(size);
        window.makeCurrent();

        gl::init();
        gl::setupDebugLogging();

        window.showWindow(false);
        program = gl::buildProgram(VERTEX_SHADER, FRAGMENT_SHADER);
        startTime = glfwGetTime();

        glDisable(GL_DEPTH_TEST);
        glClearColor(1, 0, 0, 1);

        // FIXME properly use semaphores for GL/VK sync
/*
        glGenSemaphoresEXT(1, &vkSem);
        glGenSemaphoresEXT(1, &glSem);
        glImportSemaphoreWin32HandleEXT(vkSem, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handles.vkSemaphore);
        glImportSemaphoreWin32HandleEXT(glSem, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handles.glSemaphore);
*/
        glCreateMemoryObjectsEXT(1, &mem);
        glImportMemoryWin32HandleEXT(mem, memorySize, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handles.memory);
        glCreateTextures(GL_TEXTURE_2D, 1, &color);
        glTextureStorageMem2DEXT(color, 1, GL_RGBA8, size.x, size.y, mem, 0 );

        glCreateFramebuffers(1, &fbo);
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color, 0);

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glUseProgram(program);
        glProgramUniform3f(program, 0, (float)size.x, (float)size.y, 0.0f);

        // Now check for completness
        auto fboStatus = glCheckNamedFramebufferStatus(fbo, GL_DRAW_FRAMEBUFFER);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glViewport(0, 0, 512, 512);
    }

    void render() {
        GLenum srcLayout = GL_LAYOUT_COLOR_ATTACHMENT_EXT;
        GLenum dstLayout = GL_LAYOUT_SHADER_READ_ONLY_EXT;
        glProgramUniform1f(program, 1, (float)(glfwGetTime() - startTime));
        // FIXME properly use semaphores for GL/VK sync
        //glWaitSemaphoreEXT(vkSem, 0, nullptr, 1, &color, &srcLayout);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        //glSignalSemaphoreEXT(glSem, 0, nullptr, 1, &color, &dstLayout);
        glFlush();
        glFinish();
    }

private:
    uvec2 size{ 512, 512 };
    //GLuint vkSem{ 0 }, glSem{ 0 };
    GLuint color = 0;
    GLuint fbo = 0;
    GLuint vao = 0;
    GLuint program = 0;
    GLuint mem = 0;
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

class TextureExample : public vkx::ExampleBase {
    using Parent = ExampleBase;
public:

    ShareHandles sharedHandles;

    struct SharedResources {
        vks::Image texture;
        vk::Semaphore vkSemaphore;
        vk::Semaphore glSemaphore;
    } shared;

    TextureGenerator texGenerator;


    struct {
        uint32_t count{ 0 };
        vks::Buffer indices;
        vks::Buffer vertices;
    } geometry;

    struct {
        int count;
        vk::Buffer buffer;
        vk::DeviceMemory memory;
    } indices;

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

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    TextureExample() {
        camera.setRotation({ 0.0f, 15.0f, 0.0f });
        camera.dolly(-2.5f);
        title = "Vulkan Example - Texturing";

        context.requireExtensions({
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME
        });

        context.requireDeviceExtensions({
            VK_KHR_MAINTENANCE1_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME
        });
    }

    ~TextureExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        // Clean up texture resources
        shared.texture.destroy();

        device.destroyPipeline(pipelines.solid);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        geometry.vertices.destroy();
        geometry.indices.destroy();

        device.destroyBuffer(uniformDataVS.buffer);
        device.freeMemory(uniformDataVS.memory);
    }

    void buildExportableImage() {
        vk::DispatchLoaderDynamic dynamicLoader{ context.instance, device };
        // FIXME properly use semaphores for GL/VK sync
        /*
        {
            {
                vk::SemaphoreCreateInfo sci;
                vk::ExportSemaphoreCreateInfo esci;
                sci.pNext = &esci;
                esci.handleTypes = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
                shared.vkSemaphore = device.createSemaphore(sci);
                shared.glSemaphore = device.createSemaphore(sci);
            }

            vk::SemaphoreGetWin32HandleInfoKHR shci;
            shci.handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
            shci.semaphore = shared.vkSemaphore;
            shared.handles.vkSemaphore = device.getSemaphoreWin32HandleKHR(shci, dynamicLoader);
            shci.semaphore = shared.glSemaphore;
            shared.handles.glSemaphore = device.getSemaphoreWin32HandleKHR(shci, dynamicLoader);
        }
        */

        auto& texture = shared.texture;
        {
            vk::ImageCreateInfo imageCreateInfo;
            imageCreateInfo.imageType = vk::ImageType::e2D;
            imageCreateInfo.format = vk::Format::eR8G8B8A8Unorm;
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.extent.width = 512;
            imageCreateInfo.extent.height = 512;
            imageCreateInfo.extent.depth = 1;
            imageCreateInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
            texture.image = device.createImage(imageCreateInfo);
            texture.device = device;
            texture.format = imageCreateInfo.format;
            texture.extent = imageCreateInfo.extent;
        }

        {
            vk::MemoryRequirements memReqs = device.getImageMemoryRequirements(texture.image);
            vk::MemoryAllocateInfo memAllocInfo;
            vk::ExportMemoryAllocateInfo exportAllocInfo{ vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 };
            memAllocInfo.pNext = &exportAllocInfo;
            memAllocInfo.allocationSize = texture.allocSize = memReqs.size;
            memAllocInfo.memoryTypeIndex = context.getMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            texture.memory = device.allocateMemory(memAllocInfo);
            device.bindImageMemory(texture.image, texture.memory, 0);


            //auto test1 = device.getProcAddr("vkGetMemoryWin32HandleKHR", );
            //std::cout << test1 << std::endl;
            sharedHandles.memory = device.getMemoryWin32HandleKHR({ texture.memory, vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 }, dynamicLoader);
        }

        {
            // Create sampler
            vk::SamplerCreateInfo samplerCreateInfo;
            samplerCreateInfo.magFilter = vk::Filter::eLinear;
            samplerCreateInfo.minFilter = vk::Filter::eLinear;
            samplerCreateInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
            // Max level-of-detail should match mip level count
            samplerCreateInfo.maxLod = (float)1;
            // Only enable anisotropic filtering if enabled on the devicec
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

        texGenerator.init(sharedHandles, texture.allocSize);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setViewport(0, vks::util::viewport(size));
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
        vk::DeviceSize offsets = 0;
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, geometry.vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(geometry.indices.buffer, 0, vk::IndexType::eUint32);

        cmdBuffer.drawIndexed(geometry.count, 1, 0, 0, 0);
    }

    void generateQuad() {
        // Setup vertices for a single uv-mapped quad
#define DIM 1.0f
#define NORMAL { 0.0f, 0.0f, 1.0f }
        std::vector<Vertex> vertexBuffer =
        {
            { {  DIM,  DIM, 0.0f }, { 1.0f, 1.0f }, NORMAL },
            { { -DIM,  DIM, 0.0f }, { 0.0f, 1.0f }, NORMAL },
            { { -DIM, -DIM, 0.0f }, { 0.0f, 0.0f }, NORMAL },
            { {  DIM, -DIM, 0.0f }, { 1.0f, 0.0f }, NORMAL }
        };
#undef DIM
#undef NORMAL
        geometry.vertices = context.stageToDeviceBuffer<Vertex>(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
        geometry.count = (uint32_t)indexBuffer.size();
        geometry.indices = context.stageToDeviceBuffer<uint32_t>(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);

    }

    void setupDescriptorPool() {
        // Example uses one ubo and one image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 1 },
        };
        descriptorPool = device.createDescriptorPool({ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            // Binding 0 : Vertex shader uniform buffer
            vk::DescriptorSetLayoutBinding{ 0,  vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader image sampler
            vk::DescriptorSetLayoutBinding{ 1,  vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &descriptorSetLayout })[0];
        // vk::Image descriptor for the color map texture
        vk::DescriptorImageInfo texDescriptor{ shared.texture.sampler, shared.texture.view, vk::ImageLayout::eShaderReadOnlyOptimal };
        device.updateDescriptorSets({
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformDataVS.descriptor },
            // Binding 1 : Fragment shader texture sampler
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptor },
            }, {});
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayout, renderPass };
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelineBuilder.vertexInputState.bindingDescriptions = {
            { VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex }
        };

        pipelineBuilder.vertexInputState.attributeDescriptions = {
            { 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, 0 },
            { 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, sizeof(float) * 3 },
            { 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, sizeof(float) * 5 },
        };
        pipelineBuilder.loadShader(getAssetPath() + "shaders/texture/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/texture/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines.solid = pipelineBuilder.create(context.pipelineCache);;
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformDataVS = context.createUniformBuffer(uboVS);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader
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

    void viewChanged() override {
        updateUniformBuffers();
    }

    void changeLodBias(float delta) {
        uboVS.lodBias += delta;
        if (uboVS.lodBias < 0.0f) {
            uboVS.lodBias = 0.0f;
        }
        if (uboVS.lodBias > 8.0f) {
            uboVS.lodBias = 8.0f;
        }
        updateUniformBuffers();
    }

    void draw() override {
        prepareFrame();
        context.setImageLayout(shared.texture.image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
        texGenerator.render();
        context.setImageLayout(shared.texture.image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
        drawCurrentCommandBuffer();
        submitFrame();
    }


#if !defined(__ANDROID__)
    void keyPressed(uint32_t keyCode) override {
        switch (keyCode) {
        case KEY_KPADD:
            changeLodBias(0.1f);
            break;
        case KEY_KPSUB:
            changeLodBias(-0.1f);
            break;
        }
    }
#endif
};

RUN_EXAMPLE(TextureExample)


