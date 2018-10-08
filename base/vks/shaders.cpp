#include "shaders.hpp"
#include "filesystem.hpp"
#include "storage.hpp"

vk::ShaderModule vks::shaders::loadShaderModule(const vk::Device& device, const std::string& filename) {
    vk::ShaderModule result;
    {
        auto storage = storage::Storage::readFile(filename);
        vk::ShaderModuleCreateInfo createInfo;
        createInfo.codeSize = (uint32_t)storage->size();
        createInfo.pCode = (const uint32_t*)storage->data();
        result = device.createShaderModule(createInfo);
    }
    return result;
}

// Load a SPIR-V shader
vk::PipelineShaderStageCreateInfo vks::shaders::loadShader(const vk::Device& device,
                                                           const std::string& fileName,
                                                           vk::ShaderStageFlagBits stage,
                                                           const char* entryPoint) {
    vk::PipelineShaderStageCreateInfo shaderStage;
    shaderStage.stage = stage;
    shaderStage.module = loadShaderModule(device, fileName);
    shaderStage.pName = entryPoint;
    return shaderStage;
}
