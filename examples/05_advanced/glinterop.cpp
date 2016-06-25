#include <GL/glew.h>
#include "vulkanOffscreenExampleBase.hpp"

namespace gl {
    void DebugCallbackHandler(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *msg, GLvoid* data) {
        OutputDebugStringA(msg);
        std::cout << "debug call: " << msg << std::endl;
    }

    const std::set<std::string>& getExtensions() {
        static std::once_flag once;
        static std::set<std::string> extensions;
        std::call_once(once, [&] {
            GLint n;
            glGetIntegerv(GL_NUM_EXTENSIONS, &n);
            for (GLint i = 0; i < n; i++) {
                const char* extension = (const char*)glGetStringi(GL_EXTENSIONS, i);
                extensions.insert(extension);
            }
        });
        return extensions;
    }

    namespace nv {
        namespace vk {
            typedef void (GLAPIENTRY * PFN_glWaitVkSemaphoreNV) (GLuint64 vkSemaphore);
            typedef void (GLAPIENTRY * PFN_glSignalVkSemaphoreNV) (GLuint64 vkSemaphore);
            typedef void (GLAPIENTRY * PFN_glSignalVkFenceNV) (GLuint64 vkFence);
            typedef void (GLAPIENTRY * PFN_glDrawVkImageNV) (GLuint64 vkImage, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1, GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1);

            PFN_glDrawVkImageNV __glDrawVkImageNV = nullptr;
            PFN_glWaitVkSemaphoreNV __glWaitVkSemaphoreNV = nullptr;
            PFN_glSignalVkSemaphoreNV __glSignalVkSemaphoreNV = nullptr;

            void init() {
                // Ensure the extension is available
                if (!getExtensions().count("GL_NV_draw_vulkan_image")) {
                    throw std::runtime_error("GL_NV_draw_vulkan_image not supported");
                }

                __glDrawVkImageNV = (PFN_glDrawVkImageNV)wglGetProcAddress("glDrawVkImageNV");
                __glWaitVkSemaphoreNV = (PFN_glWaitVkSemaphoreNV)wglGetProcAddress("glWaitVkSemaphoreNV");
                __glSignalVkSemaphoreNV = (PFN_glSignalVkSemaphoreNV)wglGetProcAddress("glSignalVkSemaphoreNV");
                if (nullptr == __glDrawVkImageNV || nullptr == __glWaitVkSemaphoreNV || nullptr == __glSignalVkSemaphoreNV) {
                    throw std::runtime_error("Could not load required extension");
                }
            }
            void WaitSemaphore(const ::vk::Semaphore& semaphore) {
                __glWaitVkSemaphoreNV((GLuint64)(VkSemaphore)semaphore);
            }
            void SignalSemaphore(const ::vk::Semaphore& semaphore) {
                __glSignalVkSemaphoreNV((GLuint64)(VkSemaphore)semaphore);
            }
            void DrawVkImage(const ::vk::Image& image, GLuint sampler, const vec2& origin, const vec2& size, float z = 0, const vec2& tex1 = vec2(0, 1), const vec2& tex2 = vec2(1, 0)) {
                __glDrawVkImageNV((GLuint64)(VkImage)(image), 0, origin.x, origin.y, size.x, size.y, z, tex1.x, tex1.y, tex2.x, tex2.y);
            }
        }
    }
}


// Texture properties
#define TEX_DIM 512
#define TEX_FORMAT  vk::Format::eR8G8B8A8Unorm

// Vertex layout for this example
std::vector<vkx::VertexLayout> vertexLayout =
{
    vkx::VertexLayout::VERTEX_LAYOUT_POSITION,
    vkx::VertexLayout::VERTEX_LAYOUT_UV,
    vkx::VertexLayout::VERTEX_LAYOUT_COLOR,
    vkx::VertexLayout::VERTEX_LAYOUT_NORMAL
};

class VulkanExample : public vkx::OffscreenExampleBase {
public:
    struct {
        vkx::Texture colorMap;
    } textures;

    struct {
        vkx::MeshBuffer example;
        vkx::MeshBuffer quad;
        vkx::MeshBuffer plane;
    } meshes;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        vkx::UniformData vsShared;
        vkx::UniformData vsMirror;
        vkx::UniformData vsOffScreen;
    } uniformData;

    struct UBO {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    };

    struct {
        UBO vsShared;
    } ubos;

    struct {
        vk::Pipeline shaded;
        vk::Pipeline mirror;
    } pipelines;

    struct {
        vk::PipelineLayout quad;
        vk::PipelineLayout offscreen;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet offscreen;
        vk::DescriptorSet mirror;
        vk::DescriptorSet model;
    } descriptorSets;

    vk::DescriptorSetLayout descriptorSetLayout;
    vk::Semaphore glPresentComplete;
    GLFWwindow* glWindow{ nullptr };
    glm::vec3 meshPos = glm::vec3(0.0f, -1.5f, 0.0f);

    VulkanExample() : vkx::OffscreenExampleBase(false) {
        zoom = -6.5f;
        orientation = glm::quat(glm::radians(glm::vec3({ -11.25f, 45.0f, 0.0f })));
        timerSpeed *= 0.25f;
        title = "Vulkan Example - OpenGL interoperability";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        // Frame buffer
        destroyOffscreen();

        // Textures
        textures.colorMap.destroy();

        device.destroyPipeline(pipelines.shaded);
        device.destroyPipeline(pipelines.mirror);

        device.destroyPipelineLayout(pipelineLayouts.quad);
        device.destroyPipelineLayout(pipelineLayouts.offscreen);

        device.destroyDescriptorSetLayout(descriptorSetLayout);

        // Meshes
        meshes.example.destroy();
        meshes.quad.destroy();
        meshes.plane.destroy();

        // Uniform buffers
        uniformData.vsShared.destroy();
        uniformData.vsMirror.destroy();
        uniformData.vsOffScreen.destroy();

        if (nullptr != glWindow) {
            glfwDestroyWindow(glWindow);
        }
    }

    void setupWindow() override {
        OffscreenExampleBase::setupWindow();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_DEPTH_BITS, 16);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
        glWindow = glfwCreateWindow(TEX_DIM, TEX_DIM, "glfw", nullptr, nullptr);
        if (!glWindow) {
            throw std::runtime_error("Unable to create rendering window");
        }
        glfwMakeContextCurrent(glWindow);
        glfwSetWindowPos(glWindow, 100, -1080 + 100);
        glfwSwapInterval(0);
    }

    // The command buffer to copy for rendering 
    // the offscreen scene and blitting it into
    // the texture target is only build once
    // and gets resubmitted 
    void buildOffscreenCommandBuffer() override {

        vk::ClearValue clearValues[2];
        clearValues[0].color = vkx::clearColor({ 0.0f, 0.0f, 0.0f, 0.0f });
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = offscreen.renderPass;
        renderPassBeginInfo.framebuffer = offscreen.framebuffer.framebuffer;
        renderPassBeginInfo.renderArea.extent.width = offscreen.framebuffer.size.x;
        renderPassBeginInfo.renderArea.extent.height = offscreen.framebuffer.size.y;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vk::CommandBufferBeginInfo cmdBufInfo;
        cmdBufInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
        offscreen.cmdBuffer.begin(cmdBufInfo);
        offscreen.cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        offscreen.cmdBuffer.setViewport(0, vkx::viewport(offscreen.framebuffer.size));
        offscreen.cmdBuffer.setScissor(0, vkx::rect2D(offscreen.framebuffer.size));
        offscreen.cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.offscreen, nullptr);
        offscreen.cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.shaded);
        offscreen.cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buffer, { 0 });
        offscreen.cmdBuffer.bindIndexBuffer(meshes.example.indices.buffer, 0, vk::IndexType::eUint32);
        offscreen.cmdBuffer.drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);
        offscreen.cmdBuffer.endRenderPass();
        offscreen.cmdBuffer.end();
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setViewport(0, vkx::viewport(size));
        cmdBuffer.setScissor(0, vkx::rect2D(size));

        // Reflection plane
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.mirror, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.mirror);

        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.plane.vertices.buffer, { 0 });
        cmdBuffer.bindIndexBuffer(meshes.plane.indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(meshes.plane.indexCount, 1, 0, 0, 0);

        // Model
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.model, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.shaded);

        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.example.vertices.buffer, { 0 });
        cmdBuffer.bindIndexBuffer(meshes.example.indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(meshes.example.indexCount, 1, 0, 0, 0);
    }

    void loadMeshes() {
        meshes.plane = loadMesh(getAssetPath() + "models/plane.obj", vertexLayout, 0.4f);
        meshes.example = loadMesh(getAssetPath() + "models/chinesedragon.dae", vertexLayout, 0.3f);
    }

    void loadTextures() {
        textures.colorMap = textureLoader->loadTexture(
            getAssetPath() + "textures/darkmetal_bc3.ktx",
            vk::Format::eBc3UnormBlock);
    }

    void generateQuad() {
        // Setup vertices for a single uv-mapped quad
        struct Vertex {
            float pos[3];
            float uv[2];
            float col[3];
            float normal[3];
        };

#define QUAD_COLOR_NORMAL { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }
        std::vector<Vertex> vertexBuffer =
        {
            { { 1.0f, 1.0f, 0.0f },{ 1.0f, 1.0f }, QUAD_COLOR_NORMAL },
            { { 0.0f, 1.0f, 0.0f },{ 0.0f, 1.0f }, QUAD_COLOR_NORMAL },
            { { 0.0f, 0.0f, 0.0f },{ 0.0f, 0.0f }, QUAD_COLOR_NORMAL },
            { { 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f }, QUAD_COLOR_NORMAL }
        };
#undef QUAD_COLOR_NORMAL
        meshes.quad.vertices = stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
        meshes.quad.indexCount = indexBuffer.size();
        meshes.quad.indices = stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vkx::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vkx::vertexSize(vertexLayout), vk::VertexInputRate::eVertex);

        // Attribute descriptions
        vertices.attributeDescriptions.resize(4);
        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, vk::Format::eR32G32B32Sfloat, 0);
        // Location 1 : Texture coordinates
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, vk::Format::eR32G32Sfloat, sizeof(float) * 3);
        // Location 2 : Color
        vertices.attributeDescriptions[2] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, vk::Format::eR32G32B32Sfloat, sizeof(float) * 5);
        // Location 3 : Normal
        vertices.attributeDescriptions[3] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3, vk::Format::eR32G32B32Sfloat, sizeof(float) * 8);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 6),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 8)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 5);

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
        // Textured quad pipeline layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eVertex,
                0),
            // Binding 1 : Fragment shader image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                1),
            // Binding 2 : Fragment shader image sampler
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eCombinedImageSampler,
                vk::ShaderStageFlagBits::eFragment,
                2)
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);


        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

        pipelineLayouts.quad = device.createPipelineLayout(pPipelineLayoutCreateInfo);


        // Offscreen pipeline layout
        pipelineLayouts.offscreen = device.createPipelineLayout(pPipelineLayoutCreateInfo);

    }

    void setupDescriptorSet() {
        // Mirror plane descriptor set
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSets.mirror = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the offscreen mirror texture
        vk::DescriptorImageInfo texDescriptorMirror =
            vkx::descriptorImageInfo(offscreen.framebuffer.colors[0].sampler, offscreen.framebuffer.colors[0].view, vk::ImageLayout::eGeneral);

        // vk::Image descriptor for the color map
        vk::DescriptorImageInfo texDescriptorColorMap =
            vkx::descriptorImageInfo(textures.colorMap.sampler, textures.colorMap.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.mirror,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsMirror.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.mirror,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptorMirror),
            // Binding 2 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.mirror,
                vk::DescriptorType::eCombinedImageSampler,
                2,
                &texDescriptorColorMap)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Model
        // No texture
        descriptorSets.model = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> modelWriteDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.model,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsShared.descriptor)
        };
        device.updateDescriptorSets(modelWriteDescriptorSets.size(), modelWriteDescriptorSets.data(), 0, NULL);

        // Offscreen
        descriptorSets.offscreen = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> offScreenWriteDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
            descriptorSets.offscreen,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vsOffScreen.descriptor)
        };
        device.updateDescriptorSets(offScreenWriteDescriptorSets.size(), offScreenWriteDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise);

        vk::PipelineColorBlendAttachmentState blendAttachmentState =
            vkx::pipelineColorBlendAttachmentState();

        vk::PipelineColorBlendStateCreateInfo colorBlendState =
            vkx::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vkx::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual);

        vk::PipelineViewportStateCreateInfo viewportState =
            vkx::pipelineViewportStateCreateInfo(1, 1);

        vk::PipelineMultisampleStateCreateInfo multisampleState =
            vkx::pipelineMultisampleStateCreateInfo(vk::SampleCountFlagBits::e1);

        std::vector<vk::DynamicState> dynamicStateEnables = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        // Solid rendering pipeline
        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/quad.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/quad.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
            vkx::pipelineCreateInfo(pipelineLayouts.quad, renderPass);

        pipelineCreateInfo.pVertexInputState = &vertices.inputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();

        // Mirror
        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/mirror.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/mirror.frag.spv", vk::ShaderStageFlagBits::eFragment);

        pipelines.mirror = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];


        // Solid shading pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen/offscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen/offscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);

        pipelineCreateInfo.layout = pipelineLayouts.offscreen;

        pipelines.shaded = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Mesh vertex shader uniform buffer block
        uniformData.vsShared = createUniformBuffer(ubos.vsShared);
        uniformData.vsShared.map();

        // Mirror plane vertex shader uniform buffer block
        uniformData.vsMirror = createUniformBuffer(ubos.vsShared);
        uniformData.vsMirror.map();

        // Offscreen vertex shader uniform buffer block
        uniformData.vsOffScreen = createUniformBuffer(ubos.vsShared);
        uniformData.vsOffScreen.map();

        updateUniformBuffers();
        updateUniformBufferOffscreen();
    }

    void updateUniformBuffers() {
        // Mesh
        ubos.vsShared.projection = getProjection();
        ubos.vsShared.model = glm::translate(glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom)) * glm::mat4_cast(orientation), meshPos);
        uniformData.vsShared.copy(ubos.vsShared);

        // Mirror
        ubos.vsShared.model = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom)) * glm::mat4_cast(orientation);
        uniformData.vsMirror.copy(ubos.vsShared);
    }

    void updateUniformBufferOffscreen() {
        ubos.vsShared.projection = getProjection();
        ubos.vsShared.model = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom)) * glm::mat4_cast(orientation);
        ubos.vsShared.model = glm::scale(ubos.vsShared.model, glm::vec3(1.0f, -1.0f, 1.0f));
        ubos.vsShared.model = glm::translate(ubos.vsShared.model, meshPos);
        uniformData.vsOffScreen.copy(ubos.vsShared);
    }

    void prepare() override {
        offscreen.framebuffer.size = glm::uvec2(TEX_DIM);
        OffscreenExampleBase::prepare();
        loadTextures();
        generateQuad();
        loadMeshes();
        setupVertexDescriptions();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildOffscreenCommandBuffer();
        updateDrawCommandBuffers();

        // Initialize the OpenGL bindings
        // For some reason we have to set this experminetal flag to properly
        // init GLEW if we use a core context.
        glewExperimental = GL_TRUE;
        glfwMakeContextCurrent(glWindow);
        if (0 != glewInit()) {
            throw std::runtime_error("Failed to initialize GLEW");
        }
        glGetError();
        if (GLEW_KHR_debug) {
            GLint v;
            glGetIntegerv(GL_CONTEXT_FLAGS, &v);
            if (v & GL_CONTEXT_FLAG_DEBUG_BIT) {
                glDebugMessageCallback((GLDEBUGPROC)gl::DebugCallbackHandler, this);
            }
        }

        // Ensure the extension is available
        gl::nv::vk::init();
        prepared = true;
    }

    void draw() override {
        prepareFrame();

        // Offscreen rendering
        {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &semaphores.acquireComplete;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &offscreen.renderComplete;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &offscreen.cmdBuffer;
            queue.submit(submitInfo, VK_NULL_HANDLE);
        }

        {
            gl::nv::vk::WaitSemaphore(semaphores.renderComplete);
            gl::nv::vk::DrawVkImage(offscreen.framebuffer.colors[0].image, 0, vec2(0), vec2(TEX_DIM));
        }

        // Scene rendering
        drawCurrentCommandBuffer(offscreen.renderComplete);
        submitFrame();

        glfwSwapBuffers(glWindow);

#ifdef INTEROP
        glFlush();
        gl::nv::vk::SignalSemaphore(semaphores.renderComplete);
        glClearColor(0, 0.5f, 0.8f, 1.0f);
        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
#endif


    }

    virtual void render() {
        if (!prepared)
            return;
        draw();

        if (!paused) {
            updateUniformBuffers();
            updateUniformBufferOffscreen();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffers();
        updateUniformBufferOffscreen();
    }
};


RUN_EXAMPLE(VulkanExample)

