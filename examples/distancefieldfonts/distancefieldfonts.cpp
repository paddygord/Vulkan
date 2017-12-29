/*
* Vulkan Example - Font rendering using signed distance fields
*
* Font generated using https://github.com/libgdx/libgdx/wiki/Hiero
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanExampleBase.h"


// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
};

// AngelCode .fnt format structs and classes
struct bmchar {
    uint32_t x, y;
    uint32_t width;
    uint32_t height;
    int32_t xoffset;
    int32_t yoffset;
    int32_t xadvance;
    uint32_t page;
};

// Quick and dirty : complete ASCII table
// Only chars present in the .fnt are filled with data!
std::array<bmchar, 255> fontChars;

int32_t nextValuePair(std::stringstream *stream) {
    std::string pair;
    *stream >> pair;
    uint32_t spos = pair.find("=");
    std::string value = pair.substr(spos + 1);
    int32_t val = std::stoi(value);
    return val;
}

class VulkanExample : public vkx::ExampleBase {
public:
    bool splitScreen = true;

    struct {
        vkx::Texture fontSDF;
        vkx::Texture fontBitmap;
    } textures;

    struct {
        void operator=(const vkx::CreateBufferResult& result) {
            buffer = result.buffer;
            memory = result.memory;
        }
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    struct {
        void operator=(const vkx::CreateBufferResult& result) {
            buffer = result.buffer;
            memory = result.memory;
        }
        int count;
        vk::Buffer buffer;
        vk::DeviceMemory memory;
    } indices;

    struct {
        vks::Buffer vs;
        vks::Buffer fs;
    } uniformData;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVS;

    struct UboFS {
        glm::vec4 outlineColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        float outlineWidth = 0.6f;
        float outline = true;
    } uboFS;

    struct {
        vk::Pipeline sdf;
        vk::Pipeline bitmap;
    } pipelines;

    struct {
        vk::DescriptorSet sdf;
        vk::DescriptorSet bitmap;
    } descriptorSets;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() {
        camera.setZoom(-1.5f);
        title = "Vulkan Example - Distance field fonts";
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        // Clean up texture resources
        textures.fontSDF.destroy();
        textures.fontBitmap.destroy();

        device.destroyPipeline(pipelines.sdf);

        device.destroyPipelineLayout(pipelineLayout);
        device.destroyDescriptorSetLayout(descriptorSetLayout);

        device.destroyBuffer(vertices.buffer);
        device.freeMemory(vertices.memory);

        device.destroyBuffer(indices.buffer);
        device.freeMemory(indices.memory);

        device.destroyBuffer(uniformData.vs.buffer);
        device.freeMemory(uniformData.vs.memory);
    }

    // Basic parser fpr AngelCode bitmap font format files
    // See http://www.angelcode.com/products/bmfont/doc/file_format.html for details
    void parsebmFont() {
        std::string fileName = getAssetPath() + "font.fnt";

        auto fileData = vkx::readBinaryFile(fileName);
        std::stringbuf sbuf((const char*)fileData.data(), fileData.size());
        std::istream istream(&sbuf);
        assert(istream.good());

        while (!istream.eof()) {
            std::string line;
            std::stringstream lineStream;
            std::getline(istream, line);
            lineStream << line;

            std::string info;
            lineStream >> info;

            if (info == "char") {
                std::string pair;

                // char id
                uint32_t charid = nextValuePair(&lineStream);
                // Char properties
                fontChars[charid].x = nextValuePair(&lineStream);
                fontChars[charid].y = nextValuePair(&lineStream);
                fontChars[charid].width = nextValuePair(&lineStream);
                fontChars[charid].height = nextValuePair(&lineStream);
                fontChars[charid].xoffset = nextValuePair(&lineStream);
                fontChars[charid].yoffset = nextValuePair(&lineStream);
                fontChars[charid].xadvance = nextValuePair(&lineStream);
                fontChars[charid].page = nextValuePair(&lineStream);
            }
        }

    }

    void loadTextures() {
        textures.fontSDF = textureLoader->loadTexture(
            getAssetPath() + "textures/font_sdf_rgba.ktx",
             vk::Format::eR8G8B8A8Unorm);
        textures.fontBitmap = textureLoader->loadTexture(
            getAssetPath() + "textures/font_bitmap_rgba.ktx",
             vk::Format::eR8G8B8A8Unorm);
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) override {

        vk::Viewport viewport = vks::util::viewport((float)size.width, (splitScreen) ? (float)size.height / 2.0f : (float)size.height, 0.0f, 1.0f);
        cmdBuffer.setViewport(0, viewport);
        cmdBuffer.setScissor(0, vks::util::rect2D(size));

        // Signed distance field font
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.sdf, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.sdf);
        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, vertices.buffer, { 0 });
        cmdBuffer.bindIndexBuffer(indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(indices.count, 1, 0, 0, 0);

        // Linear filtered bitmap font
        if (splitScreen) {
            viewport.y += viewport.height;
            cmdBuffer.setViewport(0, viewport);
            cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSets.bitmap, nullptr);
            cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.bitmap);
            cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, vertices.buffer, { 0 });
            cmdBuffer.bindIndexBuffer(indices.buffer, 0, vk::IndexType::eUint32);
            cmdBuffer.drawIndexed(indices.count, 1, 0, 0, 0);
        }
    }

    // todo : function fill buffer with quads from font

    // Creates a vertex buffer containing quads for the passed text
    void generateText(std::string text) {
        std::vector<Vertex> vertexBuffer;
        std::vector<uint32_t> indexBuffer;
        uint32_t indexOffset = 0;

        float w = textures.fontSDF.extent.width;

        float posx = 0.0f;
        float posy = 0.0f;

        for (uint32_t i = 0; i < text.size(); i++) {
            bmchar *charInfo = &fontChars[(int)text[i]];

            if (charInfo->width == 0)
                charInfo->width = 36;

            float charw = ((float)(charInfo->width) / 36.0f);
            float dimx = 1.0f * charw;
            float charh = ((float)(charInfo->height) / 36.0f);
            float dimy = 1.0f * charh;
            posy = 1.0f - charh;

            float us = charInfo->x / w;
            float ue = (charInfo->x + charInfo->width) / w;
            float ts = charInfo->y / w;
            float te = (charInfo->y + charInfo->height) / w;

            float xo = charInfo->xoffset / 36.0f;
            float yo = charInfo->yoffset / 36.0f;

            vertexBuffer.push_back({ { posx + dimx + xo,  posy + dimy, 0.0f }, { ue, te } });
            vertexBuffer.push_back({ { posx + xo,         posy + dimy, 0.0f }, { us, te } });
            vertexBuffer.push_back({ { posx + xo,         posy,        0.0f }, { us, ts } });
            vertexBuffer.push_back({ { posx + dimx + xo,  posy,        0.0f }, { ue, ts } });

            std::array<uint32_t, 6> indices = { 0,1,2, 2,3,0 };
            for (auto& index : indices) {
                indexBuffer.push_back(indexOffset + index);
            }
            indexOffset += 4;

            float advance = ((float)(charInfo->xadvance) / 36.0f);
            posx += advance;
        }
        indices.count = indexBuffer.size();

        // Center
        for (auto& v : vertexBuffer) {
            v.pos[0] -= posx / 2.0f;
            v.pos[1] -= 0.5f;
        }
        vertices= context.createBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);
        indices= context.createBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
    }

    void setupVertexDescriptions() {
        // Binding description
        vertices.bindingDescriptions.resize(1);
        vertices.bindingDescriptions[0] =
            vkx::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, sizeof(Vertex), vk::VertexInputRate::eVertex);

        // Attribute descriptions
        // Describes memory layout and shader positions
        vertices.attributeDescriptions.resize(2);
        // Location 0 : Position
        vertices.attributeDescriptions[0] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0,  vk::Format::eR32G32B32Sfloat, 0);
        // Location 1 : Texture coordinates
        vertices.attributeDescriptions[1] =
            vkx::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1,  vk::Format::eR32G32Sfloat, sizeof(float) * 3);

        vertices.inputState = vk::PipelineVertexInputStateCreateInfo();
        vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
        vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
        vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
        vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
    }

    void setupDescriptorPool() {
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 4),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

        descriptorPool = device.createDescriptorPool(descriptorPoolInfo);
    }

    void setupDescriptorSetLayout() {
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
            // Binding 2 : Fragment shader uniform buffer
            vkx::descriptorSetLayoutBinding(
                vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eFragment,
                2)
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout =
            vkx::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);


        vk::PipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vkx::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

        pipelineLayout = device.createPipelineLayout(pPipelineLayoutCreateInfo);

    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        // Signed distance front descriptor set
        descriptorSets.sdf = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the color map texture
        vk::DescriptorImageInfo texDescriptor =
            vkx::descriptorImageInfo(textures.fontSDF.sampler, textures.fontSDF.view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
            descriptorSets.sdf,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vs.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.sdf,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor),
            // Binding 2 : Fragment shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.sdf,
                vk::DescriptorType::eUniformBuffer,
                2,
                &uniformData.fs.descriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Default font rendering descriptor set
        descriptorSets.bitmap = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the color map texture
        texDescriptor.sampler = textures.fontBitmap.sampler;
        texDescriptor.imageView = textures.fontBitmap.view;

        writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.bitmap,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.vs.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSets.bitmap,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

        vk::PipelineColorBlendAttachmentState blendAttachmentState;
        blendAttachmentState.blendEnable = VK_TRUE;
        blendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        blendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        blendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
        blendAttachmentState.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlendState =
            vkx::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        vk::PipelineDepthStencilStateCreateInfo depthStencilState =
            vkx::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_TRUE, vk::CompareOp::eLessOrEqual);

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

        // Load shaders
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;

        shaderStages[0] = context.loadShader(getAssetPath() + "shaders/distancefieldfonts/sdf.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = context.loadShader(getAssetPath() + "shaders/distancefieldfonts/sdf.frag.spv", vk::ShaderStageFlagBits::eFragment);

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

        pipelines.sdf = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];


        // Default bitmap font rendering pipeline
        shaderStages[0] = context.loadShader(getAssetPath() + "shaders/distancefieldfonts/bitmap.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = context.loadShader(getAssetPath() + "shaders/distancefieldfonts/bitmap.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelines.bitmap = device.createGraphicsPipelines(context.pipelineCache, pipelineCreateInfo, nullptr)[0];

    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformData.vs= context.createUniformBuffer(uboVS);

        // Fragment sahder uniform buffer block
        // Contains font rendering parameters
        uniformData.fs= context.createUniformBuffer(uboFS);

        updateUniformBuffers();
        updateFontSettings();
    }

    void updateUniformBuffers() {
        // Vertex shader
        uboVS.projection = glm::perspective(glm::radians(splitScreen ? 45.0f : 45.0f), (float)size.width / (float)(size.height * ((splitScreen) ? 0.5f : 1.0f)), 0.001f, 256.0f);
        uboVS.model = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, splitScreen ? camera.position.z : camera.position.z - 2.0f)) * glm::mat4_cast(camera.orientation);
        uniformData.vs.copy(uboVS);
    }

    void updateFontSettings() {
        // Fragment shader
        uniformData.fs.copy(uboFS);
    }

    void prepare() override {
        ExampleBase::prepare();
        parsebmFont();
        loadTextures();
        generateText("Vulkan");
        setupVertexDescriptions();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        updateDrawCommandBuffers();
        prepared = true;
    }

    void render() override {
        if (!prepared)
            return;
        draw();
    }

    void viewChanged() override {
        updateUniformBuffers();
    }

    void toggleSplitScreen() {
        splitScreen = !splitScreen;
        updateDrawCommandBuffers();
        updateUniformBuffers();
    }

    void toggleFontOutline() {
        uboFS.outline = !uboFS.outline;
        updateFontSettings();
    }


    void keyPressed(uint32_t key) override {
        switch (key) {
        case GLFW_KEY_S:
            toggleSplitScreen();
            break;
        case GLFW_KEY_O:
            toggleFontOutline();
            break;
        }
    }
};

RUN_EXAMPLE(VulkanExample)
