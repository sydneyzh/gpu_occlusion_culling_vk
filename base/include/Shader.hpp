#pragma once
#include <vulkan/vulkan.hpp>
#include "Device.hpp"

namespace base
{
class Shader
{
public:
    Shader(Device* p_dev,
           const vk::ShaderStageFlagBits shader_stage_flag_bits) :
        p_dev_(p_dev),
        shader_stage_flag_bits_(shader_stage_flag_bits)
    {}

    ~Shader()
    {
        if (module_) p_dev_->dev.destroyShaderModule(module_);
    }

    void generate(const uint32_t code_size,
                  const uint32_t* code_ptr)
    {
        module_ = p_dev_->dev.createShaderModule(vk::ShaderModuleCreateInfo({},
                                                                            code_size,
                                                                            code_ptr));
    }

    void generate(const char *filename)
    {
        std::ifstream fs(filename, std::ios::binary);
        if (fs.is_open()) {
            uint32_t size = fs.tellg();
            fs.seekg(0, std::ios::beg);
            char *code = new char[size];
            fs.read(code, size);
            fs.close();
            generate(size, reinterpret_cast<uint32_t *>(code));
            delete[] code;
        } else {
            std::string msg = "Cannot open shader file ";
            msg.append(filename);
            throw std::runtime_error(msg);
        }
    }

    vk::PipelineShaderStageCreateInfo create_pipeline_stage_info()
    {
        return {{}, shader_stage_flag_bits_, module_, "main"};
    }

private:
    Device * p_dev_;
    vk::ShaderStageFlagBits shader_stage_flag_bits_;
    vk::ShaderModule module_;
};
} // namespace base