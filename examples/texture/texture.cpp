/*
* Vulkan Example - Texture loading (and display) example (including mip maps)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vulkanExampleBase.h>
#include <vks/texture.hpp>
#include <vulkanTools.h>

// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
    float normal[3];
};

class TextureExample : public vkx::ExampleBase {
    using Parent = ExampleBase;
public:
    // Contains all Vulkan objects that are required to store and use a texture
    // Note that this repository contains a texture loader (TextureLoader.h)
    // that encapsulates texture loading functionality in a class that is used
    // in subsequent demos
    vks::texture::Texture2D texture;

    struct {
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

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
        camera.setZoom(-2.5f);
        camera.setRotation({ 0.0f, 15.0f, 0.0f });
        title = "Vulkan Example - Texturing";
    }

    ~TextureExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        // Clean up texture resources
        texture.destroy();

        device.destroyPipeline(pipelines.solid);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        device.destroyBuffer(vertices.buffer);
        device.freeMemory(vertices.memory);

        device.destroyBuffer(indices.buffer);
        device.freeMemory(indices.memory);

        device.destroyBuffer(uniformDataVS.buffer);
        device.freeMemory(uniformDataVS.memory);
    }

    // Create an image memory barrier for changing the layout of
    // an image and put it into an active command buffer
    void setImageLayout(vk::CommandBuffer cmdBuffer, vk::Image image, vk::ImageAspectFlags aspectMask, vk::ImageLayout oldImageLayout, vk::ImageLayout newImageLayout, uint32_t mipLevel, uint32_t mipLevelCount) {
        // Create an image barrier object
        vk::ImageMemoryBarrier imageMemoryBarrier;
        imageMemoryBarrier.oldLayout = oldImageLayout;
        imageMemoryBarrier.newLayout = newImageLayout;
        imageMemoryBarrier.image = image;
        imageMemoryBarrier.subresourceRange.aspectMask = aspectMask;
        imageMemoryBarrier.subresourceRange.baseMipLevel = mipLevel;
        imageMemoryBarrier.subresourceRange.levelCount = mipLevelCount;
        imageMemoryBarrier.subresourceRange.layerCount = 1;

        // Only sets masks for layouts used in this example
        // For a more complete version that can be used with
        // other layouts see vkx::setImageLayout

        // Source layouts (new)

        if (oldImageLayout == vk::ImageLayout::ePreinitialized) {
            imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
        } else if (oldImageLayout == vk::ImageLayout::eTransferDstOptimal) {
            imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        } else if (oldImageLayout == vk::ImageLayout::eTransferSrcOptimal) {
            imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        } else if (oldImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        }

        // Target layouts (new)

        if (newImageLayout == vk::ImageLayout::eTransferDstOptimal) {
            // New layout is transfer destination (copy, blit)
            // Make sure any reads from and writes to the image have been finished
            imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        } else if (newImageLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
            // New layout is shader read (sampler, input attachment)
            // Make sure any writes to the image have been finished
            imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        } else if (newImageLayout == vk::ImageLayout::eTransferSrcOptimal) {
            // New layout is transfer source (copy, blit)
            // Make sure any reads from and writes to the image have been finished
            imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        }

        // Put barrier on top
        vk::PipelineStageFlags srcStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
        vk::PipelineStageFlags destStageFlags = vk::PipelineStageFlagBits::eTopOfPipe;

        // Put barrier inside setup command buffer
        cmdBuffer.pipelineBarrier(srcStageFlags, destStageFlags, vk::DependencyFlags(), nullptr, nullptr, imageMemoryBarrier);
    }

    void loadTexture(const std::string& fileName, vk::Format format) {
        texture.loadFromFile(context, fileName, format);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {
        cmdBuffer.setViewport(0, vks::util::viewport(size));
        cmdBuffer.setScissor(0, vks::util::rect2D(size));
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
        vk::DeviceSize offsets = 0;
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, vertices.buffer, offsets);
        cmdBuffer.bindIndexBuffer(indices.buffer, 0, vk::IndexType::eUint32);

        cmdBuffer.drawIndexed(indices.count, 1, 0, 0, 0);
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
        auto result= context.createBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);
        vertices.buffer = result.buffer;
        vertices.memory = result.memory;

        // Setup indices
        std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
        indices.count = indexBuffer.size();
        result= context.createBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
        indices.buffer = result.buffer;
        indices.memory = result.memory;
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions = {
            vk::VertexInputBindingDescription{ VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex }
        };

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions = {
            // Location 0 : Position
            vk::VertexInputAttributeDescription{ 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, 0 },
            // Location 1 : Texture coordinates
            vk::VertexInputAttributeDescription{ 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, sizeof(float) * 3 },
            // Location 1 : Vertex normal
            vk::VertexInputAttributeDescription{ 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, sizeof(float) * 5 },
        };

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
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
        vk::DescriptorImageInfo texDescriptor{ texture.sampler, texture.view, vk::ImageLayout::eGeneral };
        device.updateDescriptorSets({
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformDataVS.descriptor },
            // Binding 1 : Fragment shader texture sampler
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texDescriptor },
            }, {});
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

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
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), (uint32_t)dynamicStateEnables.size());

        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = loadShader(getAssetPath() + "shaders/texture/texture.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/texture/texture.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
            vkx::pipelineCreateInfo(pipelineLayout, renderPass);

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

        context.trashPipeline(pipelines.solid);
        pipelines.solid = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformDataVS= context.createUniformBuffer(uboVS);
        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        // Vertex shader
        uboVS.projection = camera.matrices.perspective;
        glm::mat4 viewMatrix = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, camera.position.z));
        uboVS.model = viewMatrix * glm::translate(glm::mat4(), glm::vec3(camera.position.x, camera.position.y, 0));
        uboVS.model = uboVS.model * glm::mat4_cast(camera.orientation);
        uboVS.viewPos = glm::vec4(0.0f, 0.0f, -camera.position.z, 0.0f);
        uniformDataVS.copy(uboVS);
    }

    void prepare() override {
        Parent::prepare();
        generateQuad();
        setupVertexDescriptions();
        prepareUniformBuffers();
        loadTexture(getAssetPath() + "textures/metalplate01_rgba.ktx", vk::Format::eR8G8B8A8Unorm);
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        updateDrawCommandBuffers();
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

#if !defined(__ANDROID__)
    void keyPressed(uint32_t keyCode) override {
        switch (keyCode) {
        case GLFW_KEY_KP_ADD:
            changeLodBias(0.1f);
            break;
        case GLFW_KEY_KP_SUBTRACT:
            changeLodBias(-0.1f);
            break;
        }
    }
#endif
};

RUN_EXAMPLE(TextureExample)
