/*
* Vulkan Example - Offscreen rendering using a separate framebuffer
*
*    p - Toggle light source animation
*    l - Toggle between scene and light's POV
*    s - Toggle shadowmap display
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanOffscreenExampleBase.hpp"


// 16 bits of depth is enough for such a small scene
#define DEPTH_FORMAT  vk::Format::eD16Unorm

// Texture properties
#define TEX_DIM 2048
#define TEX_FILTER vk::Filter::eLinear

// Offscreen frame buffer properties
#define FB_DIM TEX_DIM
#define FB_COLOR_FORMAT  vk::Format::eR8G8B8A8Unorm

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
    bool displayShadowMap = false;
    bool lightPOV = false;

    // Keep depth range as small as possible
    // for better shadow map precision
    float zNear = 1.0f;
    float zFar = 96.0f;

    // Constant depth bias factor (always applied)
    float depthBiasConstant = 1.25f;
    // Slope depth bias factor, applied depending on polygon's slope
    float depthBiasSlope = 1.75f;

    glm::vec3 lightPos = glm::vec3();
    float lightFOV = 45.0f;

    struct {
        vkx::MeshBuffer scene;
        vkx::MeshBuffer quad;
    } meshes;

    struct {
        vk::PipelineVertexInputStateCreateInfo inputState;
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    } vertices;

    vkx::UniformData uniformDataVS, uniformDataOffscreenVS;

    struct {
        vkx::UniformData scene;
    } uniformData;

    struct {
        glm::mat4 projection;
        glm::mat4 model;
    } uboVSquad;

    struct {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model;
        glm::mat4 depthBiasMVP;
        glm::vec3 lightPos;
    } uboVSscene;

    struct {
        glm::mat4 depthMVP;
    } uboOffscreenVS;

    struct {
        vk::Pipeline quad;
        vk::Pipeline offscreen;
        vk::Pipeline scene;
    } pipelines;

    struct {
        vk::PipelineLayout quad;
        vk::PipelineLayout offscreen;
    } pipelineLayouts;

    struct {
        vk::DescriptorSet offscreen;
        vk::DescriptorSet scene;
    } descriptorSets;

    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() : vkx::OffscreenExampleBase(ENABLE_VALIDATION) {
        camera.setZoom(-20.0f);
        orientation = glm::quat(glm::radians(glm::vec3({ -15.0f, -390.0f, 0.0f })));
        title = "Vulkan Example - Projected shadow mapping";
        timerSpeed *= 0.5f;
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources 
        // Note : Inherited destructor cleans up resources stored in base class

        device.destroyPipeline(pipelines.quad);
        device.destroyPipeline(pipelines.offscreen);
        device.destroyPipeline(pipelines.scene);

        device.destroyPipelineLayout(pipelineLayouts.quad);
        device.destroyPipelineLayout(pipelineLayouts.offscreen);

        device.destroyDescriptorSetLayout(descriptorSetLayout);

        // Meshes
        meshes.scene.destroy();
        meshes.quad.destroy();

        // Uniform buffers
        uniformDataVS.destroy();
        uniformDataOffscreenVS.destroy();
    }

        virtual void prepareOffscreenRenderPass() {
            std::vector<vk::AttachmentDescription> attachments;
            std::vector<vk::AttachmentReference> colorAttachmentReferences;
            attachments.resize(offscreen.framebuffer.colorFormats.size());
            colorAttachmentReferences.resize(attachments.size());
            // Color attachment
            for (size_t i = 0; i < attachments.size(); ++i) {
                attachments[i].format = colorformat;
                attachments[i].loadOp = vk::AttachmentLoadOp::eClear;
                attachments[i].storeOp = vk::AttachmentStoreOp::eStore;
                attachments[i].initialLayout = vk::ImageLayout::eUndefined;
                attachments[i].finalLayout = offscreen.colorFinalLayout;
                vk::AttachmentReference& attachmentReference = colorAttachmentReferences[i];
                attachmentReference.attachment = i;
                attachmentReference.layout = vk::ImageLayout::eColorAttachmentOptimal;
            }

            // Depth attachment
            vk::AttachmentReference depthAttachmentReference;
            {
                vk::AttachmentDescription depthAttachment;
                depthAttachment.format = depthFormat;
                depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
                depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
                depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                depthAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                attachments.push_back(depthAttachment);
                depthAttachmentReference.attachment = attachments.size() - 1;
                depthAttachmentReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            }

            std::vector<vk::SubpassDescription> subpasses;
            {
                vk::SubpassDescription subpass;
                subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
                subpass.pDepthStencilAttachment = &depthAttachmentReference;
                subpass.colorAttachmentCount = colorAttachmentReferences.size();
                subpass.pColorAttachments = colorAttachmentReferences.data();
                subpasses.push_back(subpass);
            }


            std::vector<vk::SubpassDependency> subpassDependencies;
            {
                vk::SubpassDependency dependency;
                dependency.srcSubpass = 0;
                dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
                dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;

                dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
                switch (offscreen.colorFinalLayout) {
                case vk::ImageLayout::eShaderReadOnlyOptimal:
                    dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                    break;
                case vk::ImageLayout::eTransferSrcOptimal:
                    dependency.dstAccessMask = vk::AccessFlagBits::eTransferRead;
                    break;
                default:
                    throw std::runtime_error("Unhandled color final layout");
                }
                dependency.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
                subpassDependencies.push_back(dependency);
            }

            if (offscreen.renderPass) {
                device.destroyRenderPass(offscreen.renderPass);
            }

            vk::RenderPassCreateInfo renderPassInfo;
            renderPassInfo.attachmentCount = attachments.size();
            renderPassInfo.pAttachments = attachments.data();
            renderPassInfo.subpassCount = subpasses.size();
            renderPassInfo.pSubpasses = subpasses.data();
            renderPassInfo.dependencyCount = subpassDependencies.size();
            renderPassInfo.pDependencies = subpassDependencies.data();
            offscreen.renderPass = device.createRenderPass(renderPassInfo);
        }


    void buildOffscreenCommandBuffer() {
        // Create separate command buffer for offscreen 
        // rendering
        if (!offscreen.cmdBuffer) {
            vk::CommandBufferAllocateInfo cmd = vkx::commandBufferAllocateInfo(cmdPool, vk::CommandBufferLevel::ePrimary, 1);
            offscreen.cmdBuffer = device.allocateCommandBuffers(cmd)[0];
        }

        //// Make sure color writes to the framebuffer are finished before using it as transfer source
        //vkx::setImageLayout(
        //    offscreen.cmdBuffer,
        //    offscreen.framebuffer.depth.image,
        //    vk::ImageAspectFlagBits::eDepth,
        //    vk::ImageLayout::eDepthStencilAttachmentOptimal,
        //    vk::ImageLayout::eShaderReadOnlyOptimal);

        //// Transform framebuffer color attachment back 
        //vkx::setImageLayout(
        //    offscreen.cmdBuffer,
        //    offscreen.framebuffer.depth.image,
        //    vk::ImageAspectFlagBits::eDepth,
        //    vk::ImageLayout::eTransferSrcOptimal,
        //    vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::ClearValue clearValues[2];
        clearValues[0].color = vkx::clearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        clearValues[1].depthStencil = { 1.0f, 0 };

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = offscreen.renderPass;
        renderPassBeginInfo.framebuffer = offscreen.framebuffer.framebuffer;
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = offscreen.framebuffer.size.x;
        renderPassBeginInfo.renderArea.extent.height = offscreen.framebuffer.size.y;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vk::CommandBufferBeginInfo cmdBufInfo;
        cmdBufInfo.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
        offscreen.cmdBuffer.begin(cmdBufInfo);
        offscreen.cmdBuffer.setViewport(0, vkx::viewport(offscreen.framebuffer.size));
        offscreen.cmdBuffer.setScissor(0, vkx::rect2D(offscreen.framebuffer.size));

        // Set depth bias (aka "Polygon offset")
        offscreen.cmdBuffer.setDepthBias(depthBiasConstant, 0.0f, depthBiasSlope);
        offscreen.cmdBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

        offscreen.cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.offscreen);
        offscreen.cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.offscreen, 0, descriptorSets.offscreen, nullptr);
        offscreen.cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.scene.vertices.buffer, { 0 });
        offscreen.cmdBuffer.bindIndexBuffer(meshes.scene.indices.buffer, 0, vk::IndexType::eUint32);
        offscreen.cmdBuffer.drawIndexed(meshes.scene.indexCount, 1, 0, 0, 0);
        offscreen.cmdBuffer.endRenderPass();
        offscreen.cmdBuffer.end();
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& cmdBuffer) {
        cmdBuffer.setViewport(0, vkx::viewport(size));
        cmdBuffer.setScissor(0, vkx::rect2D(size));
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSet, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.quad);

        // Visualize shadow map
        if (displayShadowMap) {
            cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.quad.vertices.buffer, { 0 });
            cmdBuffer.bindIndexBuffer(meshes.quad.indices.buffer, 0, vk::IndexType::eUint32);
            cmdBuffer.drawIndexed(meshes.quad.indexCount, 1, 0, 0, 0);
        }

        // 3D scene
        cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayouts.quad, 0, descriptorSets.scene, nullptr);
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.scene);

        cmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, meshes.scene.vertices.buffer, { 0 });
        cmdBuffer.bindIndexBuffer(meshes.scene.indices.buffer, 0, vk::IndexType::eUint32);
        cmdBuffer.drawIndexed(meshes.scene.indexCount, 1, 0, 0, 0);
    }

    void loadMeshes() {
        meshes.scene = loadMesh(getAssetPath() + "models/vulkanscene_shadow.dae", vertexLayout, 4.0f);
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
        // Example uses three ubos and two image samplers
        std::vector<vk::DescriptorPoolSize> poolSizes =
        {
            vkx::descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 6),
            vkx::descriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4)
        };

        vk::DescriptorPoolCreateInfo descriptorPoolInfo =
            vkx::descriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 3);

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
                1)
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

    void setupDescriptorSets() {
        // Textured quad descriptor set
        vk::DescriptorSetAllocateInfo allocInfo =
            vkx::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

        descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the shadow map texture
        vk::DescriptorImageInfo texDescriptor =
            vkx::descriptorImageInfo(offscreen.framebuffer.colors[0].sampler, offscreen.framebuffer.colors[0].view, vk::ImageLayout::eGeneral);

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformDataVS.descriptor),
            // Binding 1 : Fragment shader texture sampler
            vkx::writeDescriptorSet(
                descriptorSet,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor)
        };

        device.updateDescriptorSets(writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Offscreen
        descriptorSets.offscreen = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> offscreenWriteDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.offscreen,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformDataOffscreenVS.descriptor),
        };
        device.updateDescriptorSets(offscreenWriteDescriptorSets.size(), offscreenWriteDescriptorSets.data(), 0, NULL);

        // 3D scene
        descriptorSets.scene = device.allocateDescriptorSets(allocInfo)[0];

        // vk::Image descriptor for the shadow map texture
        texDescriptor.sampler = offscreen.framebuffer.colors[0].sampler;
        texDescriptor.imageView = offscreen.framebuffer.colors[0].view;

        std::vector<vk::WriteDescriptorSet> sceneDescriptorSets =
        {
            // Binding 0 : Vertex shader uniform buffer
            vkx::writeDescriptorSet(
                descriptorSets.scene,
                vk::DescriptorType::eUniformBuffer,
                0,
                &uniformData.scene.descriptor),
            // Binding 1 : Fragment shader shadow sampler
            vkx::writeDescriptorSet(
                descriptorSets.scene,
                vk::DescriptorType::eCombinedImageSampler,
                1,
                &texDescriptor)
        };
        device.updateDescriptorSets(sceneDescriptorSets.size(), sceneDescriptorSets.data(), 0, NULL);

    }

    void preparePipelines() {
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vkx::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList, vk::PipelineInputAssemblyStateCreateFlags(), VK_FALSE);

        vk::PipelineRasterizationStateCreateInfo rasterizationState =
            vkx::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill, vk::CullModeFlagBits::eFront, vk::FrontFace::eClockwise);

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

        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/quad.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/quad.frag.spv", vk::ShaderStageFlagBits::eFragment);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo =
            vkx::pipelineCreateInfo(pipelineLayouts.quad, renderPass);

        rasterizationState.cullMode = vk::CullModeFlagBits::eNone;

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

        pipelines.quad = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // 3D scene
        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/scene.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/scene.frag.spv", vk::ShaderStageFlagBits::eFragment);
        rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelines.scene = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];

        // Offscreen pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/shadowmapping/offscreen.vert.spv", vk::ShaderStageFlagBits::eVertex);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/shadowmapping/offscreen.frag.spv", vk::ShaderStageFlagBits::eFragment);
        pipelineCreateInfo.layout = pipelineLayouts.offscreen;
        // Cull front faces
        depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual;
        // Enable depth bias
        rasterizationState.depthBiasEnable = VK_TRUE;
        // Add depth bias to dynamic state, so we can change it at runtime
        dynamicStateEnables.push_back(vk::DynamicState::eDepthBias);
        dynamicState =
            vkx::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size());

        pipelines.offscreen = device.createGraphicsPipelines(pipelineCache, pipelineCreateInfo, nullptr)[0];
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Debug quad vertex shader uniform buffer block
        uniformDataVS = createUniformBuffer(uboVSscene);
        uniformDataVS.map();

        // Offsvreen vertex shader uniform buffer block
        uniformDataOffscreenVS = createUniformBuffer(uboOffscreenVS);
        uniformDataOffscreenVS.map();

        // Scene vertex shader uniform buffer block
        uniformData.scene = createUniformBuffer(uboVSscene);
        uniformData.scene.map();

        updateLight();
        updateUniformBufferOffscreen();
        updateUniformBuffers();
    }

    void updateLight() {
        // Animate the light source
        lightPos.x = cos(glm::radians(timer * 360.0f)) * 40.0f;
        lightPos.y = -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f;
        lightPos.z = 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f;
    }

    void updateUniformBuffers() {
        // Shadow map debug quad
        float AR = (float)size.height / (float)size.width;

        uboVSquad.projection = glm::ortho(0.0f, 2.5f / AR, 0.0f, 2.5f, -1.0f, 1.0f);
        uboVSquad.model = glm::mat4();

        uniformDataVS.copy(uboVSquad);

        // 3D scene
        uboVSscene.projection = glm::perspective(glm::radians(45.0f), (float)size.width / (float)size.height, zNear, zFar);

        uboVSscene.view = camera.matrices.view;
        uboVSscene.model = glm::mat4();
        uboVSscene.lightPos = lightPos;
        // Render scene from light's point of view
        if (lightPOV) {
            uboVSscene.projection = glm::perspective(glm::radians(lightFOV), (float)size.width / (float)size.height, zNear, zFar);
            uboVSscene.view = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        }
        uboVSscene.depthBiasMVP = uboOffscreenVS.depthMVP;
        uniformData.scene.copy(uboVSscene);
    }

    void updateUniformBufferOffscreen() {
        // Matrix from light's point of view
        glm::mat4 depthProjectionMatrix = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
        glm::mat4 depthViewMatrix = glm::lookAt(lightPos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 depthModelMatrix = glm::mat4();

        uboOffscreenVS.depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;
        uniformDataOffscreenVS.copy(uboOffscreenVS);
    }


    void prepare() {
        offscreen.framebuffer.size = glm::uvec2(TEX_DIM);
        OffscreenExampleBase::prepare();
        generateQuad();
        loadMeshes();
        setupVertexDescriptions();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSets();
        updateDrawCommandBuffers();
        buildOffscreenCommandBuffer();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        if (!paused) {
            updateLight();
            updateUniformBufferOffscreen();
            updateUniformBuffers();
        }
    }

    virtual void viewChanged() {
        updateUniformBufferOffscreen();
        updateUniformBuffers();
    }

    void toggleShadowMapDisplay() {
        displayShadowMap = !displayShadowMap;
        updateDrawCommandBuffers();
    }

    void toogleLightPOV() {
        lightPOV = !lightPOV;
        viewChanged();
    }

    void keyPressed(uint32_t key) override {
        switch (key) {
        case GLFW_KEY_S:
            toggleShadowMapDisplay();
            break;
        case GLFW_KEY_L:
            toogleLightPOV();
            break;
        }
    }
};

RUN_EXAMPLE(VulkanExample)
