#pragma once
#include <vulkan/vulkan.hpp>
#include "Physical_device.hpp"
#include "Device.hpp"
#include "tools.hpp"

namespace base
{
class Render_target
{
public:
    vk::Image image;
    vk::DeviceMemory mem;
    vk::ImageView view;
    vk::Format format;

    vk::Sampler sampler;
    vk::DescriptorImageInfo desc_image_info;

    bool has_mip_levels;
    uint32_t mip_levels{1};
    vk::ImageView view_with_mip_levels;

    Render_target(base::Physical_device* p_phy_dev,
                  base::Device* p_dev,
                  const vk::Format format,
                  const vk::Extent2D extent,
                  vk::ImageUsageFlags usage,
                  const vk::ImageAspectFlags aspect_flags,
                  const vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1,
                  const bool create_sampler = false,
                  const vk::SamplerCreateInfo sampler_create_info = {},
                  const vk::ImageLayout layout = {},
                  const bool has_mip_levels = false) :
        p_phy_dev_(p_phy_dev),
        p_dev_(p_dev),
        format(format),
        has_mip_levels(has_mip_levels)
    {
        if (create_sampler) {
            usage = usage | vk::ImageUsageFlagBits::eSampled;
        }
        create_image_(extent, usage, sample_count);
        allocate_and_bind_memory_();
        create_view_(format, aspect_flags);
        if (create_sampler) create_sampler_(sampler_create_info, layout);
    }
    ~Render_target()
    {
        if (sampler) p_dev_->dev.destroySampler(sampler);
        if (view) p_dev_->dev.destroyImageView(view);
        if (view_with_mip_levels) p_dev_->dev.destroyImageView(view_with_mip_levels);
        if (image) p_dev_->dev.destroyImage(image);
        if (mem) p_dev_->dev.freeMemory(mem);
    }

private:
    base::Physical_device* p_phy_dev_;
    base::Device* p_dev_;

    void create_image_(const vk::Extent2D extent, const vk::ImageUsageFlags& usage, const vk::SampleCountFlagBits& sample_count)
    {
        if (has_mip_levels)
            mip_levels = base::get_mip_levels(extent.width, extent.height);

        image = p_dev_->dev.createImage(vk::ImageCreateInfo({},
                                                            vk::ImageType::e2D,
                                                            format,
                                                            {extent.width, extent.height, 1},
                                                            mip_levels,
                                                            1,
                                                            sample_count,
                                                            vk::ImageTiling::eOptimal,
                                                            usage));
    }
    void create_view_(const vk::Format& format, const vk::ImageAspectFlags& aspect_flags)
    {
        view = p_dev_->dev.createImageView(
            vk::ImageViewCreateInfo({},
                                    image,
                                    vk::ImageViewType::e2D, format,
                                    vk::ComponentMapping(vk::ComponentSwizzle::eR,
                                                         vk::ComponentSwizzle::eG,
                                                         vk::ComponentSwizzle::eB,
                                                         vk::ComponentSwizzle::eA),
                                    vk::ImageSubresourceRange{aspect_flags, 0, 1, 0, 1}));
        if (has_mip_levels) {
            view_with_mip_levels = p_dev_->dev.createImageView(
                vk::ImageViewCreateInfo({},
                                        image,
                                        vk::ImageViewType::e2D,
                                        format,
                                        vk::ComponentMapping(vk::ComponentSwizzle::eR,
                                                             vk::ComponentSwizzle::eG,
                                                             vk::ComponentSwizzle::eB,
                                                             vk::ComponentSwizzle::eA),
                                        vk::ImageSubresourceRange{aspect_flags, 0, mip_levels, 0, 1}));
        }
    }
    void allocate_and_bind_memory_()
    {
        vk::MemoryRequirements mem_reqs = p_dev_->dev.getImageMemoryRequirements(image);
        mem = p_dev_->dev.allocateMemory(
            vk::MemoryAllocateInfo(mem_reqs.size,
                                   p_phy_dev_->get_memory_type_index(mem_reqs.memoryTypeBits,
                                                                     vk::MemoryPropertyFlagBits::eDeviceLocal)));
        p_dev_->dev.bindImageMemory(image, mem, 0);
    }
    void create_sampler_(const vk::SamplerCreateInfo& sampler_create_info, const vk::ImageLayout& layout)
    {
        sampler = p_dev_->dev.createSampler(sampler_create_info);
        if (!has_mip_levels) {
            desc_image_info = {sampler, view, layout};
        } else {
            desc_image_info = {sampler, view_with_mip_levels, layout};
        }
    }
};
} // namespace base
