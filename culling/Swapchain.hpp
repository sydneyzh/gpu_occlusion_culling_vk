#pragma once
#include "stdafx.h"

class Swapchain : public base::Swapchain
{
public:
    base::Render_target *p_prepass_depth_attachment{nullptr};

    Swapchain(base::Physical_device *p_phy_dev,
              base::Device *p_dev,
              vk::SurfaceKHR surface,
              vk::SurfaceFormatKHR surface_format,
              vk::Format depth_format,
              uint32_t image_count,
              base::Render_pass *p_onscreen_rp,
              uint32_t attachment_count) :
        base::Swapchain(p_phy_dev, p_dev, surface, surface_format, depth_format, image_count, p_onscreen_rp),
        attachment_count_(attachment_count)
    {}

    void detach() override
    {
        // onscreen
        if (p_depth_attachment_) {
            delete p_depth_attachment_;
            p_depth_attachment_ = nullptr;
        }
        // prepass 
        if (p_prepass_depth_attachment) {
            delete p_prepass_depth_attachment;
            p_prepass_depth_attachment = nullptr;
        }
        // color
        if (!p_color_attachments_.empty()) {
            for (auto p_color_attachment : p_color_attachments_) {
                if (p_color_attachment)
                    delete p_color_attachment;
            }
            p_color_attachments_.clear();
        }
        if (!framebuffers.empty()) {
            for (auto &fb : framebuffers) {
                p_dev_->dev.destroyFramebuffer(fb);
            }
            framebuffers.clear();
        }
    }

    void update_onscreen_rp(base::Render_pass *p_rp)
    {
        p_onscreen_rp_ = p_rp;
        resize(curr_extent_.width, curr_extent_.height, true);
    }

private:
    uint32_t attachment_count_;
    void create_depth_attachment_()  override
    {
        base::Swapchain::create_depth_attachment_();
        p_prepass_depth_attachment = new base::Render_target(p_phy_dev_, p_dev_,
                                                             depth_format_,
                                                             curr_extent_,
                                                             vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
                                                             vk::ImageAspectFlagBits::eDepth,
                                                             vk::SampleCountFlagBits::e1,
                                                             true,
                                                             vk::SamplerCreateInfo({},
                                                                                   vk::Filter::eNearest,
                                                                                   vk::Filter::eNearest,
                                                                                   vk::SamplerMipmapMode::eNearest,
                                                                                   vk::SamplerAddressMode::eClampToEdge,
                                                                                   vk::SamplerAddressMode::eClampToEdge,
                                                                                   vk::SamplerAddressMode::eClampToEdge,
                                                                                   0.f,
                                                                                   VK_FALSE,
                                                                                   1.f,
                                                                                   VK_FALSE,
                                                                                   vk::CompareOp::eNever,
                                                                                   0.f,
                                                                                   1.f),
                                                             vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }

    void create_framebuffers_() override
    {
        assert(image_count_);
        framebuffers.reserve(image_count_);
        vk::ImageView attachments[3];
        attachments[1] = p_prepass_depth_attachment->view;
        attachments[2] = p_depth_attachment_->view;
        for (uint32_t i = 0; i < image_count_; i++) {
            attachments[0] = p_color_attachments_[i]->view;
            framebuffers.push_back(p_dev_->dev.createFramebuffer(
                vk::FramebufferCreateInfo({},
                                          p_onscreen_rp_->rp,
                                          attachment_count_,
                                          attachments,
                                          curr_extent_.width,
                                          curr_extent_.height,
                                          1)));
        }
    }
};

