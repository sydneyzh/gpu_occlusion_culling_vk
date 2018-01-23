#pragma once
#include "stdafx.h"
#include <string>

class Prog_info : public base::Prog_info_base
{
public:
    const uint32_t MAX_DEPTH_IMAGE_WIDTH = 1024;
    const uint32_t MAX_DEPTH_IMAGE_HEIGHT = 1024;

    // 1.5 times the max depth image width
    const uint32_t MAX_DEPTH_STAGING_IMAGE_WIDTH = 1536; 
    // same as the max depth image height
    const uint32_t MAX_DEPTH_STAGING_IMAGE_HEIGHT = 1024; 

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
        width_ = std::min(width, MAX_DEPTH_IMAGE_WIDTH);
        height_ = std::min(height, MAX_DEPTH_IMAGE_HEIGHT);
        resize_flag=true;
    }

    void select_mode(uint32_t mode)
    {
        // mode 1 simple multidraw indirect 
        // mode 2 frustum culling
        // mode 3 frustum + occlusion culling 
        // mode 4 frustum + occlusion culling (blending enabled)
        if (mode < 5 && mode > 0)
            mode_ = mode;
    }

    uint32_t mode() const {
        return mode_;
    }

private:
    uint32_t width_{1024};
    uint32_t height_{700};
    std::string prog_name_{"occlusion culling vk"};
    uint32_t mode_{1};
};
