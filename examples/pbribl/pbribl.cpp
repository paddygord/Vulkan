/*
* Vulkan Example - Physical based rendering with image based lighting
*
* Note: Requires the separate asset pack (see data/README.md)
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

// For reference see http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf

#include <vulkanExampleBase.h>

#define GRID_DIM 7

struct Material {
    // Parameter block used as push constant block
    struct PushBlock {
        float roughness = 0.0f;
        float metallic = 0.0f;
        float specular = 0.0f;
        float r, g, b;
    } params;
    std::string name;
    Material(){};
    Material(std::string n, glm::vec3 c)
        : name(n) {
        params.r = c.r;
        params.g = c.g;
        params.b = c.b;
    };
};

class VulkanExample : public vkx::ExampleBase {
public:
    bool displaySkybox = true;

    struct Textures {
        vks::texture::TextureCubeMap environmentCube;
        // Generated at runtime
        vks::texture::Texture2D lutBrdf;
        vks::texture::TextureCubeMap irradianceCube;
        vks::texture::TextureCubeMap prefilteredCube;
    } textures;

    // Vertex layout for the models
    vks::model::VertexLayout vertexLayout{ {
        vks::model::VERTEX_COMPONENT_POSITION,
        vks::model::VERTEX_COMPONENT_NORMAL,
        vks::model::VERTEX_COMPONENT_UV,
    } };

    struct Meshes {
        vks::model::Model skybox;
        std::vector<vks::model::Model> objects;
        int32_t objectIndex = 0;
    } models;

    struct {
        vks::Buffer object;
        vks::Buffer skybox;
        vks::Buffer params;
    } uniformBuffers;

    struct UBOMatrices {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 view;
        glm::vec3 camPos;
    } uboMatrices;

    struct UBOParams {
        glm::vec4 lights[4];
        float exposure = 4.5f;
        float gamma = 2.2f;
    } uboParams;

    struct {
        vk::Pipeline skybox;
        vk::Pipeline pbr;
    } pipelines;

    struct {
        vk::DescriptorSet object;
        vk::DescriptorSet skybox;
    } descriptorSets;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSetLayout descriptorSetLayout;

    // Default materials to select from
    std::vector<Material> materials;
    int32_t materialIndex = 0;

    std::vector<std::string> materialNames;
    std::vector<std::string> objectNames;

    VulkanExample() {
        title = "PBR with image based lighting";

        camera.type = Camera::CameraType::firstperson;
        camera.movementSpeed = 4.0f;
        camera.setPerspective(60.0f, (float)size.width / (float)size.height, 0.1f, 256.0f);
        camera.rotationSpeed = 0.25f;

        camera.setRotation({ -3.75f, 180.0f, 0.0f });
        camera.setPosition({ 0.55f, 0.85f, 12.0f });

        // Setup some default materials (source: https://seblagarde.wordpress.com/2011/08/17/feeding-a-physical-based-lighting-mode/)
        materials.push_back(Material("Gold", glm::vec3(1.0f, 0.765557f, 0.336057f)));
        materials.push_back(Material("Copper", glm::vec3(0.955008f, 0.637427f, 0.538163f)));
        materials.push_back(Material("Chromium", glm::vec3(0.549585f, 0.556114f, 0.554256f)));
        materials.push_back(Material("Nickel", glm::vec3(0.659777f, 0.608679f, 0.525649f)));
        materials.push_back(Material("Titanium", glm::vec3(0.541931f, 0.496791f, 0.449419f)));
        materials.push_back(Material("Cobalt", glm::vec3(0.662124f, 0.654864f, 0.633732f)));
        materials.push_back(Material("Platinum", glm::vec3(0.672411f, 0.637331f, 0.585456f)));
        // Testing materials
        materials.push_back(Material("White", glm::vec3(1.0f)));
        materials.push_back(Material("Dark", glm::vec3(0.1f)));
        materials.push_back(Material("Black", glm::vec3(0.0f)));
        materials.push_back(Material("Red", glm::vec3(1.0f, 0.0f, 0.0f)));
        materials.push_back(Material("Blue", glm::vec3(0.0f, 0.0f, 1.0f)));

        settings.overlay = true;

        for (auto material : materials) {
            materialNames.push_back(material.name);
        }
        objectNames = { "Sphere", "Teapot", "Torusknot", "Venus" };

        materialIndex = 9;
    }

    ~VulkanExample() {
        device.destroyPipeline(pipelines.skybox, nullptr);
        device.destroyPipeline(pipelines.pbr, nullptr);

        device.destroyPipelineLayout(pipelineLayout, nullptr);
        device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

        for (auto& model : models.objects) {
            model.destroy();
        }
        models.skybox.destroy();

        uniformBuffers.object.destroy();
        uniformBuffers.skybox.destroy();
        uniformBuffers.params.destroy();

        textures.environmentCube.destroy();
        textures.irradianceCube.destroy();
        textures.prefilteredCube.destroy();
        textures.lutBrdf.destroy();
    }

    virtual void getEnabledFeatures() {
        if (context.deviceFeatures.samplerAnisotropy) {
            context.enabledFeatures.samplerAnisotropy = VK_TRUE;
        }
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& commandBuffer) override {
        commandBuffer.setViewport(0, viewport());
        commandBuffer.setScissor(0, scissor());
        vk::DeviceSize offsets[1] = { 0 };

        // Skybox
        if (displaySkybox) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, 1, &descriptorSets.skybox, 0, NULL);
            commandBuffer.bindVertexBuffers(0, 1, &models.skybox.vertices.buffer, offsets);
            commandBuffer.bindIndexBuffer(models.skybox.indices.buffer, 0, vk::IndexType::eUint32);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skybox);
            commandBuffer.drawIndexed(models.skybox.indexCount, 1, 0, 0, 0);
        }

        // Objects
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, 1, &descriptorSets.object, 0, NULL);
        commandBuffer.bindVertexBuffers(0, 1, &models.objects[models.objectIndex].vertices.buffer, offsets);
        commandBuffer.bindIndexBuffer(models.objects[models.objectIndex].indices.buffer, 0, vk::IndexType::eUint32);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.pbr);

        Material mat = materials[materialIndex];

#define SINGLE_ROW 1
#ifdef SINGLE_ROW
        uint32_t objcount = 10;
        for (uint32_t x = 0; x < objcount; x++) {
            glm::vec3 pos = glm::vec3(float(x - (objcount / 2.0f)) * 2.15f, 0.0f, 0.0f);
            mat.params.roughness = 1.0f - glm::clamp((float)x / (float)objcount, 0.005f, 1.0f);
            mat.params.metallic = glm::clamp((float)x / (float)objcount, 0.005f, 1.0f);
            commandBuffer.pushConstants<glm::vec3>(pipelineLayout, vSS::eVertex, 0, pos);
            commandBuffer.pushConstants<Material::PushBlock>(pipelineLayout, vSS::eFragment, sizeof(glm::vec3), mat.params);
            commandBuffer.drawIndexed(models.objects[models.objectIndex].indexCount, 1, 0, 0, 0);
        }
#else
        for (uint32_t y = 0; y < GRID_DIM; y++) {
            mat.params.metallic = (float)y / (float)(GRID_DIM);
            for (uint32_t x = 0; x < GRID_DIM; x++) {
                glm::vec3 pos = glm::vec3(float(x - (GRID_DIM / 2.0f)) * 2.5f, 0.0f, float(y - (GRID_DIM / 2.0f)) * 2.5f);
                mat.params.roughness = glm::clamp((float)x / (float)(GRID_DIM), 0.05f, 1.0f);
                commandBuffer.pushConstants<glm::vec3>(pipelineLayout, vSS::eVertex, 0, pos);
                commandBuffer.pushConstants<Material::PushBlock>(pipelineLayout, vSS::eFragment, sizeof(glm::vec3), mat.params);
                commandBuffer.drawIndexed(models.objects[models.objectIndex].indexCount, 1, 0, 0, 0);
            }
        }
#endif
    }

    void loadAssets() {
        textures.environmentCube.loadFromFile(context, getAssetPath() + "textures/hdr/pisa_cube.ktx", vF::eR16G16B16A16Sfloat);
        // Skybox
        models.skybox.loadFromFile(context, getAssetPath() + "models/cube.obj", vertexLayout, 1.0f);
        // Objects
        const std::vector<std::string> filenames = { "geosphere.obj", "teapot.dae", "torusknot.obj", "venus.fbx" };
        auto modelCount = filenames.size();
        models.objects.resize(modelCount);
        for (size_t i = 0; i < modelCount; ++i) {
            auto& model = models.objects[i];
            const auto& file = filenames[i];
            model.loadFromFile(context, getAssetPath() + "models/" + file, vertexLayout, 0.05f * (file == "venus.fbx" ? 3.0f : 1.0f));
        }
    }

    void setupDescriptors() {
        // Descriptor Pool
        std::vector<vk::DescriptorPoolSize> poolSizes{
            { vDT::eUniformBuffer, 4 },
            { vDT::eCombinedImageSampler, 6 },
        };

        descriptorPool = device.createDescriptorPool({ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });

        // Descriptor set layout
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            { 0, vDT::eUniformBuffer, 1, vSS::eVertex | vSS::eFragment }, { 1, vDT::eUniformBuffer, 1, vSS::eFragment },
            { 2, vDT::eCombinedImageSampler, 1, vSS::eFragment },         { 3, vDT::eCombinedImageSampler, 1, vSS::eFragment },
            { 4, vDT::eCombinedImageSampler, 1, vSS::eFragment },
        };
        descriptorSetLayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });

        // Descriptor sets
        vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, 1, &descriptorSetLayout };
        // Objects
        descriptorSets.object = device.allocateDescriptorSets(allocInfo)[0];
        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            { descriptorSets.object, 0, 0, 1, vDT::eUniformBuffer, nullptr, &uniformBuffers.object.descriptor },
            { descriptorSets.object, 1, 0, 1, vDT::eUniformBuffer, nullptr, &uniformBuffers.params.descriptor },
            { descriptorSets.object, 2, 0, 1, vDT::eCombinedImageSampler, &textures.irradianceCube.descriptor },
            { descriptorSets.object, 3, 0, 1, vDT::eCombinedImageSampler, &textures.lutBrdf.descriptor },
            { descriptorSets.object, 4, 0, 1, vDT::eCombinedImageSampler, &textures.prefilteredCube.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);

        // Sky box
        descriptorSets.skybox = device.allocateDescriptorSets(allocInfo)[0];
        writeDescriptorSets = {
            { descriptorSets.skybox, 0, 0, 1, vDT::eUniformBuffer, nullptr, &uniformBuffers.skybox.descriptor },
            { descriptorSets.skybox, 1, 0, 1, vDT::eUniformBuffer, nullptr, &uniformBuffers.params.descriptor },
            { descriptorSets.skybox, 2, 0, 1, vDT::eCombinedImageSampler, &textures.environmentCube.descriptor },
        };
        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        // Push constant ranges
        std::vector<vk::PushConstantRange> pushConstantRanges{
            { vSS::eVertex, 0, sizeof(glm::vec3) },
            { vSS::eFragment, sizeof(glm::vec3), sizeof(Material::PushBlock) },
        };
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout, (uint32_t)pushConstantRanges.size(), pushConstantRanges.data() });

        // Pipelines
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayout, renderPass };
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelineBuilder.depthStencilState = { false };
        // Vertex bindings and attributes
        pipelineBuilder.vertexInputState.appendVertexLayout(vertexLayout);
        // Skybox pipeline (background cube)
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/skybox.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/skybox.frag.spv", vSS::eFragment);
        pipelines.skybox = pipelineBuilder.create(context.pipelineCache);

        pipelineBuilder.destroyShaderModules();

        // PBR pipeline
        // Enable depth test and write
        pipelineBuilder.depthStencilState = { true };
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/pbribl.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/pbribl.frag.spv", vSS::eFragment);
        pipelines.pbr = pipelineBuilder.create(context.pipelineCache);
    }

    // Generate a BRDF integration map used as a look-up-table (stores roughness / NdotV)
    void generateBRDFLUT() {
        auto tStart = std::chrono::high_resolution_clock::now();

        const vk::Format format = vF::eR16G16Sfloat;  // R16G16 is supported pretty much everywhere
        const int32_t dim = 512;

        // Image
        vk::ImageCreateInfo imageCI;
        imageCI.imageType = vIT::e2D;
        imageCI.format = format;
        imageCI.extent.width = dim;
        imageCI.extent.height = dim;
        imageCI.extent.depth = 1;
        imageCI.mipLevels = 1;
        imageCI.arrayLayers = 1;
        imageCI.usage = vIU::eColorAttachment | vIU::eSampled;
        (vks::Image&)textures.lutBrdf = context.createImage(imageCI);
        // Image view
        vk::ImageViewCreateInfo viewCI;
        viewCI.viewType = vIVT::e2D;
        viewCI.format = format;
        viewCI.subresourceRange = {};
        viewCI.subresourceRange.aspectMask = vIA::eColor;
        viewCI.subresourceRange.levelCount = 1;
        viewCI.subresourceRange.layerCount = 1;
        viewCI.image = textures.lutBrdf.image;
        textures.lutBrdf.view = device.createImageView(viewCI);
        // Sampler
        vk::SamplerCreateInfo samplerCI;
        samplerCI.magFilter = vk::Filter::eLinear;
        samplerCI.minFilter = vk::Filter::eLinear;
        samplerCI.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerCI.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerCI.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerCI.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerCI.maxLod = 1.0f;
        samplerCI.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        textures.lutBrdf.sampler = device.createSampler(samplerCI);

        textures.lutBrdf.descriptor.imageView = textures.lutBrdf.view;
        textures.lutBrdf.descriptor.sampler = textures.lutBrdf.sampler;
        textures.lutBrdf.descriptor.imageLayout = vIL::eShaderReadOnlyOptimal;
        textures.lutBrdf.device = device;

        // FB, Att, RP, Pipe, etc.
        vk::RenderPass renderpass;
        {
            vk::AttachmentDescription attDesc;
            // Color attachment
            attDesc.format = format;
            attDesc.loadOp = vk::AttachmentLoadOp::eClear;
            attDesc.storeOp = vk::AttachmentStoreOp::eStore;
            attDesc.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attDesc.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attDesc.initialLayout = vIL::eUndefined;
            attDesc.finalLayout = vIL::eShaderReadOnlyOptimal;
            vk::AttachmentReference colorReference { 0, vIL::eColorAttachmentOptimal };

            vk::SubpassDescription subpassDescription = {};
            subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpassDescription.colorAttachmentCount = 1;
            subpassDescription.pColorAttachments = &colorReference;

            // Use subpass dependencies for layout transitions
            std::array<vk::SubpassDependency, 2> dependencies{
                vk::SubpassDependency{ VK_SUBPASS_EXTERNAL, 0, vPS::eBottomOfPipe, vPS::eColorAttachmentOutput, vAF::eMemoryRead,
                vAF::eColorAttachmentRead | vAF::eColorAttachmentWrite, vk::DependencyFlagBits::eByRegion },
                vk::SubpassDependency{ 0, VK_SUBPASS_EXTERNAL, vPS::eColorAttachmentOutput, vPS::eBottomOfPipe,
                vAF::eColorAttachmentRead | vAF::eColorAttachmentWrite, vAF::eMemoryRead, vk::DependencyFlagBits::eByRegion },
            };


            // Create the actual renderpass
            vk::RenderPassCreateInfo renderPassCI;
            renderPassCI.attachmentCount = 1;
            renderPassCI.pAttachments = &attDesc;
            renderPassCI.subpassCount = 1;
            renderPassCI.pSubpasses = &subpassDescription;
            renderPassCI.dependencyCount = 2;
            renderPassCI.pDependencies = dependencies.data();
            renderpass = device.createRenderPass(renderPassCI);
        }

        vk::Framebuffer framebuffer;
        {
            vk::FramebufferCreateInfo framebufferCI;
            framebufferCI.renderPass = renderpass;
            framebufferCI.attachmentCount = 1;
            framebufferCI.pAttachments = &textures.lutBrdf.view;
            framebufferCI.width = dim;
            framebufferCI.height = dim;
            framebufferCI.layers = 1;
            framebuffer = device.createFramebuffer(framebufferCI);
        }

        // Desriptors
        vk::DescriptorSetLayout descriptorsetlayout = device.createDescriptorSetLayout({});

        // Descriptor Pool
        std::vector<vk::DescriptorPoolSize> poolSizes{ { vDT::eCombinedImageSampler, 1 } };
        vk::DescriptorPool descriptorpool = device.createDescriptorPool({ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });

        // Descriptor sets
        vk::DescriptorSet descriptorset = device.allocateDescriptorSets({ descriptorpool, 1, &descriptorsetlayout })[0];

        // Pipeline layout
        vk::PipelineLayout pipelinelayout = device.createPipelineLayout({ {}, 1, &descriptorsetlayout });

        // Pipeline
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelinelayout, renderpass };
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelineBuilder.depthStencilState = { false };
        // Look-up-table (from BRDF) pipeline
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/genbrdflut.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/genbrdflut.frag.spv", vSS::eFragment);
        vk::Pipeline pipeline = pipelineBuilder.create(context.pipelineCache);

        // Render
        vk::ClearValue clearValues[1];
        clearValues[0].color = vks::util::clearColor({ 0.0f, 0.0f, 0.0f, 1.0f });

        vk::RenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.renderPass = renderpass;
        renderPassBeginInfo.renderArea.extent.width = dim;
        renderPassBeginInfo.renderArea.extent.height = dim;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = clearValues;
        renderPassBeginInfo.framebuffer = framebuffer;

        context.withPrimaryCommandBuffer([&](const vk::CommandBuffer& cmdBuf) {
            cmdBuf.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            vk::Viewport viewport{ 0, 0, (float)dim, (float)dim, 0, 1 };
            vk::Rect2D scissor{ vk::Offset2D{}, vk::Extent2D{ (uint32_t)dim, (uint32_t)dim } };
            cmdBuf.setViewport(0, viewport);
            cmdBuf.setScissor(0, scissor);
            cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            cmdBuf.draw(3, 1, 0, 0);
            cmdBuf.endRenderPass();
        });
        queue.waitIdle();

        vkQueueWaitIdle(queue);

        // todo: cleanup
        device.destroyPipeline(pipeline);
        device.destroyPipelineLayout(pipelinelayout);
        device.destroyRenderPass(renderpass);
        device.destroyFramebuffer(framebuffer);
        device.destroyDescriptorSetLayout(descriptorsetlayout);
        device.destroyDescriptorPool(descriptorpool);

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        std::cout << "Generating BRDF LUT took " << tDiff << " ms" << std::endl;
    }

    // Generate an irradiance cube map from the environment cube map
    void generateIrradianceCube() {
        auto tStart = std::chrono::high_resolution_clock::now();

        const vk::Format format = vF::eR32G32B32A32Sfloat;
        const int32_t dim = 64;
        const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

        {
            // Pre-filtered cube map
            // Image
            vk::ImageCreateInfo imageCI;
            imageCI.imageType = vIT::e2D;
            imageCI.format = format;
            imageCI.extent.width = dim;
            imageCI.extent.height = dim;
            imageCI.extent.depth = 1;
            imageCI.mipLevels = numMips;
            imageCI.arrayLayers = 6;
            imageCI.usage = vIU::eSampled | vIU::eTransferDst;
            imageCI.flags = vk::ImageCreateFlagBits::eCubeCompatible;

            (vks::Image&)textures.irradianceCube = context.createImage(imageCI);

            // Image view
            vk::ImageViewCreateInfo viewCI;
            viewCI.viewType = vIVT::eCube;
            viewCI.format = format;
            viewCI.subresourceRange.aspectMask = vIA::eColor;
            viewCI.subresourceRange.levelCount = numMips;
            viewCI.subresourceRange.layerCount = 6;
            viewCI.image = textures.irradianceCube.image;
            textures.irradianceCube.view = device.createImageView(viewCI);
            // Sampler
            vk::SamplerCreateInfo samplerCI;
            samplerCI.magFilter = vk::Filter::eLinear;
            samplerCI.minFilter = vk::Filter::eLinear;
            samplerCI.mipmapMode = vk::SamplerMipmapMode::eLinear;
            samplerCI.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerCI.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerCI.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            samplerCI.maxLod = static_cast<float>(numMips);
            samplerCI.borderColor = vk::BorderColor::eFloatOpaqueWhite;
            textures.irradianceCube.sampler = device.createSampler(samplerCI);

            textures.irradianceCube.descriptor.imageView = textures.irradianceCube.view;
            textures.irradianceCube.descriptor.sampler = textures.irradianceCube.sampler;
            textures.irradianceCube.descriptor.imageLayout = vIL::eShaderReadOnlyOptimal;
            textures.irradianceCube.device = device;
        }

        vk::RenderPass renderpass;
        {
            // FB, Att, RP, Pipe, etc.
            vk::AttachmentDescription attDesc = {};
            // Color attachment
            attDesc.format = format;
            attDesc.loadOp = vk::AttachmentLoadOp::eClear;
            attDesc.storeOp = vk::AttachmentStoreOp::eStore;
            attDesc.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attDesc.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attDesc.initialLayout = vIL::eUndefined;
            attDesc.finalLayout = vIL::eColorAttachmentOptimal;
            vk::AttachmentReference colorReference{ 0, vIL::eColorAttachmentOptimal };
            vk::SubpassDescription subpassDescription{ {}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &colorReference };

            // Use subpass dependencies for layout transitions
            std::array<vk::SubpassDependency, 2> dependencies{
                vk::SubpassDependency{ VK_SUBPASS_EXTERNAL, 0, vPS::eBottomOfPipe, vPS::eColorAttachmentOutput, vAF::eMemoryRead,
                                       vAF::eColorAttachmentRead | vAF::eColorAttachmentWrite, vk::DependencyFlagBits::eByRegion },
                vk::SubpassDependency{ 0, VK_SUBPASS_EXTERNAL, vPS::eColorAttachmentOutput, vPS::eBottomOfPipe,
                                       vAF::eColorAttachmentRead | vAF::eColorAttachmentWrite, vAF::eMemoryRead, vk::DependencyFlagBits::eByRegion },
            };

            // Renderpass
            vk::RenderPassCreateInfo renderPassCI;
            renderPassCI.attachmentCount = 1;
            renderPassCI.pAttachments = &attDesc;
            renderPassCI.subpassCount = 1;
            renderPassCI.pSubpasses = &subpassDescription;
            renderPassCI.dependencyCount = 2;
            renderPassCI.pDependencies = dependencies.data();

            renderpass = device.createRenderPass(renderPassCI);
        }

        struct {
            vks::Image image;
            vk::Framebuffer framebuffer;
        } offscreen;

        // Offfscreen framebuffer
        {
            // Color attachment
            vk::ImageCreateInfo imageCreateInfo;
            imageCreateInfo.imageType = vIT::e2D;
            imageCreateInfo.format = format;
            imageCreateInfo.extent.width = dim;
            imageCreateInfo.extent.height = dim;
            imageCreateInfo.extent.depth = 1;
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.usage = vIU::eColorAttachment | vIU::eTransferSrc;
            offscreen.image = context.createImage(imageCreateInfo);

            vk::ImageViewCreateInfo colorImageView;
            colorImageView.viewType = vk::ImageViewType::e2D;
            colorImageView.format = format;
            colorImageView.subresourceRange.aspectMask = vIA::eColor;
            colorImageView.subresourceRange.levelCount = 1;
            colorImageView.subresourceRange.layerCount = 1;
            colorImageView.image = offscreen.image.image;
            offscreen.image.view = device.createImageView(colorImageView);

            vk::FramebufferCreateInfo fbufCreateInfo;
            fbufCreateInfo.renderPass = renderpass;
            fbufCreateInfo.attachmentCount = 1;
            fbufCreateInfo.pAttachments = &offscreen.image.view;
            fbufCreateInfo.width = dim;
            fbufCreateInfo.height = dim;
            fbufCreateInfo.layers = 1;
            offscreen.framebuffer = device.createFramebuffer(fbufCreateInfo);
            context.setImageLayout(offscreen.image.image, vIA::eColor, vIL::eUndefined, vIL::eColorAttachmentOptimal);
        }

        // Descriptors
        vk::DescriptorSetLayout descriptorsetlayout;
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            { 0, vDT::eCombinedImageSampler, 1, vSS::eFragment },
        };
        descriptorsetlayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });

        // Descriptor Pool
        std::vector<vk::DescriptorPoolSize> poolSizes{
            { vDT::eCombinedImageSampler, 1 },
        };
        vk::DescriptorPool descriptorpool = device.createDescriptorPool({ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
        // Descriptor sets
        vk::DescriptorSet descriptorset = device.allocateDescriptorSets({ descriptorpool, 1, &descriptorsetlayout })[0];
        device.updateDescriptorSets(vk::WriteDescriptorSet{ descriptorset, 0, 0, 1, vDT::eCombinedImageSampler, &textures.environmentCube.descriptor },
                                    nullptr);

        // Pipeline layout
        struct PushBlock {
            glm::mat4 mvp;
            // Sampling deltas
            float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
            float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
        } pushBlock;
        vk::PushConstantRange pushConstantRange{ vSS::eVertex | vSS::eFragment, 0, sizeof(PushBlock) };

        vk::PipelineLayout pipelinelayout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, 1, &descriptorsetlayout, 1, &pushConstantRange });

        // Pipeline
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelinelayout, renderpass };
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelineBuilder.depthStencilState = { false };
        pipelineBuilder.vertexInputState.bindingDescriptions = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        pipelineBuilder.vertexInputState.attributeDescriptions = {
            { 0, 0, vF::eR32G32B32Sfloat, 0 },
        };

        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/filtercube.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/irradiancecube.frag.spv", vSS::eFragment);
        vk::Pipeline pipeline = pipelineBuilder.create(context.pipelineCache);

        // Render
        vk::ClearValue clearValues[1];
        clearValues[0].color = vks::util::clearColor({ 0.0f, 0.0f, 0.2f, 0.0f });

        vk::RenderPassBeginInfo renderPassBeginInfo{ renderpass, offscreen.framebuffer, vk::Rect2D{ vk::Offset2D{}, vk::Extent2D{ (uint32_t)dim, (uint32_t)dim } }, 1, clearValues };

        const std::vector<glm::mat4> matrices = {
            // POSITIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        context.withPrimaryCommandBuffer([&](const vk::CommandBuffer& cmdBuf) {
            vk::Viewport viewport{ 0, 0, (float)dim, (float)dim, 0.0f, 1.0f };
            vk::Rect2D scissor{ vk::Offset2D{}, vk::Extent2D{ (uint32_t)dim, (uint32_t)dim } };

            cmdBuf.setViewport(0, viewport);
            cmdBuf.setScissor(0, scissor);

            vk::ImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = vIA::eColor;
            subresourceRange.levelCount = numMips;
            subresourceRange.layerCount = 6;

            // Change image layout for all cubemap faces to transfer destination
            context.setImageLayout(cmdBuf, textures.irradianceCube.image, vIL::eUndefined, vIL::eTransferDstOptimal, subresourceRange);

            for (uint32_t m = 0; m < numMips; m++) {
                for (uint32_t f = 0; f < 6; f++) {
                    viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
                    viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
                    cmdBuf.setViewport(0, 1, &viewport);
                    // Render scene from cube face's point of view
                    cmdBuf.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

                    // Update shader push constant block
                    pushBlock.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];

                    cmdBuf.pushConstants<PushBlock>(pipelinelayout, vSS::eVertex | vSS::eFragment, 0, pushBlock);

                    cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
                    cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelinelayout, 0, descriptorset, nullptr);

                    std::vector<vk::DeviceSize> offsets{ { 0 } };

                    cmdBuf.bindVertexBuffers(0, models.skybox.vertices.buffer, offsets);
                    cmdBuf.bindIndexBuffer(models.skybox.indices.buffer, 0, vk::IndexType::eUint32);
                    cmdBuf.drawIndexed(models.skybox.indexCount, 1, 0, 0, 0);

                    cmdBuf.endRenderPass();

                    context.setImageLayout(cmdBuf, offscreen.image.image, vIA::eColor, vIL::eColorAttachmentOptimal, vIL::eTransferSrcOptimal);

                    // Copy region for transfer from framebuffer to cube face
                    vk::ImageCopy copyRegion;
                    copyRegion.srcSubresource.aspectMask = vIA::eColor;
                    copyRegion.srcSubresource.baseArrayLayer = 0;
                    copyRegion.srcSubresource.mipLevel = 0;
                    copyRegion.srcSubresource.layerCount = 1;
                    copyRegion.srcOffset = { 0, 0, 0 };
                    copyRegion.dstSubresource.aspectMask = vIA::eColor;
                    copyRegion.dstSubresource.baseArrayLayer = f;
                    copyRegion.dstSubresource.mipLevel = m;
                    copyRegion.dstSubresource.layerCount = 1;
                    copyRegion.dstOffset = { 0, 0, 0 };
                    copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
                    copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
                    copyRegion.extent.depth = 1;

                    cmdBuf.copyImage(offscreen.image.image, vIL::eTransferSrcOptimal, textures.irradianceCube.image, vIL::eTransferDstOptimal, copyRegion);

                    // Transform framebuffer color attachment back
                    context.setImageLayout(cmdBuf, offscreen.image.image, vIA::eColor, vIL::eTransferSrcOptimal, vIL::eColorAttachmentOptimal);
                }
            }
            context.setImageLayout(cmdBuf, textures.irradianceCube.image, vIL::eTransferDstOptimal, vIL::eShaderReadOnlyOptimal, subresourceRange);
        });

        // todo: cleanup
        device.destroyRenderPass(renderpass, nullptr);
        device.destroyFramebuffer(offscreen.framebuffer, nullptr);
        offscreen.image.destroy();
        device.destroyDescriptorPool(descriptorpool, nullptr);
        device.destroyDescriptorSetLayout(descriptorsetlayout, nullptr);
        device.destroyPipeline(pipeline, nullptr);
        device.destroyPipelineLayout(pipelinelayout, nullptr);

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        std::cout << "Generating irradiance cube with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
    }

    // Prefilter environment cubemap
    // See https://placeholderart.wordpress.com/2015/07/28/implementation-notes-runtime-environment-map-filtering-for-image-based-lighting/
    void generatePrefilteredCube() {
        auto tStart = std::chrono::high_resolution_clock::now();

        const vk::Format format = vF::eR16G16B16A16Sfloat;
        const int32_t dim = 512;
        const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

        // Pre-filtered cube map
        // Image
        {
            vk::ImageCreateInfo imageCI;
            imageCI.imageType = vIT::e2D;
            imageCI.format = format;
            imageCI.extent.width = dim;
            imageCI.extent.height = dim;
            imageCI.extent.depth = 1;
            imageCI.mipLevels = numMips;
            imageCI.arrayLayers = 6;
            imageCI.usage = vIU::eSampled | vIU::eTransferDst;
            imageCI.flags = vk::ImageCreateFlagBits::eCubeCompatible;
            (vks::Image&)textures.prefilteredCube = context.createImage(imageCI);
            // Image view
            vk::ImageViewCreateInfo viewCI;
            viewCI.viewType = vIVT::eCube;
            viewCI.format = format;
            viewCI.subresourceRange.aspectMask = vIA::eDepth;
            viewCI.subresourceRange.levelCount = numMips;
            viewCI.subresourceRange.layerCount = 6;
            viewCI.image = textures.prefilteredCube.image;
            textures.prefilteredCube.view = device.createImageView(viewCI);
            // Sampler
            vk::SamplerCreateInfo samplerCI;
            samplerCI.magFilter = vk::Filter::eLinear;
            samplerCI.minFilter = vk::Filter::eLinear;
            samplerCI.mipmapMode = vk::SamplerMipmapMode::eLinear;
            samplerCI.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerCI.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerCI.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            samplerCI.maxLod = static_cast<float>(numMips);
            samplerCI.borderColor = vk::BorderColor::eFloatOpaqueWhite;
            textures.prefilteredCube.sampler = device.createSampler(samplerCI);

            textures.prefilteredCube.descriptor.imageView = textures.prefilteredCube.view;
            textures.prefilteredCube.descriptor.sampler = textures.prefilteredCube.sampler;
            textures.prefilteredCube.descriptor.imageLayout = vIL::eShaderReadOnlyOptimal;
            textures.prefilteredCube.device = device;
        }

        vk::RenderPass renderpass;
        {
            // FB, Att, RP, Pipe, etc.
            vk::AttachmentDescription attDesc = {};
            // Color attachment
            attDesc.format = format;
            attDesc.loadOp = vk::AttachmentLoadOp::eClear;
            attDesc.storeOp = vk::AttachmentStoreOp::eStore;
            attDesc.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attDesc.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attDesc.initialLayout = vIL::eUndefined;
            attDesc.finalLayout = vIL::eColorAttachmentOptimal;
            vk::AttachmentReference colorReference{ 0, vIL::eColorAttachmentOptimal };

            vk::SubpassDescription subpassDescription = {};
            subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpassDescription.colorAttachmentCount = 1;
            subpassDescription.pColorAttachments = &colorReference;

            // Use subpass dependencies for layout transitions
            std::array<vk::SubpassDependency, 2> dependencies{
                vk::SubpassDependency{ VK_SUBPASS_EXTERNAL, 0, vPS::eBottomOfPipe, vPS::eColorAttachmentOutput, vAF::eMemoryRead,
                                       vAF::eColorAttachmentRead | vAF::eColorAttachmentWrite, vk::DependencyFlagBits::eByRegion },
                vk::SubpassDependency{ 0, VK_SUBPASS_EXTERNAL, vPS::eColorAttachmentOutput, vPS::eBottomOfPipe,
                                       vAF::eColorAttachmentRead | vAF::eColorAttachmentWrite, vAF::eMemoryRead, vk::DependencyFlagBits::eByRegion },
            };

            // Renderpass
            renderpass = device.createRenderPass({ {}, 1, &attDesc, 1, &subpassDescription, (uint32_t)dependencies.size(), dependencies.data() });
        }

        struct {
            vks::Image image;
            vk::Framebuffer framebuffer;
        } offscreen;

        // Offfscreen framebuffer
        {
            // Color attachment
            vk::ImageCreateInfo imageCreateInfo;
            imageCreateInfo.imageType = vIT::e2D;
            imageCreateInfo.format = format;
            imageCreateInfo.extent.width = dim;
            imageCreateInfo.extent.height = dim;
            imageCreateInfo.extent.depth = 1;
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.usage = vIU::eColorAttachment | vIU::eTransferSrc;
            offscreen.image = context.createImage(imageCreateInfo);

            vk::ImageViewCreateInfo colorImageView;
            colorImageView.viewType = vIVT::e2D;
            colorImageView.format = format;
            colorImageView.subresourceRange.aspectMask = vIA::eColor;
            colorImageView.subresourceRange.levelCount = 1;
            colorImageView.subresourceRange.layerCount = 1;
            colorImageView.image = offscreen.image.image;
            offscreen.image.view = device.createImageView(colorImageView);

            vk::FramebufferCreateInfo fbufCreateInfo;
            fbufCreateInfo.renderPass = renderpass;
            fbufCreateInfo.attachmentCount = 1;
            fbufCreateInfo.pAttachments = &offscreen.image.view;
            fbufCreateInfo.width = dim;
            fbufCreateInfo.height = dim;
            fbufCreateInfo.layers = 1;
            offscreen.framebuffer = device.createFramebuffer(fbufCreateInfo);
            context.setImageLayout(offscreen.image.image, vIA::eColor, vIL::eUndefined, vIL::eColorAttachmentOptimal);
        }

        // Descriptors
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
            { 0, vDT::eCombinedImageSampler, 1, vSS::eFragment },
        };
        vk::DescriptorSetLayout descriptorsetlayout = device.createDescriptorSetLayout({ {}, (uint32_t)setLayoutBindings.size(), setLayoutBindings.data() });

        // Descriptor Pool
        std::vector<vk::DescriptorPoolSize> poolSizes{
            { vDT::eCombinedImageSampler, 1 },
        };
        vk::DescriptorPool descriptorpool = device.createDescriptorPool({ {}, 2, (uint32_t)poolSizes.size(), poolSizes.data() });
        // Descriptor sets
        vk::DescriptorSet descriptorset = device.allocateDescriptorSets({ descriptorpool, 1, &descriptorsetlayout })[0];
        device.updateDescriptorSets(vk::WriteDescriptorSet{ descriptorset, 0, 0, 1, vDT::eCombinedImageSampler, &textures.environmentCube.descriptor },
                                    nullptr);

        // Pipeline layout
        struct PushBlock {
            glm::mat4 mvp;
            float roughness;
            uint32_t numSamples = 32u;
        } pushBlock;

        vk::PushConstantRange pushConstantRange{ vSS::eVertex | vSS::eFragment, 0, sizeof(PushBlock) };
        vk::PipelineLayout pipelinelayout = device.createPipelineLayout({ {}, 1, &descriptorsetlayout, 1, &pushConstantRange });

        // Pipeline
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelinelayout, renderpass };
        pipelineBuilder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        pipelineBuilder.depthStencilState = { false };
        pipelineBuilder.vertexInputState.bindingDescriptions = {
            { 0, vertexLayout.stride(), vk::VertexInputRate::eVertex },
        };
        pipelineBuilder.vertexInputState.attributeDescriptions = {
            { 0, 0, vF::eR32G32B32Sfloat, 0 },
        };

        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/filtercube.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/prefilterenvmap.frag.spv", vSS::eFragment);
        vk::Pipeline pipeline = pipelineBuilder.create(context.pipelineCache);

        // Render

        vk::ClearValue clearValues[1];
        clearValues[0].color = vks::util::clearColor({ 0.0f, 0.0f, 0.2f, 0.0f });

        vk::RenderPassBeginInfo renderPassBeginInfo{ renderpass, offscreen.framebuffer, { vk::Offset2D{}, vk::Extent2D{ (uint32_t)dim, (uint32_t)dim } }, 1, clearValues };
        // Reuse render pass from example pass

        const std::vector<glm::mat4> matrices = {
            // POSITIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_X
            glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Y
            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // POSITIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
            // NEGATIVE_Z
            glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        };

        context.withPrimaryCommandBuffer([&](const vk::CommandBuffer& cmdBuf) {
            vk::Viewport viewport{ 0, 0, (float)dim, (float)dim, 0, 1 };
            vk::Rect2D scissor{ vk::Offset2D{}, vk::Extent2D{ (uint32_t)dim, (uint32_t)dim } };
            cmdBuf.setViewport(0, viewport);
            cmdBuf.setScissor(0, scissor);
            vk::ImageSubresourceRange subresourceRange{ vIA::eColor, 0, numMips, 0, 6 };
            // Change image layout for all cubemap faces to transfer destination
            context.setImageLayout(cmdBuf, textures.prefilteredCube.image, vIL::eUndefined, vIL::eTransferDstOptimal, subresourceRange);

            for (uint32_t m = 0; m < numMips; m++) {
                pushBlock.roughness = (float)m / (float)(numMips - 1);
                for (uint32_t f = 0; f < 6; f++) {
                    viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
                    viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
                    cmdBuf.setViewport(0, viewport);

                    // Render scene from cube face's point of view
                    cmdBuf.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

                    // Update shader push constant block
                    pushBlock.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * matrices[f];

                    cmdBuf.pushConstants<PushBlock>(pipelinelayout, vSS::eVertex | vSS::eFragment, 0, pushBlock);
                    cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
                    cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelinelayout, 0, descriptorset, nullptr);

                    std::vector<vk::DeviceSize> offsets{ { 0 } };
                    cmdBuf.bindVertexBuffers(0, models.skybox.vertices.buffer, offsets);
                    cmdBuf.bindIndexBuffer(models.skybox.indices.buffer, 0, vk::IndexType::eUint32);
                    cmdBuf.drawIndexed(models.skybox.indexCount, 1, 0, 0, 0);

                    cmdBuf.endRenderPass();

                    context.setImageLayout(cmdBuf, offscreen.image.image, vIA::eColor, vIL::eColorAttachmentOptimal, vIL::eTransferSrcOptimal);

                    // Copy region for transfer from framebuffer to cube face
                    vk::ImageCopy copyRegion;
                    copyRegion.srcSubresource.aspectMask = vIA::eColor;
                    copyRegion.srcSubresource.baseArrayLayer = 0;
                    copyRegion.srcSubresource.mipLevel = 0;
                    copyRegion.srcSubresource.layerCount = 1;
                    copyRegion.srcOffset = { 0, 0, 0 };

                    copyRegion.dstSubresource.aspectMask = vIA::eColor;
                    copyRegion.dstSubresource.baseArrayLayer = f;
                    copyRegion.dstSubresource.mipLevel = m;
                    copyRegion.dstSubresource.layerCount = 1;
                    copyRegion.dstOffset = { 0, 0, 0 };

                    copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
                    copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
                    copyRegion.extent.depth = 1;

                    cmdBuf.copyImage(offscreen.image.image, vIL::eTransferSrcOptimal, textures.prefilteredCube.image, vIL::eTransferDstOptimal, copyRegion);
                    // Transform framebuffer color attachment back
                    context.setImageLayout(cmdBuf, offscreen.image.image, vIA::eColor, vIL::eTransferSrcOptimal, vIL::eColorAttachmentOptimal);
                }
            }
            context.setImageLayout(cmdBuf, textures.prefilteredCube.image, vIL::eTransferDstOptimal, vIL::eShaderReadOnlyOptimal, subresourceRange);
        });

        // todo: cleanup
        device.destroyRenderPass(renderpass, nullptr);
        device.destroyFramebuffer(offscreen.framebuffer, nullptr);
        offscreen.image.destroy();
        device.destroyDescriptorPool(descriptorpool, nullptr);
        device.destroyDescriptorSetLayout(descriptorsetlayout, nullptr);
        device.destroyPipeline(pipeline, nullptr);
        device.destroyPipelineLayout(pipelinelayout, nullptr);

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
        std::cout << "Generating pre-filtered enivornment cube with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Objact vertex shader uniform buffer
        uniformBuffers.object = context.createUniformBuffer(uboMatrices);

        // Skybox vertex shader uniform buffer
        uniformBuffers.skybox = context.createUniformBuffer(uboMatrices);

        // Shared parameter uniform buffer
        uniformBuffers.params = context.createUniformBuffer(uboParams);

        updateUniformBuffers();
        updateParams();
    }

    void updateUniformBuffers() {
        // 3D object
        uboMatrices.projection = camera.matrices.perspective;
        uboMatrices.view = camera.matrices.view;
        uboMatrices.model = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f + (models.objectIndex == 1 ? 45.0f : 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
        uboMatrices.camPos = camera.position * -1.0f;
        memcpy(uniformBuffers.object.mapped, &uboMatrices, sizeof(uboMatrices));

        // Skybox
        uboMatrices.model = glm::mat4(glm::mat3(camera.matrices.view));
        memcpy(uniformBuffers.skybox.mapped, &uboMatrices, sizeof(uboMatrices));
    }

    void updateParams() {
        const float p = 15.0f;
        uboParams.lights[0] = glm::vec4(-p, -p * 0.5f, -p, 1.0f);
        uboParams.lights[1] = glm::vec4(-p, -p * 0.5f, p, 1.0f);
        uboParams.lights[2] = glm::vec4(p, -p * 0.5f, p, 1.0f);
        uboParams.lights[3] = glm::vec4(p, -p * 0.5f, -p, 1.0f);

        memcpy(uniformBuffers.params.mapped, &uboParams, sizeof(uboParams));
    }

    void prepare() override {
        ExampleBase::prepare();
        generateBRDFLUT();
        generateIrradianceCube();
        generatePrefilteredCube();
        prepareUniformBuffers();
        setupDescriptors();
        preparePipelines();
        buildCommandBuffers();
        prepared = true;
    }

    void viewChanged() override { updateUniformBuffers(); }

    virtual void OnUpdateUIOverlay() {
        if (ui.header("Settings")) {
            if (ui.comboBox("Material", &materialIndex, materialNames)) {
                buildCommandBuffers();
            }
            if (ui.comboBox("Object type", &models.objectIndex, objectNames)) {
                updateUniformBuffers();
                buildCommandBuffers();
            }
            if (ui.inputFloat("Exposure", &uboParams.exposure, 0.1f, 2)) {
                updateParams();
            }
            if (ui.inputFloat("Gamma", &uboParams.gamma, 0.1f, 2)) {
                updateParams();
            }
            if (ui.checkBox("Skybox", &displaySkybox)) {
                buildCommandBuffers();
            }
        }
    }
};

VULKAN_EXAMPLE_MAIN()
