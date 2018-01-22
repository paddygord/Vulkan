#include "shaders.hpp"
#include "filesystem.hpp"

vk::ShaderModule vks::shaders::loadShaderModule(const vk::Device& device, const std::string& filename) {
    vk::ShaderModule result;
    vks::file::withBinaryFileContents(filename, [&](size_t size, const void* data) {
        result = device.createShaderModule(
            vk::ShaderModuleCreateInfo{ {}, size, (const uint32_t*)data }
        );
    });
    vk::ShaderModuleCreateInfo x;
    return result;
}

// Load a SPIR-V shader
vk::PipelineShaderStageCreateInfo vks::shaders::loadShader(const vk::Device& device, const std::string& fileName, vk::ShaderStageFlagBits stage, const char* entryPoint) {
    vk::PipelineShaderStageCreateInfo shaderStage;
    shaderStage.stage = stage;
    shaderStage.module = loadShaderModule(device, fileName);
    shaderStage.pName = entryPoint;
    return shaderStage;
}
