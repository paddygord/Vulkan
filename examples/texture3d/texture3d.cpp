/*
* Vulkan Example - 3D texture loading (and generation using perlin noise) example
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vkx/vulkanExampleBase.hpp>
#include <vkx/model.hpp>
#include <algorithm>
#include <ranges>

#define VERTEX_BUFFER_BIND_ID 0
#define FRACTAL

// Vertex layout for this example
struct Vertex {
    float pos[3];
    float uv[2];
    float normal[3];
};

// Translation of Ken Perlin's JAVA implementation (http://mrl.nyu.edu/~perlin/noise/)
template <typename T>
class PerlinNoise {
private:
    static constexpr int32_t PERM_COUNT = 512;
    static constexpr int32_t COUNT = 256;
    static constexpr int32_t MASK = 255;

    using LookupArray = std::array<uint8_t, COUNT>;
    using PermutationArray = std::array<uint32_t, PERM_COUNT>;
    PermutationArray permutations;
    T fade(T t) {
        return t * t * t * (t * (t * (T)6 - (T)15) + (T)10);
    }
    T lerp(T t, T a, T b) {
        return a + t * (b - a);
    }
    T grad(int hash, T x, T y, T z) {
        // Convert LO 4 bits of hash code into 12 gradient directions
        int h = hash & 15;
        T u = h < 8 ? x : y;
        T v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

public:
    PerlinNoise() {
        // Generate random lookup for permutations containing all numbers from 0..255
        LookupArray plookup;

        //        std::iota(plookup.begin(), plookup.end(), 0);
        for (auto i = 0; i < 256; ++i) {  //: std::ranges::iota_view{ 0, 256 }) {
            plookup[i] = i;
        }
        std::default_random_engine rndEngine(std::random_device{}());
        std::shuffle(plookup.begin(), plookup.end(), rndEngine);

        for (uint32_t i = 0; i < COUNT; i++) {
            permutations[i] = permutations[COUNT + i] = plookup[i];
        }
    }
    T noise(T x, T y, T z) {
        // Find unit cube that contains point
        int32_t X = (int32_t)floor(x) & MASK;
        int32_t Y = (int32_t)floor(y) & MASK;
        int32_t Z = (int32_t)floor(z) & MASK;
        // Find relative x,y,z of point in cube
        x -= floor(x);
        y -= floor(y);
        z -= floor(z);

        // Compute fade curves for each of x,y,z
        T u = fade(x);
        T v = fade(y);
        T w = fade(z);

        // Hash coordinates of the 8 cube corners
        uint32_t A = permutations[X] + Y;
        uint32_t AA = permutations[A] + Z;
        uint32_t AB = permutations[A + 1] + Z;
        uint32_t B = permutations[X + 1] + Y;
        uint32_t BA = permutations[B] + Z;
        uint32_t BB = permutations[B + 1] + Z;

        // And add blended results for 8 corners of the cube;
        T res = lerp(w,
                     lerp(v, lerp(u, grad(permutations[AA], x, y, z), grad(permutations[BA], x - 1, y, z)),
                          lerp(u, grad(permutations[AB], x, y - 1, z), grad(permutations[BB], x - 1, y - 1, z))),
                     lerp(v, lerp(u, grad(permutations[AA + 1], x, y, z - 1), grad(permutations[BA + 1], x - 1, y, z - 1)),
                          lerp(u, grad(permutations[AB + 1], x, y - 1, z - 1), grad(permutations[BB + 1], x - 1, y - 1, z - 1))));
        return res;
    }
};

// Fractal noise generator based on perlin noise above
template <typename T>
class FractalNoise {
private:
    PerlinNoise<float> perlinNoise;
    uint32_t octaves;
    T frequency;
    T amplitude;
    T persistence;

public:
    FractalNoise(const PerlinNoise<T>& perlinNoise) {
        this->perlinNoise = perlinNoise;
        octaves = 6;
        persistence = (T)0.5;
    }

    T noise(T x, T y, T z) {
        T sum = 0;
        T frequency = (T)1;
        T amplitude = (T)1;
        T max = (T)0;
        for (uint32_t i = 0; i < octaves; i++) {
            sum += perlinNoise.noise(x * frequency, y * frequency, z * frequency) * amplitude;
            max += amplitude;
            amplitude *= persistence;
            frequency *= (T)2;
        }

        sum = sum / max;
        return (sum + (T)1.0) / (T)2.0;
    }
};

class VulkanExample : public vkx::ExampleBase {
    using Parent = vkx::ExampleBase;

public:
    // Contains all Vulkan objects that are required to store and use a 3D texture
    struct Texture : public vks::Image {
        vk::ImageLayout imageLayout;
        vk::DescriptorImageInfo descriptor;
        vk::Extent3D extent;
        //    uint32_t mipLevels;
    } texture;

    struct {
        vkx::model::Model cube;
    } models;

    vks::Buffer vertexBuffer;
    vks::Buffer indexBuffer;
    uint32_t indexCount;

    vks::Buffer uniformBufferVS;

    struct UboVS {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec4 viewPos;
        float depth = 0.0f;
    } uboVS;

    struct {
        vk::Pipeline solid;
    } pipelines;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSet descriptorSet;
    vk::DescriptorSetLayout descriptorSetLayout;

    VulkanExample() {
        zoom = -2.5f;
        rotation = { 0.0f, 15.0f, 0.0f };
        title = "3D textures";
        settings.overlay = true;
        srand((unsigned int)time(NULL));
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note : Inherited destructor cleans up resources stored in base class

        texture.destroy();

        device.destroy(pipelines.solid);

        device.destroy(pipelineLayout);
        device.destroy(descriptorSetLayout);

        vertexBuffer.destroy();
        indexBuffer.destroy();
        uniformBufferVS.destroy();
    }

    // Prepare all Vulkan resources for the 3D texture (including descriptors)
    // Does not fill the texture with data
    void prepareNoiseTexture(uint32_t width, uint32_t height, uint32_t depth) {
        // A 3D texture is described as width x height x depth
        texture.extent.width = width;
        texture.extent.height = height;
        texture.extent.depth = depth;
        //texture.mipLevels = 1;
        texture.format = vk::Format::eR8Unorm;

        // Format support check
        // 3D texture support in Vulkan is mandatory (in contrast to OpenGL) so no need to check if it's supported
        vk::FormatProperties formatProperties = physicalDevice.getFormatProperties(texture.format);
        // Check if format supports transfer
        if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eTransferDst)) {
            std::cout << "Error: Device does not support flag TRANSFER_DST for selected texture format!" << std::endl;
            return;
        }
        // Check if GPU supports requested 3D texture dimensions
        uint32_t maxImageDimension3D(context.deviceProperties.limits.maxImageDimension3D);
        if (width > maxImageDimension3D || height > maxImageDimension3D || depth > maxImageDimension3D) {
            std::cout << "Error: Requested texture dimensions is greater than supported 3D texture dimension!" << std::endl;
            return;
        }

        // Create optimal tiled target image
        vk::ImageCreateInfo imageCreateInfo;
        imageCreateInfo.imageType = vk::ImageType::e3D;
        imageCreateInfo.format = texture.format;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.extent = texture.extent;
        // Set initial layout of the image to undefined
        imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        (vks::Image&)texture = context.createImage(imageCreateInfo, vk::MemoryPropertyFlagBits::eDeviceLocal);

        // Create sampler
        vk::SamplerCreateInfo sampler;
        sampler.magFilter = vk::Filter::eLinear;
        sampler.minFilter = vk::Filter::eLinear;
        sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
        sampler.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        sampler.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        sampler.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        sampler.maxAnisotropy = 1.0;
        //sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        texture.sampler = device.createSampler(sampler);

        // Create image view
        vk::ImageViewCreateInfo view;
        view.image = texture.image;
        view.viewType = vk::ImageViewType::e3D;
        view.format = texture.format;
        view.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        view.subresourceRange.layerCount = 1;
        view.subresourceRange.levelCount = 1;
        texture.view = device.createImageView(view);

        // Fill image descriptor image info to be used descriptor set setup
        texture.descriptor.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        texture.descriptor.imageView = texture.view;
        texture.descriptor.sampler = texture.sampler;

        updateNoiseTexture();
    }
#pragma warning(push)
#pragma warning(disable : 4018)
    static void forEveryExtent(const vk::Extent3D& extent, const std::function<void(int32_t x, int32_t y, int32_t z, int32_t offset)>& f) {
#pragma omp parallel for
        for (int32_t z = 0; z < extent.depth; z++) {
            for (int32_t y = 0; y < extent.height; y++) {
                for (int32_t x = 0; x < extent.width; x++) {
                    auto offset = x + y * extent.width + z * extent.width * extent.height;
                    f(x, y, z, offset);
                }
            }
        }
    }
#pragma warning(pop)

    // Generate randomized noise and upload it to the 3D texture using staging
    void updateNoiseTexture() {
        const uint32_t texMemSize = texture.extent.width * texture.extent.height * texture.extent.depth;

        std::vector<uint8_t> data;
        data.resize(texMemSize, 0);

        // Generate perlin based noise
        std::cout << "Generating " << texture.extent.width << " x " << texture.extent.height << " x " << texture.extent.depth << " noise texture..."
                  << std::endl;

        auto tStart = std::chrono::high_resolution_clock::now();

        PerlinNoise<float> perlinNoise;
#ifdef FRACTAL
        FractalNoise<float> fractalNoise(perlinNoise);
#endif
        const float noiseScale = static_cast<float>(rand() % 10) + 4.0f;

        forEveryExtent(texture.extent, [&](int32_t x, int32_t y, int32_t z, int32_t offset) {
            float nx = (float)x / (float)texture.extent.width;
            float ny = (float)y / (float)texture.extent.height;
            float nz = (float)z / (float)texture.extent.depth;
#ifdef FRACTAL
            float n = fractalNoise.noise(nx * noiseScale, ny * noiseScale, nz * noiseScale);
#else
            float n = 20.0 * perlinNoise.noise(nx, ny, nz);
#endif
            n = n - floor(n);

            data[offset] = static_cast<uint8_t>(floor(n * 255));
        });

        auto tEnd = std::chrono::high_resolution_clock::now();
        auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

        std::cout << "Done in " << tDiff << "ms" << std::endl;

        // Buffer object
        // Create a host-visible staging buffer that contains the raw image data
        auto stagingBuffer = context.createStagingBuffer(data);

        context.withPrimaryCommandBuffer([&](const vk::CommandBuffer& copyCmd) {
            // Optimal image will be used as destination for the copy, so we must transfer from our
            // initial undefined image layout to the transfer destination layout
            context.setImageLayout(copyCmd, texture.image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
            // Copy 3D noise data to texture
            // Setup buffer copy regions
            vk::BufferImageCopy bufferCopyRegion;
            bufferCopyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            bufferCopyRegion.imageExtent = texture.extent;
            copyCmd.copyBufferToImage(stagingBuffer.buffer, texture.image, vk::ImageLayout::eTransferDstOptimal, bufferCopyRegion);
            // Change texture image layout to shader read after all mip levels have been copied
            context.setImageLayout(copyCmd, texture.image, vk::ImageAspectFlagBits::eColor, vk::ImageLayout::eTransferDstOptimal,
                                   vk::ImageLayout::eShaderReadOnlyOptimal);
        });

        // Clean up staging resources
        stagingBuffer.destroy();
    }

    void updateDrawCommandBuffer(const vk::CommandBuffer& drawCmdBuffer) override {
        drawCmdBuffer.setViewport(0, vk::Viewport{ 0, 0, (float)width, (float)height, 0, 1 });
        drawCmdBuffer.setScissor(0, vk::Rect2D{ {}, vk::Extent2D{ width, height } });
        drawCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        drawCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);
        drawCmdBuffer.bindVertexBuffers(VERTEX_BUFFER_BIND_ID, vertexBuffer.buffer, { 0 });
        drawCmdBuffer.bindIndexBuffer(indexBuffer.buffer, 0, vk::IndexType::eUint32);
        drawCmdBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
        // drawUI(drawCmdBuffers[i]);
    }

    //void draw() override {
    //    Parent::prepareFrame();

    //    // Command buffer to be sumitted to the queue
    //    submitInfo.commandBufferCount = 1;
    //    submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

    //    // Submit to queue
    //    queue.submit(submitInfo, nullptr);
    //    Parent::submitFrame();
    //}

    void generateQuad() {
        // Setup vertices for a single uv-mapped quad made from two triangles
        std::vector<Vertex> vertices = {
            { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
            { { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
            { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
            { { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        };
        vertexBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertices);

        // Setup indices
        std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };
        indexBuffer = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indices);
        indexCount = static_cast<uint32_t>(indices.size());
    }

    void setupDescriptorPool() {
        // Example uses one ubo and one image sampler
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 1 },
        };
        descriptorPool = device.createDescriptorPool({ {}, 2, static_cast<uint32_t>(poolSizes.size()), poolSizes.data() });
    }

    void setupDescriptorSetLayout() {
        std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
            // Binding 0 : Vertex shader uniform buffer
            vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
            // Binding 1 : Fragment shader image sampler
            vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
        };

        vk::DescriptorSetLayoutCreateInfo descriptorLayout{ {}, static_cast<uint32_t>(setLayoutBindings.size()), setLayoutBindings.data() };

        descriptorSetLayout = device.createDescriptorSetLayout(descriptorLayout);
        pipelineLayout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, 1, &descriptorSetLayout });
    }

    void setupDescriptorSet() {
        vk::DescriptorSetAllocateInfo allocInfo{ descriptorPool, 1, &descriptorSetLayout };
        descriptorSet = device.allocateDescriptorSets(allocInfo)[0];

        std::vector<vk::WriteDescriptorSet> writeDescriptorSets{
            // Binding 0 : Vertex shader uniform buffer
            vk::WriteDescriptorSet{ descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &uniformBufferVS.descriptor },
            // Binding 1 : Fragment shader texture sampler
            vk::WriteDescriptorSet{ descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &texture.descriptor },
        };

        device.updateDescriptorSets(writeDescriptorSets, nullptr);
    }

    void preparePipelines() {
        vks::pipelines::GraphicsPipelineBuilder builder{ device, pipelineLayout, renderPass };
        builder.rasterizationState.cullMode = vk::CullModeFlagBits::eNone;
        builder.loadShader(getAssetPath() + "shaders/texture3d/texture3d.vert.spv", vk::ShaderStageFlagBits::eVertex);
        builder.loadShader(getAssetPath() + "shaders/texture3d/texture3d.frag.spv", vk::ShaderStageFlagBits::eFragment);
        builder.vertexInputState.bindingDescriptions = {
            vk::VertexInputBindingDescription{ VERTEX_BUFFER_BIND_ID, sizeof(Vertex) },
        };
        builder.vertexInputState.attributeDescriptions = {
            // Location 0 : Position
            vk::VertexInputAttributeDescription{ 0, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos) },
            // Location 1 : Texture coordinates
            vk::VertexInputAttributeDescription{ 1, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv) },
            // Location 1 : Vertex normal
            vk::VertexInputAttributeDescription{ 2, VERTEX_BUFFER_BIND_ID, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal) },
        };
        pipelines.solid = builder.create(pipelineCache);
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        uniformBufferVS = context.createUniformBuffer(uboVS);
        updateUniformBuffers();
    }

    void updateUniformBuffers(bool viewchanged = true) {
        if (viewchanged) {
            uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.001f, 256.0f);
            glm::mat4 viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

            uboVS.model = viewMatrix * glm::translate(glm::mat4(1.0f), cameraPos);
            uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            uboVS.model = glm::rotate(uboVS.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

            uboVS.viewPos = glm::vec4(0.0f, 0.0f, -zoom, 0.0f);
        } else {
            uboVS.depth += frameTimer * 0.15f;
            if (uboVS.depth > 1.0f)
                uboVS.depth = uboVS.depth - 1.0f;
        }
        uniformBufferVS.copy(uboVS);
    }

    void prepare() {
        Parent::prepare();
        generateQuad();
        prepareUniformBuffers();
        prepareNoiseTexture(128, 128, 128);
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        draw();
        if (!paused || viewUpdated) {
            updateUniformBuffers(viewUpdated);
        }
    }

    void OnUpdateUIOverlay() override {
        if (ui.header("Settings")) {
            if (ui.button("Generate new texture")) {
                updateNoiseTexture();
            }
        }
    }

    //void viewChanged() override {
    //    // This function is called by the base example class each time the view is changed by user input
    //    updateUniformBuffers();
    //}
};

VULKAN_EXAMPLE_MAIN()
