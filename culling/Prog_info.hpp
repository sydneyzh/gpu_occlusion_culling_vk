#pragma once
#include <string>

class Prog_info : public base::Prog_info_base
{
public:

    Prog_info() = default;

    uint32_t width() const override
    {
        return width_;
    }

    uint32_t height() const override
    {
        return height_;
    }

    const std::string& prog_name() const override
    {
        return prog_name_;
    }

    void on_resize(uint32_t width, uint32_t height) override
    {
        resize_flag=true;
    }

private:
    uint32_t width_{800};
    uint32_t height_{600};
    std::string prog_name_{"Vulkan Scene"};
};