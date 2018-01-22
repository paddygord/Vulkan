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

#include <iostream>
#include <mutex>
#include <filesystem>

#include <vks/gltf.hpp>
#include <vks/context.hpp>
#include <vks/filesystem.hpp>
#include <vks/storage.hpp>
#include <pbr.hpp>

namespace fs = std::experimental::filesystem;

// Returns the passed value rounded up to the next 4 byte aligned value, if it's not already 4 byte aligned
template <typename T>
inline T evalAlignedSize(T value, T alignment) {
    auto alignmentRemainder = alignment - 1;
    auto alignmentMask = ~(T)alignmentRemainder;
    return (value + alignmentRemainder) & alignmentMask;
}

struct GltfPrimitive;
using GltfPrimitivePtr = std::shared_ptr<GltfPrimitive>;

struct GltfBridge {
    vks::Buffer buffer;
    std::unordered_map<vks::gltf::BufferViewPtr, vk::DeviceSize> viewOffsets;
    std::vector<GltfPrimitivePtr> primitives;
    void parse(const vks::Context& context, const vks::gltf::GltfPtr& gltf);

    const vk::DeviceSize& bufferViewOffset(const vks::gltf::BufferViewPtr& bufferView) const {
        auto itr = viewOffsets.find(bufferView);
        if (itr == viewOffsets.end()) {
            throw std::runtime_error("Unknown bufferview");
        }
        return itr->second;
    }

    void buildPipelines(const vks::pipelines::GraphicsPipelineBuilder& pipelineBuilder);
};


struct GltfPrimitive {
    const GltfBridge& parent;
    vks::pipelines::PipelineVertexInputStateCreateInfo vertexInputState;
    std::vector<vk::DeviceSize> bufferBindingOffsets;
    std::vector<vk::Buffer> bufferBindings;
    vk::IndexType indexType{ vk::IndexType::eUint16 };
    vk::DeviceSize indexOffset{ 0 };
    vk::Pipeline pipeline;
    uint32_t indexCount{ 0 };

    GltfPrimitive(const GltfBridge& parent, const vks::gltf::Primitive& primitive) : parent(parent) {
        setupVertexInputState(primitive);
        setupIndex(primitive);
    }

    void buildPipeline(vks::pipelines::GraphicsPipelineBuilder pipelineBuilder) {
        pipeline = pipelineBuilder.create();
    }

    void draw(const vk::CommandBuffer& cmdBuffer) {
        cmdBuffer.bindVertexBuffers(0, bufferBindings, bufferBindingOffsets);
        cmdBuffer.bindIndexBuffer(parent.buffer.buffer, indexOffset, indexType);
        cmdBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
    }

private:
    static vks::model::Component attributeLocationForName(const std::string& name) {
        static const std::string POSITION{ "POSITION" };
        static const std::string NORMAL{ "NORMAL" };
        static const std::string TANGENT{ "TANGENT" };
        static const std::string TEXCOORD_0{ "TEXCOORD_0" };
        static const std::string TEXCOORD_1{ "TEXCOORD_1" };
        static const std::string COLOR_0{ "COLOR_0" };
        static const std::string JOINTS_0{ "JOINTS_0" };
        static const std::string WEIGHTS_0{ "WEIGHTS_0" };
        if (name == POSITION) {
            return vks::model::Component::VERTEX_COMPONENT_POSITION;
        } else if (name == NORMAL) {
            return vks::model::Component::VERTEX_COMPONENT_NORMAL;
        } else if (name == TANGENT) {
            return vks::model::Component::VERTEX_COMPONENT_TANGENT;
        } else if (name == TEXCOORD_0) {
            return vks::model::Component::VERTEX_COMPONENT_UV;
        } else if (name == COLOR_0) {
            return vks::model::Component::VERTEX_COMPONENT_COLOR;
        }
        throw std::runtime_error("Unsupported attribute " + name);
    }

    static vk::Format formatForLocationAndAttribute(vks::model::Component location, const vks::gltf::Accessor& accessor) {
        using ComponentType = vks::gltf::Accessor::ComponentType;
        using Type = vks::gltf::Accessor::Type;
        using vks::model::Component;
        switch (location) {
        case Component::VERTEX_COMPONENT_POSITION:
            assert(accessor.componentType == ComponentType::Float);
            assert(accessor.type == Type::Vec3);
            return vk::Format::eR32G32B32Sfloat;
            break;

        case vks::model::VERTEX_COMPONENT_NORMAL:
            assert(accessor.componentType == ComponentType::Float);
            assert(accessor.type == Type::Vec3);
            return vk::Format::eR32G32B32Sfloat;
            break;

        case vks::model::VERTEX_COMPONENT_UV:
            assert(accessor.type == Type::Vec2);
            assert((accessor.componentType == ComponentType::Float) || (accessor.componentType == ComponentType::UnsignedByte) ||
                (accessor.componentType == ComponentType::UnsignedShort));
            switch (accessor.componentType) {
            case ComponentType::Float:
                return vk::Format::eR32G32Sfloat;
                break;
            case ComponentType::UnsignedByte:
                return vk::Format::eR8G8Unorm;
                break;
            case ComponentType::UnsignedShort:
                return vk::Format::eR16G16Unorm;
                break;
            }
            break;

        case vks::model::VERTEX_COMPONENT_COLOR:
            assert((accessor.type == Type::Vec3) || (accessor.type == Type::Vec4));
            assert((accessor.componentType == ComponentType::Float) || (accessor.componentType == ComponentType::UnsignedByte) ||
                (accessor.componentType == ComponentType::UnsignedShort));
            switch (accessor.componentType) {
            case ComponentType::Float:
                return (accessor.type == Type::Vec3) ? vk::Format::eR32G32B32Sfloat : vk::Format::eR32G32B32A32Sfloat;
                break;
            case ComponentType::UnsignedByte:
                return (accessor.type == Type::Vec3) ? vk::Format::eR8G8B8Unorm : vk::Format::eR8G8B8A8Unorm;
                break;
            case ComponentType::UnsignedShort:
                return (accessor.type == Type::Vec3) ? vk::Format::eR16G16B16Unorm : vk::Format::eR16G16B16A16Unorm;
                break;
            }
            break;
        default:
            break;
        }
        throw std::runtime_error("Unable to determine format");
    }

    void setupVertexInputState(const vks::gltf::Primitive& primitive) {
        uint32_t count = (uint32_t)primitive.attributes.size();
        for (uint32_t i = 0; i < count; ++i) {
            const auto& attribute = primitive.attributes[i];
            // FIXME account for the stride in bufferviews
            const auto& attributeName = attribute.first;
            const auto& accessor = *(attribute.second);
            const auto& bufferViewPtr = accessor.bufferView;
            const auto& gpuOffset = parent.bufferViewOffset(bufferViewPtr);
            auto location = attributeLocationForName(attributeName);
            vertexInputState.bindingDescriptions.emplace_back(vk::VertexInputBindingDescription{ i, bufferViewPtr->stride });
            vertexInputState.attributeDescriptions.emplace_back(vk::VertexInputAttributeDescription{ (uint32_t)location, i, formatForLocationAndAttribute(location, accessor), 0 });
            bufferBindings.push_back(parent.buffer.buffer);
            bufferBindingOffsets.push_back(gpuOffset);
        }
    }

    void setupIndex(const vks::gltf::Primitive& primitive) {
        auto indexAccessorPtr = primitive.indices;
        if (indexAccessorPtr) {
            const auto& indexAccessor = *indexAccessorPtr;
            assert(indexAccessor.type == vks::gltf::Accessor::Type::Scalar);
            indexOffset = parent.bufferViewOffset(indexAccessor.bufferView);
            const auto& componentType = indexAccessor.componentType;
            switch (componentType) {
            case vks::gltf::Accessor::ComponentType::UnsignedShort:
                indexType = vk::IndexType::eUint16;
                break;
            case vks::gltf::Accessor::ComponentType::UnsignedInt:
                indexType = vk::IndexType::eUint32;
                break;
            default:
                throw std::runtime_error("Invalid index component type");
            }
            indexCount = indexAccessor.count;
        }
    }

};


void GltfBridge::parse(const vks::Context& context, const vks::gltf::GltfPtr& gltf) {
    static const auto bufferUsageFlags =
        vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
    static std::once_flag once;
    static vk::MemoryRequirements memoryRequirements;
    const auto& device = context.device;
    // Determine the needed buffer offsets
    std::call_once(once, [&] {
        vk::BufferCreateInfo bci;
        auto tempBuffer = device.createBuffer({ {}, 8192, bufferUsageFlags });
        memoryRequirements = device.getBufferMemoryRequirements(tempBuffer);
        memoryRequirements.size = 0;
        device.destroyBuffer(tempBuffer);
    });

    // Make the binary data accessible
    const auto& gltfBuffer = *(gltf->buffers[0]);
    auto storagePath = fs::path(gltf->baseUri).append(gltfBuffer.uri).string();
    auto storage = vks::storage::Storage::readFile(storagePath);
    std::vector<vks::Buffer> stagingBuffers;

    uint32_t paddedLength = 0;
    for (const auto& bufferViewPtr : gltf->bufferViews) {
        const auto& bufferView = *bufferViewPtr;
        viewOffsets.insert({ bufferViewPtr, paddedLength });
        auto stagingBuffer = context.createStagingBuffer(bufferView.length, storage->data() + bufferView.offset);
        stagingBuffers.push_back(stagingBuffer);
        paddedLength += evalAlignedSize(bufferView.length, (uint32_t)memoryRequirements.alignment);
    }

    // Create the output buffer
    buffer = context.createBuffer(bufferUsageFlags, vk::MemoryPropertyFlagBits::eDeviceLocal, paddedLength);

    // Transfer the view data to the target
    context.withPrimaryCommandBuffer([&](const auto& commandBuffer) {
        size_t bufferViewCount = gltf->bufferViews.size();
        for (size_t i = 0; i < bufferViewCount; ++i) {
            const auto& bufferViewPtr = gltf->bufferViews[i];
            const auto& bufferView = *(gltf->bufferViews[i]);
            const auto bufferViewOffset = viewOffsets[bufferViewPtr];
            assert(bufferViewOffset + bufferView.length < buffer.size);
            const auto& stagingBuffer = stagingBuffers[i];
            commandBuffer.copyBuffer(stagingBuffer.buffer, buffer.buffer, { 0, bufferViewOffset, bufferView.length });
        }
    });

    for (auto& stagingBuffer : stagingBuffers) {
        stagingBuffer.destroy();
    }

    for (const auto& mesh : gltf->meshes) {
        for (const auto& primitive : mesh->primitives) {
            primitives.emplace_back(*this, primitive);
        }
    }
}

void GltfBridge::buildPipelines(const vks::pipelines::GraphicsPipelineBuilder& pipelineBuilder) {
    for (auto& primitive : primitives) {
        primitive->buildPipeline(pipelineBuilder);
    }
}


#if 0
void resolveLocalStorage(vks::gltf::GltfPtr& gltf, const fs::path& basePath) {
    std::cout << basePath << std::endl;
    std::cout << "Buffers: " << gltf->buffers.size() << std::endl;
    for (auto& buffer : gltf->buffers) {
        if (buffer->uri.empty()) {
            continue;
        }
        auto fullPath = fs::path(basePath).append(buffer->uri);
        std::cout << fullPath << std::endl;
        buffer->_storage = vks::storage::Storage::readFile(fullPath.string());
    }
    std::cout << "Images: " << gltf->images.size() << std::endl;
    for (auto& image : gltf->images) {
        if (image->uri.empty()) {
            continue;
        }
        auto fullPath = fs::path(basePath).append(image->uri);
        std::cout << fullPath << std::endl;
        image->_storage = vks::storage::Storage::readFile(fullPath.string());
    }
    std::cout << std::endl;
}
#endif

inline bool ends_with(const std::string& value, const std::string& ending) {
    if (ending.size() > value.size())
        return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

#if 0
struct GltfParserTest {
    vks::Context context;
    const vk::Device& device{ context.device };

    vks::Buffer toVulkanBuffer(const vks::gltf::BufferPtr& buffer) {
        const vk::BufferUsageFlags bufferFlags{ vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst };
        return context.stageToDeviceBuffer(bufferFlags, buffer->byteLength, buffer->_storage->data());
    }

    void processGltfString(const std::string& jsonString, const fs::path& baseDir) {
        auto gltf = vks::gltf::parse(jsonString);
        gltf->resolveLocalStorage(baseDir.string());

        // Create the buffer object
        assert(gltf->buffers.size() == 1);
        vks::Buffer result = toVulkanBuffer(gltf->buffers[0]);

        // Copy from the source views to the buffer
        size_t destinationOffset = 0;
    }

    void processGltfFile(const fs::path& path) {
        std::string jsonString;
        vks::file::withBinaryFileContents(path.string(), [&](size_t size, const void* data) {
            jsonString = std::string((const char*)data, size);
        });
        processGltfString(jsonString, path.parent_path());
    }

    void processGlbFile(const fs::path& path) {
        std::string jsonString;
        std::vector<uint8_t> binary;
        vks::file::withBinaryFileContents(path.string(), [&](size_t size, const void* data) {
            using namespace vks::gltf::glb;
            auto byteData = (const uint8_t*)data;
            auto end = byteData + size;
            const auto header = (const Header*)byteData;
            auto gltfEnd = byteData + header->length;
            byteData += sizeof(Header);
            {
                const auto chunkPtr = (const ChunkHeader*)byteData;
                byteData += sizeof(ChunkHeader);
                assert(chunkPtr->type == ChunkHeader::Type::JSON);
                jsonString = std::string((const char*)byteData, chunkPtr->length);
                byteData += chunkPtr->length;
            }

            if (byteData < gltfEnd) {
                const auto chunkPtr = (const ChunkHeader*)byteData;
                byteData += sizeof(ChunkHeader);
                assert(chunkPtr->type == ChunkHeader::Type::BIN);
                binary.resize(chunkPtr->length);
                memcpy(binary.data(), byteData, chunkPtr->length);
                byteData += chunkPtr->length;
            }

            assert(byteData == gltfEnd);
        });
        auto gltf = vks::gltf::parse(jsonString);
    }


    void run() {
        context.setValidationEnabled(true);
        context.create();

        processGltfFile(fs::path("C:/gltf/Corset/glTF/Corset.gltf"));
#if 0
        for (auto& entry : fs::directory_iterator("C:/gltf")) {
            auto path = entry.path();
            if (fs::is_regular_file(path) && ends_with(path.string(), ".glb")) {
                std::cout << entry << std::endl;
                processGlbFile(path);
            }
        }
#endif
        std::cout << "Done" << std::endl;
    }
};
#endif

namespace pbr {
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
}  // namespace pbr

// Vertex layout for the models
vks::model::VertexLayout vertexLayout{ {
    vks::model::VERTEX_COMPONENT_POSITION,
    vks::model::VERTEX_COMPONENT_NORMAL,
    vks::model::VERTEX_COMPONENT_UV,
} };

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

    struct Meshes {
        vks::model::Model skybox;
        vks::gltf::GltfPtr gltf;
        GltfBridge corset;
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
    } pipelines;

    struct {
        vk::DescriptorSet object;
        vk::DescriptorSet skybox;
    } descriptorSets;

    vk::PipelineLayout pipelineLayout;
    vk::DescriptorSetLayout descriptorSetLayout;

    // Default materials to select from
    std::vector<pbr::Material> materials;
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
        materials.push_back(pbr::Material("Gold", glm::vec3(1.0f, 0.765557f, 0.336057f)));
        materials.push_back(pbr::Material("Copper", glm::vec3(0.955008f, 0.637427f, 0.538163f)));
        materials.push_back(pbr::Material("Chromium", glm::vec3(0.549585f, 0.556114f, 0.554256f)));
        materials.push_back(pbr::Material("Nickel", glm::vec3(0.659777f, 0.608679f, 0.525649f)));
        materials.push_back(pbr::Material("Titanium", glm::vec3(0.541931f, 0.496791f, 0.449419f)));
        materials.push_back(pbr::Material("Cobalt", glm::vec3(0.662124f, 0.654864f, 0.633732f)));
        materials.push_back(pbr::Material("Platinum", glm::vec3(0.672411f, 0.637331f, 0.585456f)));
        // Testing materials
        materials.push_back(pbr::Material("White", glm::vec3(1.0f)));
        materials.push_back(pbr::Material("Dark", glm::vec3(0.1f)));
        materials.push_back(pbr::Material("Black", glm::vec3(0.0f)));
        materials.push_back(pbr::Material("Red", glm::vec3(1.0f, 0.0f, 0.0f)));
        materials.push_back(pbr::Material("Blue", glm::vec3(0.0f, 0.0f, 1.0f)));

        settings.overlay = true;

        for (auto material : materials) {
            materialNames.push_back(material.name);
        }
        objectNames = { "Sphere", "Teapot", "Torusknot", "Venus" };

        materialIndex = 9;
    }

    ~VulkanExample() {

        device.destroyPipeline(pipelines.skybox, nullptr);

        device.destroyPipelineLayout(pipelineLayout, nullptr);
        device.destroyDescriptorSetLayout(descriptorSetLayout, nullptr);

        models.skybox.destroy();
        models.corset.destroy();

        uniformBuffers.object.destroy();
        uniformBuffers.skybox.destroy();
        uniformBuffers.params.destroy();

        textures.environmentCube.destroy();
        textures.irradianceCube.destroy();
        textures.prefilteredCube.destroy();
        textures.lutBrdf.destroy();
    }

    void loadAssets() override {
        textures.environmentCube.loadFromFile(context, getAssetPath() + "textures/hdr/pisa_cube.ktx", vF::eR16G16B16A16Sfloat);
        // Skybox
        models.skybox.loadFromFile(context, getAssetPath() + "models/cube.obj", vertexLayout, 1.0f);

        // Objects
        {
            static const std::string corsetFileName = "C:/gltf/Corset/glTF/Corset.gltf";
            std::string jsonString = vks::file::readTextFile(corsetFileName);
            models.gltf = vks::gltf::Gltf::parse(jsonString);
            models.gltf->baseUri = fs::path(corsetFileName).parent_path().string();
            models.corset.parse(context, models.gltf);
        }
    }

    void getEnabledFeatures() override {
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
            commandBuffer.bindVertexBuffers(0, { models.skybox.vertices.buffer }, { 0 });
            commandBuffer.bindIndexBuffer(models.skybox.indices.buffer, 0, vk::IndexType::eUint32);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skybox);
            commandBuffer.drawIndexed(models.skybox.indexCount, 1, 0, 0, 0);
        }

        // Objects
        /*
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, 1, &descriptorSets.object, 0, NULL);
        commandBuffer.bindVertexBuffers(0, 1, &models.objects[models.objectIndex].vertices.buffer, offsets);
        commandBuffer.bindIndexBuffer(models.objects[models.objectIndex].indices.buffer, 0, vk::IndexType::eUint32);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.pbr);

        Material mat = materials[materialIndex];
        uint32_t objcount = 10;
        for (uint32_t x = 0; x < objcount; x++) {
            glm::vec3 pos = glm::vec3(float(x - (objcount / 2.0f)) * 2.15f, 0.0f, 0.0f);
            mat.params.roughness = 1.0f - glm::clamp((float)x / (float)objcount, 0.005f, 1.0f);
            mat.params.metallic = glm::clamp((float)x / (float)objcount, 0.005f, 1.0f);
            commandBuffer.pushConstants<glm::vec3>(pipelineLayout, vSS::eVertex, 0, pos);
            commandBuffer.pushConstants<Material::PushBlock>(pipelineLayout, vSS::eFragment, sizeof(glm::vec3), mat.params);
            commandBuffer.drawIndexed(models.objects[models.objectIndex].indexCount, 1, 0, 0, 0);
        }
        */
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
            { vSS::eFragment, sizeof(glm::vec3), sizeof(pbr::Material::PushBlock) },
        };
        pipelineLayout = device.createPipelineLayout({ {}, 1, &descriptorSetLayout, (uint32_t)pushConstantRanges.size(), pushConstantRanges.data() });

        // Pipelines
        vks::pipelines::GraphicsPipelineBuilder pipelineBuilder{ device, pipelineLayout, renderPass };
        pipelineBuilder.pipelineCache = context.pipelineCache;
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
        pipelineBuilder.vertexInputState = {};
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/pbribl.vert.spv", vSS::eVertex);
        pipelineBuilder.loadShader(getAssetPath() + "shaders/pbribl/pbribl.frag.spv", vSS::eFragment);
        models.corset.buildPipelines(pipelineBuilder);
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
        //uboMatrices.model = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f + (models.objectIndex == 1 ? 45.0f : 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
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
        vkx::pbr::generateBRDFLUT(context, textures.lutBrdf);
        vkx::pbr::generateIrradianceCube(context, textures.irradianceCube, models.skybox, vertexLayout, textures.environmentCube.descriptor);
        vkx::pbr::generatePrefilteredCube(context, textures.prefilteredCube, models.skybox, vertexLayout, textures.environmentCube.descriptor);
        prepareUniformBuffers();
        setupDescriptors();
        preparePipelines();
        buildCommandBuffers();
        prepared = true;
    }

    void viewChanged() override { updateUniformBuffers(); }

    void OnUpdateUIOverlay() override {
        if (ui.header("Settings")) {
            if (ui.comboBox("Material", &materialIndex, materialNames)) {
                buildCommandBuffers();
            }
            //if (ui.comboBox("Object type", &models.objectIndex, objectNames)) {
            //    updateUniformBuffers();
            //    buildCommandBuffers();
            //}
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
