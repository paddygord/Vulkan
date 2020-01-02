/*
* Vulkan Example - Texture arrays and instanced rendering
*
* Copyright (C) Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vkx/vulkanExampleBase.hpp>
#include <vkx/texture.hpp>
#include <khrpp/ktx/ktx.hpp>

#define ENABLE_VALIDATION false

// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
};

class VulkanExample : public VulkanExampleBase {
public:
    // Number of array layers in texture array
    // Also used as instance count
    vkx::texture::Texture2DArray textureArray;
    const uint32_t& layerCount{ textureArray.layerCount };

    vks::Buffer vertexBuffer;
    vks::Buffer indexBuffer;
    uint32_t indexCount{ 0 };

    vks::Buffer uniformBufferVS;

    struct UboInstanceData {
        // Model matrix
        glm::mat4 model;
        // Texture array index
        // Vec4 due to padding
        glm::vec4 arrayIndex;
    };

    struct {
        // Global matrices
        struct {
            glm::mat4 projection;
            glm::mat4 view;
        } matrices;
        // Seperate data for each instance
        std::vector<UboInstanceData> instance;
    } uboVS;

    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample()
        : VulkanExampleBase(ENABLE_VALIDATION) {
        title = "Texture arrays";
        settings.overlay = true;
        camera.type = Camera::CameraType::lookat;
        camera.setPosition(glm::vec3(0.0f, 0.0f, -7.5f));
        camera.setRotation(glm::vec3(-35.0f, 0.0f, 0.0f));
        camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class
        textureArray.destroy();

        device.destroy(pipeline);
        device.destroy(pipelineLayout);
        device.destroy(descriptorSetLayout);

        vertexBuffer.destroy();
        indexBuffer.destroy();

        uniformBufferVS.destroy();
    }

    void loadTextureArray(std::string filename, vk::Format format) { textureArray.loadFromFile(context, filename, format); }

    void loadAssets() override {
        // Vulkan core supports three different compressed texture formats
        // As the support differs between implemementations we need to check device features and select a proper format and file
        std::string filename;
        vk::Format format;
        if (deviceFeatures.textureCompressionBC) {
            filename = "texturearray_bc3_unorm.ktx";
            format = vk::Format::eBc3UnormBlock;
        } else if (deviceFeatures.textureCompressionASTC_LDR) {
            filename = "texturearray_astc_8x8_unorm.ktx";
            format = vk::Format::eAstc8x8UnormBlock;
        } else if (deviceFeatures.textureCompressionETC2) {
            filename = "texturearray_etc2_unorm.ktx";
            format = vk::Format::eEtc2R8G8B8UnormBlock;
        } else {
            vks::tools::exitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
        }
        loadTextureArray(getAssetPath() + "textures/" + filename, format);

    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& commandBuffer) override {
        commandBuffer.setViewport(0, vks::util::viewport(size));
        commandBuffer.setScissor(0, vks::util::rect2D(size));

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        commandBuffer.bindVertexBuffers(0, vertexBuffer.buffer, { 0 });
        commandBuffer.bindIndexBuffer(indexBuffer.buffer, 0, vk::IndexType::eUint32);

        commandBuffer.drawIndexed(indexCount, layerCount, 0, 0, 0);

        //drawUI(drawCmdBuffers[i]);
    }

    void generateCube() {
        std::vector<Vertex> vertices = {
            { { -1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f } },  { { 1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f } },
            { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },    { { -1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },

            { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },    { { 1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f } },
            { { 1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f } },  { { 1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f } },

            { { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f } }, { { 1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f } },
            { { 1.0f, 1.0f, -1.0f }, { 1.0f, 1.0f } },   { { -1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f } },

            { { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f } }, { { -1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f } },
            { { -1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },   { { -1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f } },

            { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },    { { -1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            { { -1.0f, 1.0f, -1.0f }, { 1.0f, 1.0f } },  { { 1.0f, 1.0f, -1.0f }, { 0.0f, 1.0f } },

            { { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f } }, { { 1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f } },
            { { 1.0f, -1.0f, 1.0f }, { 1.0f, 1.0f } },   { { -1.0f, -1.0f, 1.0f }, { 0.0f, 1.0f } },
        };
        // Create buffers
        vertexBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertices);
        std::vector<uint32_t> indices{ {
            0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
        } };
        indexCount = static_cast<uint32_t>(indices.size());
        indexBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indices);
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes{
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 1 },
        };
        descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings {
            // Binding 0 : Vertex shader uniform buffer
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader image sampler (texture array)
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        descriptorSet = device.allocateDescriptorSets({ descriptorPool, 1, &descriptorSetLayout })[0];

        // Image descriptor for the texture array
        vk::DescriptorImageInfo textureDescriptor{ textureArray.sampler, textureArray.view, textureArray.imageLayout };

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformBufferVS.descriptor },
            // Binding 1 : Fragment shader cubemap sampler
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &textureDescriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder builder(device, pipelineLayout, renderPass);
        builder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        // Vertex bindings and attributes
        builder.vertexInputState.bindingDescriptions = {
            vk::VertexInputBindingDescription{ 0, sizeof(Vertex) },
        };
        builder.vertexInputState.attributeDescriptions = {
            vk::VertexInputAttributeDescription{ 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos) },
            vk::VertexInputAttributeDescription{ 1, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv) },
        };
        builder.loadShader(getAssetPath() + "shaders/texturearray/instancing.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/texturearray/instancing.frag.spv", vk::ShaderStageFlagBits::eFragment);

        // Instacing pipeline
        pipeline = builder.create(pipelineCache);
    }

    void prepareUniformBuffers() {
        uboVS.instance.resize(layerCount);

        uint32_t uboSize = sizeof(uboVS.matrices) + (layerCount * sizeof(UboInstanceData));

        // Vertex shader uniform buffer block
        uniformBufferVS = context.createSizedUniformBuffer(uboSize);

        // Array indices and model matrices are fixed
        float offset = -1.5f;
        float center = (layerCount * offset) / 2.0f - (offset * 0.5f);
        for (uint32_t i = 0; i < layerCount; i++) {
            // Instance model matrix
            uboVS.instance[i].model = glm::translate(glm::mat4(1.0f), glm::vec3(i * offset - center, 0.0f, 0.0f));
            uboVS.instance[i].model = glm::scale(uboVS.instance[i].model, glm::vec3(0.5f));
            // Instance texture array index
            uboVS.instance[i].arrayIndex.x = (float)i;
        }

        // Update instanced part of the uniform buffer
        uint32_t dataOffset = sizeof(uboVS.matrices);
        uint32_t dataSize = layerCount * sizeof(UboInstanceData);
        uint8_t* pData = (uint8_t*)uniformBufferVS.mapped + dataOffset;
        memcpy(pData, uboVS.instance.data(), dataSize);

        updateUniformBuffersCamera();
    }

    void updateUniformBuffersCamera() {
        uboVS.matrices.projection = camera.matrices.perspective;
        uboVS.matrices.view = camera.matrices.view;
        memcpy(uniformBufferVS.mapped, &uboVS.matrices, sizeof(uboVS.matrices));
    }

    void prepare() {
        VulkanExampleBase::prepare();
        generateCube();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    void viewChanged() override { updateUniformBuffersCamera(); }
};

VULKAN_EXAMPLE_MAIN()