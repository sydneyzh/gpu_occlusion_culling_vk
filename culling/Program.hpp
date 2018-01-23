#pragma once

#include "stdafx.h"
#include "Shell.hpp"
#include "Model.hpp"
#include "Prog_info.hpp"

#define BACK_BUFFER_COUNT 3
#define FONT_FILENAME "RobotoMonoMedium"
#define MODEL_FILENAME "occlusion_scene.fbx"

class Program : public base::Program_base
{
public:
    Program(const bool enable_validation,
            Prog_info *p_info,
            Shell *p_shell,
            base::Camera *p_camera,
            std::string &model_filename) :
        Program_base(enable_validation, p_info, p_shell),
        p_info_(p_info),
        p_shell_(p_shell),
        p_camera_(p_camera)
    {
        p_camera_->update_aspect(p_info->width(), p_info->height());

        // to read a different scene, 
        // image descriptor count needs to be modified accordingly in this program
        if (model_filename.size() == 0) model_filename_ = MODEL_FILENAME;
        else model_filename_ = model_filename;

        req_phy_dev_features_.multiDrawIndirect = VK_TRUE;
    }

    ~Program() override
    {
        p_dev_->dev.waitIdle();
        destroy_pipelines_();
        destroy_shaders_();
        destroy_descriptors_();
        destroy_depth_resources_();
        destroy_swapchain_();
        destroy_render_passes_();
        destroy_frame_data_();
        destroy_text_overlay_();
        destroy_model_();
        destroy_command_pools_();
        destroy_back_buffers_();
    }

    void init()
    {
        init_base();
        init_back_buffers_();
        init_command_pools_();
        init_model_();
        init_text_overlay_();
        init_frame_data_();
        init_render_passes_();
        init_swapchain_();
        init_depth_resources_();
        init_descriptors_();
        init_shaders_();
        init_pipelines_();
    }

private:
    Prog_info * p_info_{nullptr};
    Shell *p_shell_{nullptr};
    base::Camera *p_camera_{nullptr};
    std::string model_filename_;

    /* ---------------------------------------------------------- */

    struct Back_buffer
    {
        uint32_t swapchain_image_idx{0};
        vk::Semaphore swapchain_image_acquire_semaphore{};
        vk::Semaphore onscreen_render_semaphore{};
        vk::Semaphore compute_complete_semaphore{};
        vk::Fence present_queue_submit_fence{nullptr};
    };
    std::deque<Back_buffer> back_buffers_;
    Back_buffer acquired_back_buf_;

    void init_back_buffers_()
    {
        for (auto i = 0; i < BACK_BUFFER_COUNT; i++) {
            Back_buffer back;
            back.swapchain_image_acquire_semaphore = p_dev_->dev.createSemaphore(vk::SemaphoreCreateInfo());
            back.onscreen_render_semaphore = p_dev_->dev.createSemaphore(vk::SemaphoreCreateInfo());
            back.compute_complete_semaphore = p_dev_->dev.createSemaphore(vk::SemaphoreCreateInfo());
            back.present_queue_submit_fence = p_dev_->dev.createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
            back_buffers_.push_back(back);
        }
    }

    void destroy_back_buffers_()
    {
        while (!back_buffers_.empty()) {
            auto &back = back_buffers_.front();
            p_dev_->dev.destroySemaphore(back.swapchain_image_acquire_semaphore);
            p_dev_->dev.destroySemaphore(back.onscreen_render_semaphore);
            p_dev_->dev.destroySemaphore(back.compute_complete_semaphore);
            p_dev_->dev.destroyFence(back.present_queue_submit_fence);
            back_buffers_.pop_front();
        }
    }

    /* ---------------------------------------------------------- */

    vk::CommandPool graphics_cmd_pool_;
    vk::CommandPool compute_cmd_pool_;

    void init_command_pools_()
    {
        graphics_cmd_pool_ = p_dev_->dev.createCommandPool(
            vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                      p_phy_dev_->graphics_queue_family_idx));
        compute_cmd_pool_ = p_dev_->dev.createCommandPool(
            vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                      p_phy_dev_->compute_queue_family_idx));
    }

    void destroy_command_pools_()
    {
        p_dev_->dev.destroyCommandPool(graphics_cmd_pool_);
        p_dev_->dev.destroyCommandPool(compute_cmd_pool_);
    }

    /* ---------------------------------------------------------- */

    Model *p_model_{nullptr};

    void init_model_()
    {
        p_model_ = new Model(p_phy_dev_, p_dev_, graphics_cmd_pool_, true);

        auto model_path = base::data_dir() + "models/" + model_filename_;
        auto components = std::vector<base::Vertex_component>
        {
            base::VERT_COMP_POSITION,
            base::VERT_COMP_NORMAL,
            base::VERT_COMP_UV
        };
        base::Vertex_layout layout(components);

        auto tex_dir = base::data_dir() + "models/";
        p_model_->load(model_path, layout, aiProcess_GenNormals | aiProcess_GenUVCoords, tex_dir);

        p_camera_->eye_pos = {20.f, 2.f, 0.f};
        p_camera_->cam_far = 1000.f;
        p_camera_->update();
    }

    void destroy_model_()
    {
        delete p_model_;
    }

    /* ---------------------------------------------------------- */

    base::Text_overlay *p_text_overlay_{nullptr};

    void init_text_overlay_()
    {
        std::string font_path = base::data_dir();
        font_path.append("fonts/");
        font_path.append(FONT_FILENAME);
        p_text_overlay_ = new base::Text_overlay(p_phy_dev_, p_dev_, graphics_cmd_pool_, font_path);
    }

    void destroy_text_overlay_()
    {
        delete p_text_overlay_;
    }

    /* ---------------------------------------------------------- */

    static const uint32_t max_query_count_{10};
    struct Query_data
    {
        uint32_t data[max_query_count_];
    };
    enum
    {
        QUERY_ONSCREEN_START,
        QUERY_ONSCREEN_STOP,
        QUERY_DEPTH_START,
        QUERY_DEPTH_STOP,
        QUERY_TRANSFER_START,
        QUERY_TRANSFER_STOP,
        QUERY_COMPUTE_MIPCHAIN_START,
        QUERY_COMPUTE_MIPCHAIN_STOP,
        QUERY_COMPUTE_VISIBILITY_START,
        QUERY_COMPUTE_VISIBILITY_STOP
    };

    struct UBO
    {
        glm::mat4 model;
        glm::mat4 normal;
        glm::mat4 view;
        glm::mat4 projection_clip;
        float cam_near;
        float cam_far;
        glm::vec2 resolution;
    } ubo_;

    struct Frame_data
    {
        vk::DescriptorSet desc_set;
        uint8_t *mapped{nullptr};
        uint32_t dynamic_offset{0};

        vk::CommandBuffer graphics_cmd_buffer;
        vk::Fence graphics_submit_fence;

        vk::CommandBuffer compute_cmd_buffer;
        vk::Fence compute_submit_fence;

        vk::QueryPool query_pool;
        Query_data query_data;
    };

    std::vector<Frame_data> frame_data_vector_;
    uint32_t frame_data_count_{0};
    uint32_t frame_data_idx_{0};

    base::Buffer *p_global_uniforms_{nullptr};
    vk::DeviceMemory global_uniforms_mem_;

    void init_frame_data_()
    {
        frame_data_count_ = BACK_BUFFER_COUNT;
        frame_data_vector_.resize(frame_data_count_);

        vk::MemoryPropertyFlags host_visible_coherent{vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent};
        vk::SharingMode sharing_mode{vk::SharingMode::eExclusive};

        vk::DeviceSize aligned_size = sizeof(UBO);
        const vk::DeviceSize &alignment = p_phy_dev_->props.limits.minUniformBufferOffsetAlignment;
        base::align_size(aligned_size, alignment);

        p_global_uniforms_ = new base::Buffer(p_dev_,
                                              aligned_size * frame_data_count_,
                                              vk::BufferUsageFlagBits::eUniformBuffer,
                                              host_visible_coherent,
                                              sharing_mode,
                                              0,
                                              nullptr);
        p_global_uniforms_->update_descriptor();
        base::allocate_and_bind_buffer_memory(p_phy_dev_,
                                              p_dev_,
                                              global_uniforms_mem_,
                                              1, &p_global_uniforms_,
                                              aligned_size * frame_data_count_);

        std::vector<vk::CommandBuffer> graphics_cmd_buffers(frame_data_count_);
        std::vector<vk::CommandBuffer> compute_cmd_buffers(frame_data_count_);
        graphics_cmd_buffers = p_dev_->dev.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo(graphics_cmd_pool_,
                                          vk::CommandBufferLevel::ePrimary,
                                          static_cast<uint32_t>(graphics_cmd_buffers.size())));
        compute_cmd_buffers = p_dev_->dev.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo(compute_cmd_pool_,
                                          vk::CommandBufferLevel::ePrimary,
                                          static_cast<uint32_t>(compute_cmd_buffers.size())));
        int idx = 0;
        uint8_t *base = reinterpret_cast<uint8_t *>(p_global_uniforms_->mapped);
        for (auto &data : frame_data_vector_) {
            data.dynamic_offset = idx * aligned_size;
            data.mapped = base + idx * aligned_size;
            data.graphics_cmd_buffer = graphics_cmd_buffers[idx];
            data.compute_cmd_buffer = compute_cmd_buffers[idx];
            data.graphics_submit_fence = p_dev_->dev.createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
            data.compute_submit_fence = p_dev_->dev.createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
            data.query_pool = p_dev_->dev.createQueryPool(vk::QueryPoolCreateInfo({},
                                                                                  vk::QueryType::eTimestamp,
                                                                                  max_query_count_,
                                                                                  {}));
            idx++;
        }
    }

    void destroy_frame_data_()
    {
        p_dev_->dev.freeMemory(global_uniforms_mem_);
        delete p_global_uniforms_;
        for (auto &data : frame_data_vector_) {
            p_dev_->dev.destroyFence(data.graphics_submit_fence);
            p_dev_->dev.destroyFence(data.compute_submit_fence);
            p_dev_->dev.destroyQueryPool(data.query_pool);
        }
    }

    /* ---------------------------------------------------------- */

    base::Render_pass *p_rp_simple_{nullptr};
    base::Render_pass *p_rp_depth_{nullptr};

    vk::Format depth_format_{vk::Format::eD32Sfloat};

    std::vector<vk::ClearValue> clear_values_ = {
        vk::ClearColorValue(std::array<float, 4>{.2f, .2f, .2f, 1.f}),
        vk::ClearDepthStencilValue(1.f, 0)
    };

    void init_render_passes_()
    {
        p_rp_simple_ = new base::Render_pass(p_dev_, 2, clear_values_.data());
        p_rp_depth_ = new base::Render_pass(p_dev_, 1, &clear_values_[1]);

        // simple
        {
            vk::AttachmentDescription attachments[2] = {
                // color
                vk::AttachmentDescription(
                    {},
                    surface_format_.format,
                    vk::SampleCountFlagBits::e1,
                    vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eStore,
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eDontCare,
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::ePresentSrcKHR),
                // depth
                vk::AttachmentDescription(
                    {},
                    depth_format_,
                    vk::SampleCountFlagBits::e1,
                    vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eDontCare,
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eDontCare,
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eDepthStencilReadOnlyOptimal),
            };

            vk::AttachmentReference references[2] = {
                // color
                vk::AttachmentReference(
                    0,
                    vk::ImageLayout::eColorAttachmentOptimal),
                // depth
                vk::AttachmentReference(
                    1,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal),
            };

            vk::SubpassDescription subpass = vk::SubpassDescription({},
                                                                    vk::PipelineBindPoint::eGraphics,
                                                                    0, nullptr,
                                                                    1, &references[0],
                                                                    nullptr,
                                                                    &references[1],
                                                                    0, nullptr);
            vk::SubpassDependency dependencies[2] = {
                vk::SubpassDependency(
                    VK_SUBPASS_EXTERNAL,
                    0,
                    vk::PipelineStageFlagBits::eBottomOfPipe,
                    vk::PipelineStageFlagBits::eColorAttachmentOutput,
                    vk::AccessFlagBits::eHostWrite,
                    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                    vk::DependencyFlagBits::eByRegion),
                vk::SubpassDependency(
                    0,
                    VK_SUBPASS_EXTERNAL,
                    vk::PipelineStageFlagBits::eColorAttachmentOutput,
                    vk::PipelineStageFlagBits::eBottomOfPipe,
                    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                    vk::AccessFlagBits::eHostWrite,
                    vk::DependencyFlagBits::eByRegion)
            };
            p_rp_simple_->create(2, attachments,
                                 1, &subpass,
                                 2, dependencies);
        }

        // depth
        {
            vk::AttachmentDescription attachments[1] = {
                // depth 
                vk::AttachmentDescription({},
                depth_format_,
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eDepthStencilAttachmentOptimal),
            };

            vk::AttachmentReference references[1] = {
                // depth
                vk::AttachmentReference(0,
                vk::ImageLayout::eDepthStencilAttachmentOptimal),
            };

            vk::SubpassDescription subpass = vk::SubpassDescription({},
                                                                    vk::PipelineBindPoint::eGraphics,
                                                                    0, nullptr,
                                                                    0, nullptr,
                                                                    nullptr,
                                                                    &references[0],
                                                                    0, nullptr);
            vk::SubpassDependency dependencies[2] = {
                vk::SubpassDependency(
                    VK_SUBPASS_EXTERNAL,
                    0,
                    vk::PipelineStageFlagBits::eBottomOfPipe,
                    vk::PipelineStageFlagBits::eLateFragmentTests,
                    vk::AccessFlagBits::eHostWrite,
                    vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                    vk::DependencyFlagBits::eByRegion),
                vk::SubpassDependency(
                    0,
                    VK_SUBPASS_EXTERNAL,
                    vk::PipelineStageFlagBits::eLateFragmentTests,
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                    vk::AccessFlagBits::eShaderRead,
                    vk::DependencyFlagBits::eByRegion)
            };
            p_rp_depth_->create(1, attachments,
                                1, &subpass,
                                2, dependencies);
        }
    }

    void destroy_render_passes_()
    {
        delete p_rp_simple_;
        delete p_rp_depth_;
    }

    /* ---------------------------------------------------------- */

    base::Swapchain *p_swapchain_{nullptr};

    void init_swapchain_()
    {
        p_swapchain_ = new base::Swapchain(p_phy_dev_,
                                           p_dev_,
                                           surface_,
                                           surface_format_,
                                           depth_format_,
                                           BACK_BUFFER_COUNT,
                                           p_rp_simple_);
        p_swapchain_->resize(p_info_->width(), p_info_->height());
        auto extent = p_swapchain_->curr_extent();
        if (extent.width != p_info_->width() || extent.height != p_info_->height()) {
            p_info_->on_resize(extent.width, extent.height);
        }
    }

    void destroy_swapchain_()
    {
        p_swapchain_->detach();
        delete p_swapchain_;
    }

    /* ---------------------------------------------------------- */

    struct Mipmap_level_info
    {
        glm::ivec2 src_start;
        glm::ivec2 dst_start;
        glm::ivec2 dst_mipmap_size;
        glm::ivec2 src_image_size;
    } level_info_;

    struct Visibility_consts
    {
        uint32_t inst_total;
        uint32_t use_occluder_culling;
    } visibility_consts_;

    vk::Framebuffer depth_prepass_framebuffer_;
    base::Render_target *p_depth_src_{nullptr};
    base::Render_target *p_depth_staging_{nullptr};
    base::Render_target *p_depth_dst_{nullptr};
    vk::Sampler depth_src_sampler_;

    void init_depth_resources_()
    {
        depth_src_sampler_ = p_dev_->dev.createSampler(vk::SamplerCreateInfo{{},
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
                                                       1.f});

        // src
        p_depth_src_ = new base::Render_target(p_phy_dev_, p_dev_,
                                               depth_format_,
                                               {p_info_->MAX_DEPTH_IMAGE_WIDTH, p_info_->MAX_DEPTH_IMAGE_HEIGHT},
                                               vk::ImageUsageFlagBits::eDepthStencilAttachment |
                                               vk::ImageUsageFlagBits::eSampled,
                                               vk::ImageAspectFlagBits::eDepth,
                                               vk::SampleCountFlagBits::e1,
                                               false);
        p_depth_src_->desc_image_info = {
            depth_src_sampler_,
            p_depth_src_->view,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal
        };

        depth_prepass_framebuffer_ =
            p_dev_->dev.createFramebuffer(vk::FramebufferCreateInfo({},
                                                                    p_rp_depth_->rp,
                                                                    1,
                                                                    &p_depth_src_->view,
                                                                    p_info_->MAX_DEPTH_IMAGE_WIDTH,
                                                                    p_info_->MAX_DEPTH_IMAGE_HEIGHT,
                                                                    1));

        // staging
        p_depth_staging_ = new base::Render_target(p_phy_dev_,
                                                   p_dev_,
                                                   vk::Format::eR32Sfloat,
                                                   {p_info_->MAX_DEPTH_STAGING_IMAGE_WIDTH, p_info_->MAX_DEPTH_STAGING_IMAGE_HEIGHT},
                                                   vk::ImageUsageFlagBits::eStorage |
                                                   vk::ImageUsageFlagBits::eTransferSrc,
                                                   vk::ImageAspectFlagBits::eColor,
                                                   vk::SampleCountFlagBits::e1,
                                                   false);
        p_depth_staging_->desc_image_info = {{}, p_depth_staging_->view, vk::ImageLayout::eGeneral};

        // dst
        auto depth_dst_sampler_info = vk::SamplerCreateInfo{{},
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
            float(base::get_mip_levels(p_info_->MAX_DEPTH_IMAGE_WIDTH,
                                       p_info_->MAX_DEPTH_IMAGE_HEIGHT)) - 1.f};
        p_depth_dst_ = new base::Render_target(p_phy_dev_, p_dev_,
                                               vk::Format::eR32Sfloat,
                                               {p_info_->MAX_DEPTH_IMAGE_WIDTH, p_info_->MAX_DEPTH_IMAGE_HEIGHT},
                                               vk::ImageUsageFlagBits::eTransferDst |
                                               vk::ImageUsageFlagBits::eSampled,
                                               vk::ImageAspectFlagBits::eColor,
                                               vk::SampleCountFlagBits::e1,
                                               true,
                                               depth_dst_sampler_info,
                                               vk::ImageLayout::eShaderReadOnlyOptimal,
                                               true);
    }

    void destroy_depth_resources_()
    {
        p_dev_->dev.destroyFramebuffer(depth_prepass_framebuffer_);
        p_dev_->dev.destroySampler(depth_src_sampler_);
        delete p_depth_dst_;
        delete p_depth_staging_;
        delete p_depth_src_;
    }

    /* ---------------------------------------------------------- */

    vk::DescriptorPool desc_pool_;

    struct Descriptor_set_layouts
    {
        vk::DescriptorSetLayout frame_data;
        vk::DescriptorSetLayout font_tex;
        vk::DescriptorSetLayout depth_staging;
        vk::DescriptorSetLayout visibility;
    } desc_set_layouts_;

    vk::DescriptorSet desc_set_font_tex_;
    vk::DescriptorSet desc_set_depth_staging_;
    vk::DescriptorSet desc_set_visibility_;

    void init_descriptors_()
    {
        // layout

        vk::DescriptorSetLayoutBinding bindings[3];
        // frame_data
        bindings[0] = {
            0,
            vk::DescriptorType::eUniformBufferDynamic,
            1,
            vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eCompute};
        desc_set_layouts_.frame_data = p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo({}, 1, bindings));

        // font_tex
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        desc_set_layouts_.font_tex = p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo({}, 1, bindings));

        // compute_depth_staging
        bindings[0] = {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        desc_set_layouts_.depth_staging = p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo({}, 2, bindings));

        // compute visibility
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[1] = {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute};
        bindings[2] = {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute};
        desc_set_layouts_.visibility = p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo({}, 3, bindings));

        // pool
        std::vector<vk::DescriptorPoolSize> pool_sizes =
        {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, frame_data_count_),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 1),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 2),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 3)
        };
        desc_pool_ = p_dev_->dev.createDescriptorPool(
            vk::DescriptorPoolCreateInfo({},
                                         frame_data_count_ + 6,
                                         static_cast<uint32_t>(pool_sizes.size()),
                                         pool_sizes.data()));

        // desc set
        std::vector<vk::DescriptorSetLayout> set_layouts;
        std::vector<vk::WriteDescriptorSet> writes;
        for (auto i = 0; i < frame_data_count_; i++) {
            set_layouts.push_back(desc_set_layouts_.frame_data);
        }
        set_layouts.push_back(desc_set_layouts_.font_tex);
        set_layouts.push_back(desc_set_layouts_.depth_staging);
        set_layouts.push_back(desc_set_layouts_.visibility);

        std::vector<vk::DescriptorSet> desc_sets =
            p_dev_->dev.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(desc_pool_,
                                                                             static_cast<uint32_t>(set_layouts.size()),
                                                                             set_layouts.data()));
        uint32_t idx = 0;
        // frame_data
        for (auto &data : frame_data_vector_) {
            data.desc_set = desc_sets[idx++];
            writes.emplace_back(data.desc_set,
                                0, 0,
                                1, vk::DescriptorType::eUniformBufferDynamic, nullptr,
                                &p_global_uniforms_->desc_buf_info, nullptr);
        }
        // font_tex
        desc_set_font_tex_ = desc_sets[idx++];
        writes.emplace_back(desc_set_font_tex_,
                            0, 0,
                            1, vk::DescriptorType::eCombinedImageSampler, &p_text_overlay_->p_font->p_tex->desc_image_info);
        // depth_staging
        desc_set_depth_staging_ = desc_sets[idx++];
        writes.emplace_back(desc_set_depth_staging_,
                            0, 0,
                            1, vk::DescriptorType::eStorageImage,
                            &p_depth_staging_->desc_image_info);
        writes.emplace_back(desc_set_depth_staging_,
                            1, 0,
                            1, vk::DescriptorType::eCombinedImageSampler,
                            &p_depth_src_->desc_image_info);
        // visibility
        desc_set_visibility_ = desc_sets[idx];
        writes.emplace_back(desc_set_visibility_,
                            0, 0,
                            1, vk::DescriptorType::eCombinedImageSampler,
                            &p_depth_dst_->desc_image_info);
        writes.emplace_back(desc_set_visibility_,
                            1, 0,
                            1, vk::DescriptorType::eStorageBuffer,
                            nullptr,
                            &p_model_->p_inst_data_buffer->desc_buf_info);
        writes.emplace_back(desc_set_visibility_,
                            2, 0,
                            1, vk::DescriptorType::eStorageBuffer,
                            nullptr,
                            &p_model_->p_mdi_no_batching_cmd_buffer->desc_buf_info);
        p_dev_->dev.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                         writes.data(),
                                         0, nullptr);
    }

    void destroy_descriptors_()
    {
        p_dev_->dev.destroyDescriptorPool(desc_pool_);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.frame_data);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.font_tex);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.depth_staging);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.visibility);
    }

    /* ---------------------------------------------------------- */

    base::Shader *p_simple_vs_{nullptr};
    base::Shader *p_simple_fs_{nullptr};
    base::Shader *p_copy_comp_{nullptr};
    base::Shader *p_mipmap_comp_{nullptr};
    base::Shader *p_visibility_comp_{nullptr};

    void init_shaders_()
    {
        p_simple_vs_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eVertex);
        p_simple_fs_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eFragment);
        p_copy_comp_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eCompute);
        p_mipmap_comp_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eCompute);
        p_visibility_comp_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eCompute);

        auto dir = base::data_dir() + "shaders/";
        p_simple_vs_->generate(dir + "simple.vert.spv");
        p_simple_fs_->generate(dir + "simple.frag.spv");
        p_copy_comp_->generate(dir + "copy.comp.spv");
        p_mipmap_comp_->generate(dir + "mipmap.comp.spv");
        p_visibility_comp_->generate(dir + "visibility.comp.spv");
    }

    void destroy_shaders_()
    {
        delete p_simple_vs_;
        delete p_simple_fs_;
        delete p_copy_comp_;
        delete p_mipmap_comp_;
        delete p_visibility_comp_;
    }

    /* ---------------------------------------------------------- */

    struct Pipelines
    {
        vk::Pipeline simple;
        vk::Pipeline simple_blending;
        vk::Pipeline simple_text;
        vk::Pipeline depth;
        vk::Pipeline copy_compute;
        vk::Pipeline mipmap_compute;
        vk::Pipeline visibility_compute;
    } pipelines_;

    struct Pipeline_layouts
    {
        vk::PipelineLayout simple;
        vk::PipelineLayout text;
        vk::PipelineLayout depth;
        vk::PipelineLayout depth_compute;
        vk::PipelineLayout visibility_compute;
    } pipeline_layouts_;

    void init_pipelines_()
    {
        vk::DescriptorSetLayout layouts[3] = {
            desc_set_layouts_.frame_data,
            p_model_->desc_set_layout,
            desc_set_layouts_.font_tex,
        };
        // pipeline layouts
        pipeline_layouts_.simple = p_dev_->dev.createPipelineLayout(
            vk::PipelineLayoutCreateInfo({},
                                         2, layouts,
                                         0, nullptr));
        pipeline_layouts_.text = p_dev_->dev.createPipelineLayout(
            vk::PipelineLayoutCreateInfo({},
                                         1, &layouts[2],
                                         0, nullptr));
        pipeline_layouts_.depth = p_dev_->dev.createPipelineLayout(
            vk::PipelineLayoutCreateInfo({},
                                         1, &layouts[0],
                                         0, nullptr));

        vk::PushConstantRange compute_ranges[2] = {
            vk::PushConstantRange(
                vk::ShaderStageFlagBits::eCompute,
                0,
                sizeof(Mipmap_level_info)),
            vk::PushConstantRange(
                vk::ShaderStageFlagBits::eCompute,
                0,
                sizeof(Visibility_consts))
        };
        pipeline_layouts_.depth_compute = p_dev_->dev.createPipelineLayout(
            vk::PipelineLayoutCreateInfo({},
                                         1, &desc_set_layouts_.depth_staging,
                                         1, &compute_ranges[0]));
        layouts[0] = desc_set_layouts_.visibility;
        layouts[1] = desc_set_layouts_.frame_data;
        pipeline_layouts_.visibility_compute = p_dev_->dev.createPipelineLayout(
            vk::PipelineLayoutCreateInfo({},
                                         2, layouts,
                                         1, &compute_ranges[1]));

        // pipelines
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state(
            {},
            vk::PrimitiveTopology::eTriangleList, // topology
            VK_FALSE // primitive restart enable
        );

        vk::PipelineRasterizationStateCreateInfo rasterization_state(
            {},
            VK_FALSE, // depth clamp enable
            VK_FALSE, // rasterizer discard
            vk::PolygonMode::eFill, // polygon mode
            vk::CullModeFlagBits::eBack, // cull mode
            vk::FrontFace::eCounterClockwise, // front face
            VK_FALSE, // depth bias
            0, 0, 0, 1.f);

        vk::PipelineDepthStencilStateCreateInfo depth_stencil_state(
            {},
            VK_TRUE, // depth test enable
            VK_TRUE, // depth write enable
            vk::CompareOp::eLessOrEqual, // depth compare op
            VK_FALSE, // depth bounds test enable
            VK_FALSE, // stencil test enable
            vk::StencilOpState(),
            vk::StencilOpState(),
            0.f, // min depth bounds
            0.f); // max depth bounds

        vk::PipelineColorBlendAttachmentState blend_attachment_state{
            VK_FALSE,
            vk::BlendFactor::eSrcColor,
            vk::BlendFactor::eOneMinusSrcColor,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOne,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA
        };

        vk::PipelineColorBlendStateCreateInfo color_blend_state(
            {},
            VK_FALSE,  // logic op enable
            vk::LogicOp::eClear, // logic op
            1, // attachment count
            &blend_attachment_state, // attachments
            std::array<float, 4> {1.f, 1.f, 1.f, 1.f} // blend constants
        );

        vk::PipelineMultisampleStateCreateInfo multisample_state(
            {},
            vk::SampleCountFlagBits::e1, // sample count
            VK_FALSE, // sample shading enable
            0.f, // min sample shading
            nullptr, // sample mask
            VK_FALSE, // alpha to coverage enable
            VK_FALSE);// alpha to one enable

        std::vector<vk::DynamicState> dynamic_states =
        {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamic_state_ci(
            {},
            2,
            dynamic_states.data());

        vk::PipelineViewportStateCreateInfo viewport_state(
            {},
            1, nullptr,
            1, nullptr);

        vk::PipelineShaderStageCreateInfo shader_stages[2];
        shader_stages[0] = p_simple_vs_->create_pipeline_stage_info();
        shader_stages[1] = p_simple_fs_->create_pipeline_stage_info();

        vk::PipelineVertexInputStateCreateInfo vertex_input_state{
            {},
            p_model_->vi_bindings.size(),
            p_model_->vi_bindings.data(),
            p_model_->vi_attribs.size(),
            p_model_->vi_attribs.data()
        };

        vk::GraphicsPipelineCreateInfo pipeline_ci(
            {},
            2,
            shader_stages,
            &vertex_input_state,
            &input_assembly_state,
            nullptr,
            &viewport_state,
            &rasterization_state,
            &multisample_state,
            &depth_stencil_state,
            &color_blend_state,
            &dynamic_state_ci,
            pipeline_layouts_.simple,
            p_rp_simple_->rp,
            0); // subpass

        // simple

        pipelines_.simple = p_dev_->dev.createGraphicsPipeline(
            nullptr, pipeline_ci);

        // simple blending

        blend_attachment_state.blendEnable = VK_TRUE;
        depth_stencil_state.depthTestEnable = VK_FALSE;
        depth_stencil_state.depthWriteEnable = VK_FALSE;

        pipelines_.simple_blending = p_dev_->dev.createGraphicsPipeline(
            nullptr, pipeline_ci);

        // text

        blend_attachment_state.colorBlendOp = vk::BlendOp::eAdd;
        blend_attachment_state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        blend_attachment_state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blend_attachment_state.alphaBlendOp = vk::BlendOp::eAdd;
        blend_attachment_state.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
        blend_attachment_state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;

        auto text_vi_state = vk::PipelineVertexInputStateCreateInfo(
            {},
            p_text_overlay_->vi_binding_count,
            p_text_overlay_->vi_bindings,
            p_text_overlay_->vi_attrib_count,
            p_text_overlay_->vi_attribs
        );
        pipeline_ci.pVertexInputState = &text_vi_state;

        vk::PipelineShaderStageCreateInfo tex_shader_stages[2] = {
            p_text_overlay_->p_vs->create_pipeline_stage_info(),
            p_text_overlay_->p_fs->create_pipeline_stage_info()
        };
        pipeline_ci.pStages = tex_shader_stages;
        pipeline_ci.stageCount = 2;

        pipeline_ci.layout = pipeline_layouts_.text;
        pipelines_.simple_text = p_dev_->dev.createGraphicsPipeline(nullptr, pipeline_ci);

        // depth

        pipeline_ci.pVertexInputState = &vertex_input_state;
        pipeline_ci.pStages = shader_stages;
        pipeline_ci.stageCount = 1;

        blend_attachment_state.blendEnable = VK_FALSE;
        color_blend_state.attachmentCount = 0;
        depth_stencil_state.depthTestEnable = VK_TRUE;
        depth_stencil_state.depthWriteEnable = VK_TRUE;

        pipeline_ci.layout = pipeline_layouts_.depth;
        pipeline_ci.renderPass = p_rp_depth_->rp;
        pipelines_.depth = p_dev_->dev.createGraphicsPipeline(nullptr, pipeline_ci);

        // compute

        pipelines_.copy_compute = p_dev_->dev.createComputePipeline(
            nullptr,
            vk::ComputePipelineCreateInfo(
                {},
                p_copy_comp_->create_pipeline_stage_info(),
                pipeline_layouts_.depth_compute));
        pipelines_.mipmap_compute = p_dev_->dev.createComputePipeline(
            nullptr,
            vk::ComputePipelineCreateInfo(
                {},
                p_mipmap_comp_->create_pipeline_stage_info(),
                pipeline_layouts_.depth_compute));
        pipelines_.visibility_compute = p_dev_->dev.createComputePipeline(
            nullptr,
            vk::ComputePipelineCreateInfo(
                {},
                p_visibility_comp_->create_pipeline_stage_info(),
                pipeline_layouts_.visibility_compute));
    }

    void destroy_pipelines_()
    {
        p_dev_->dev.destroyPipeline(pipelines_.simple);
        p_dev_->dev.destroyPipeline(pipelines_.simple_blending);
        p_dev_->dev.destroyPipeline(pipelines_.simple_text);
        p_dev_->dev.destroyPipeline(pipelines_.depth);
        p_dev_->dev.destroyPipeline(pipelines_.copy_compute);
        p_dev_->dev.destroyPipeline(pipelines_.mipmap_compute);
        p_dev_->dev.destroyPipeline(pipelines_.visibility_compute);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.simple);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.text);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.depth);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.depth_compute);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.visibility_compute);
    }

    /* ---------------------------------------------------------- */

    void detect_window_resize_() const
    {
        if (p_info_->resize_flag) {
            p_info_->resize_flag = false;
            p_swapchain_->resize(p_info_->width(), p_info_->height());
        }
    }

    /* ---------------------------------------------------------- */

    void acquire_back_buffer_() override
    {
        auto &back = back_buffers_.front();

        p_dev_->dev.waitForFences(1, &back.present_queue_submit_fence, VK_TRUE, UINT64_MAX);
        p_dev_->dev.resetFences(1, &back.present_queue_submit_fence);

        detect_window_resize_();

        vk::Result res = vk::Result::eTimeout;
        while (res != vk::Result::eSuccess) {

            res = p_dev_->dev.acquireNextImageKHR(
                p_swapchain_->swapchain,
                UINT64_MAX,
                back.swapchain_image_acquire_semaphore,
                vk::Fence(),
                &back.swapchain_image_idx);
            if (res == vk::Result::eErrorOutOfDateKHR) {
                p_swapchain_->resize(0, 0);
                p_shell_->post_quit_msg();
            } else {
                assert(res == vk::Result::eSuccess);
            }
        }

        acquired_back_buf_ = back;
        back_buffers_.pop_front();
    }

    void present_back_buffer_(float elapsed_time, float delta_time) override
    {
        on_frame_(elapsed_time, delta_time);

        auto &back = acquired_back_buf_;
        vk::PresentInfoKHR present_info(1, &back.compute_complete_semaphore,
                                        1, &p_swapchain_->swapchain,
                                        &back.swapchain_image_idx);
        base::assert_success(p_dev_->present_queue.presentKHR(present_info));
        p_dev_->present_queue.submit(0, nullptr, back.present_queue_submit_fence);

        back_buffers_.push_back(back);
    }

    void update_uniforms_(Frame_data &data)
    {
        ubo_.model = p_model_->model_matrix;
        ubo_.normal = p_model_->normal_matrix;
        ubo_.view = p_camera_->view;
        ubo_.projection_clip = p_camera_->clip * p_camera_->projection;
        ubo_.cam_near = p_camera_->cam_near;
        ubo_.cam_far = p_camera_->cam_far;
        ubo_.resolution.x = p_info_->width();
        ubo_.resolution.y = p_info_->height();

        auto mapped = reinterpret_cast<UBO *>(data.mapped);
        memcpy(mapped, &ubo_, sizeof(UBO));
    }

    void update_push_constants_(int level)
    {
        if (level == 0) {
            // at depth_src image
            level_info_.src_start.x = 0;
            level_info_.src_start.y = 0;

            // at depth_staging image
            level_info_.dst_start.x = 0;
            level_info_.dst_start.y = 0;

            // at depth_staging image
            auto extent = p_swapchain_->curr_extent();
            level_info_.dst_mipmap_size.x = extent.width;
            level_info_.dst_mipmap_size.y = extent.height;

            // at depth_src image
            level_info_.src_image_size.x = p_info_->MAX_DEPTH_IMAGE_WIDTH;
            level_info_.src_image_size.y = p_info_->MAX_DEPTH_IMAGE_HEIGHT;
        } else {
            // at depth_staging image
            level_info_.src_start.x = level_info_.dst_start.x;
            level_info_.src_start.y = level_info_.dst_start.y;

            // at depth_staging image
            if (level % 2 == 0) {
                level_info_.dst_start.y += level_info_.dst_mipmap_size.y;
            } else {
                level_info_.dst_start.x += level_info_.dst_mipmap_size.x;
            }

            // at depth_staging image
            level_info_.dst_mipmap_size.x = std::max(1, level_info_.dst_mipmap_size.x / 2);
            level_info_.dst_mipmap_size.y = std::max(1, level_info_.dst_mipmap_size.y / 2);
        }

        visibility_consts_.inst_total = p_model_->mdi_no_batching_cmd_draw_info.draw_count;
        visibility_consts_.use_occluder_culling = static_cast<uint32_t>(p_info_->mode() >= 3);
    }

    void generate_text_(Frame_data &data, std::string &text)
    {
        std::stringstream ss;
        ss << p_phy_dev_->props.deviceName << "\n";
        ss << "resolution: " << p_info_->width() << " x " << p_info_->height() << "\n";
        ss << "------------------------------\n";
        auto mode = p_info_->mode();
        switch (mode) {
            case 1: ss << "multi-draw indirect batched\n"; break;
            case 2: ss << "multi-draw indirect per instance\nw/ frustum culling\n"; break;
            case 3: ss << "multi-draw indirect per instance\nw/ frustum and occlusion culling\n"; break;
            case 4: ss << "multi-draw indirect per instance\nw/ frustum and occlusion culling (blending enabled)\n"; break;
            default:break;
        }
        ss << "------------------------------\n";
        ss << "onscreen: ";
        ss << base::timestamp_str(data.query_data.data[QUERY_ONSCREEN_STOP] - data.query_data.data[QUERY_ONSCREEN_START]) << " ms\n";
        if (mode > 1) {
            ss << "depth prepass: ";
            ss << base::timestamp_str(data.query_data.data[QUERY_DEPTH_STOP] - data.query_data.data[QUERY_DEPTH_START]) << " ms\n";
            ss << "transfer: ";
            ss << base::timestamp_str(data.query_data.data[QUERY_TRANSFER_STOP] - data.query_data.data[QUERY_TRANSFER_START]) << " ms\n";
            ss << "compute mipchain: ";
            ss << base::timestamp_str(data.query_data.data[QUERY_COMPUTE_MIPCHAIN_STOP] - data.query_data.data[QUERY_COMPUTE_MIPCHAIN_START]) << " ms\n";
            ss << "compute visibility: ";
            ss << base::timestamp_str(data.query_data.data[QUERY_COMPUTE_VISIBILITY_STOP] - data.query_data.data[QUERY_COMPUTE_VISIBILITY_START]) << " ms\n";
        }
        text = ss.str();
    }

    std::string text_overlay_content_;
    bool first_invocation_depth_staging_ = true;
    bool first_invocation_depth_dst_ = true;

    void on_frame_(float elapsed_time, float delta_time)
    {
        const vk::DeviceSize vb_offset{0};

        auto &data = frame_data_vector_[frame_data_idx_];
        auto &back = acquired_back_buf_;

        // graphics
        {
            int query_count = 0;

            base::assert_success(p_dev_->dev.waitForFences(1,
                                                           &data.graphics_submit_fence,
                                                           VK_TRUE,
                                                           UINT64_MAX));
            p_dev_->dev.resetFences(1, &data.graphics_submit_fence);

            update_uniforms_(data);
            if (fps_counter_.frame_count() == 0) {
                generate_text_(data, text_overlay_content_);
                p_text_overlay_->update_text(text_overlay_content_, 0.05, 0.1, 16, p_info_->width(), p_info_->height());
            }

            auto &cmd_buf = data.graphics_cmd_buffer;
            cmd_buf.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

            // transfer

            if (!first_invocation_depth_staging_) {
                cmd_buf.resetQueryPool(data.query_pool, QUERY_TRANSFER_START, 2);

                // depth_dst layout from shader read to transfer dst
                vk::ImageMemoryBarrier barrier{
                    first_invocation_depth_dst_ ? vk::AccessFlags() : vk::AccessFlagBits::eShaderRead,
                    vk::AccessFlagBits::eTransferWrite,
                    first_invocation_depth_dst_ ? vk::ImageLayout::eUndefined : vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::ImageLayout::eTransferDstOptimal,
                    first_invocation_depth_dst_ ? 0 : p_phy_dev_->compute_queue_family_idx,
                    first_invocation_depth_dst_ ? 0 : p_phy_dev_->graphics_queue_family_idx,
                    p_depth_dst_->image,
                    vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor,
                    0, p_depth_dst_->mip_levels,
                    0, 1}};
                cmd_buf.pipelineBarrier(first_invocation_depth_dst_ ? vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eComputeShader,
                                        vk::PipelineStageFlagBits::eTransfer,
                                        vk::DependencyFlagBits::eByRegion,
                                        0, nullptr,
                                        0, nullptr,
                                        1, &barrier);
                first_invocation_depth_dst_ = false;

                // copy depth_staging from last frame to depth_dst

                query_count += 2;
                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, data.query_pool, QUERY_TRANSFER_START);

                auto extent = p_swapchain_->curr_extent();
                auto w = static_cast<int>(extent.width);
                auto h = static_cast<int>(extent.height);
                std::array<vk::Offset3D, 2>bounds_in{vk::Offset3D{0, 0, 0}, vk::Offset3D{w, h, 1}};
                std::array<vk::Offset3D, 2>bounds_out{vk::Offset3D{0, 0, 0}, vk::Offset3D{w, h, 1}};
                const auto subres_in = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);

                std::vector<vk::ImageBlit> image_blits(p_depth_dst_->mip_levels);
                for (int i = 0; i < p_depth_dst_->mip_levels; i++) {
                    auto subres_out = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);
                    image_blits[i] = {subres_in, bounds_in, subres_out, bounds_out};
                    

                    // src
                    if (i % 2 == 0) {
                        bounds_in[0].x += w;
                    } else {
                        bounds_in[0].y += h;
                    }

                    w = std::max(1, w / 2);
                    h = std::max(1, h / 2);

                    bounds_in[1].x = bounds_in[0].x + w;
                    bounds_in[1].y = bounds_in[0].y + h;

                    // dst
                    bounds_out[1].x = w;
                    bounds_out[1].y = h;
                }
                cmd_buf.blitImage(p_depth_staging_->image,
                                      vk::ImageLayout::eGeneral,
                                      p_depth_dst_->image,
                                      vk::ImageLayout::eTransferDstOptimal,
                                      p_depth_dst_->mip_levels, image_blits.data(),
                                      vk::Filter::eNearest);

                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eAllCommands, data.query_pool, QUERY_TRANSFER_STOP);

                // depth_dst layout from transfer dst to shader read 
                barrier = {
                    vk::AccessFlagBits::eTransferWrite,
                    vk::AccessFlagBits::eShaderRead,
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                    p_phy_dev_->graphics_queue_family_idx,
                    p_phy_dev_->compute_queue_family_idx,
                    p_depth_dst_->image,
                    vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor,
                    0, p_depth_dst_->mip_levels,
                    0, 1}};
                cmd_buf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                        vk::PipelineStageFlagBits::eComputeShader,
                                        vk::DependencyFlagBits::eByRegion,
                                        0, nullptr,
                                        0, nullptr,
                                        1, &barrier);
            }

            cmd_buf.setViewport(0, 1, &p_swapchain_->onscreen_viewport);
            cmd_buf.setScissor(0, 1, &p_swapchain_->onscreen_scissor);

            // depth
            {
                cmd_buf.resetQueryPool(data.query_pool, QUERY_DEPTH_START, 2);

                auto &rp_begin = p_rp_depth_->rp_begin;
                rp_begin.renderArea.extent = p_swapchain_->curr_extent();
                rp_begin.framebuffer = depth_prepass_framebuffer_;
                cmd_buf.beginRenderPass(&rp_begin, vk::SubpassContents::eInline);

                query_count += 2;
                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_DEPTH_START);

                cmd_buf.bindIndexBuffer(p_model_->p_geometries->p_idx_buffer->buf, 0, vk::IndexType::eUint32);
                cmd_buf.bindVertexBuffers(0, 1, &p_model_->p_geometries->p_vert_buffer->buf, &vb_offset);
                cmd_buf.bindVertexBuffers(1, 1, &p_model_->p_inst_attribs_buffer->buf, &vb_offset);

                cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                           pipeline_layouts_.depth,
                                           0, 1,
                                           &data.desc_set,
                                           1, &data.dynamic_offset);

                cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                     pipelines_.depth);

                cmd_buf.drawIndexedIndirect(p_model_->mdi_cmd_draw_info.indirect_cmd_buffer,
                                            p_model_->mdi_cmd_draw_info.offset,
                                            p_model_->mdi_cmd_draw_info.draw_count,
                                            p_model_->mdi_cmd_draw_info.stride);

                // write timestamp
                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eLateFragmentTests, data.query_pool, QUERY_DEPTH_STOP);

                cmd_buf.endRenderPass();
            }

            // simple

            {
                cmd_buf.resetQueryPool(data.query_pool, QUERY_ONSCREEN_START, 2);

                auto &rp_begin = p_rp_simple_->rp_begin;
                rp_begin.renderArea.extent = p_swapchain_->curr_extent();
                rp_begin.framebuffer = p_swapchain_->framebuffers[back.swapchain_image_idx];
                cmd_buf.beginRenderPass(&rp_begin, vk::SubpassContents::eInline);

                query_count += 2;
                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_ONSCREEN_START);

                cmd_buf.bindIndexBuffer(p_model_->p_geometries->p_idx_buffer->buf, 0, vk::IndexType::eUint32);
                cmd_buf.bindVertexBuffers(0, 1, &p_model_->p_geometries->p_vert_buffer->buf, &vb_offset);
                cmd_buf.bindVertexBuffers(1, 1, &p_model_->p_inst_attribs_buffer->buf, &vb_offset);

                vk::DescriptorSet desc_sets[2] = {
                    data.desc_set,
                    p_model_->desc_set
                };
                cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                           pipeline_layouts_.simple,
                                           0, 2,
                                           desc_sets,
                                           1, &data.dynamic_offset);

                if (p_info_->mode() < 4)
                    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                         pipelines_.simple);
                else
                    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                         pipelines_.simple_blending);


                if (p_info_->mode() == 1) {
                    cmd_buf.drawIndexedIndirect(p_model_->mdi_cmd_draw_info.indirect_cmd_buffer,
                                                p_model_->mdi_cmd_draw_info.offset,
                                                p_model_->mdi_cmd_draw_info.draw_count,
                                                p_model_->mdi_cmd_draw_info.stride);
                } else if (p_info_->mode() >= 2) {
                    cmd_buf.drawIndexedIndirect(p_model_->mdi_no_batching_cmd_draw_info.indirect_cmd_buffer,
                                                p_model_->mdi_no_batching_cmd_draw_info.offset,
                                                p_model_->mdi_no_batching_cmd_draw_info.draw_count,
                                                p_model_->mdi_no_batching_cmd_draw_info.stride);
                }

                // text

                cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                           pipeline_layouts_.text,
                                           0, 1,
                                           &desc_set_font_tex_,
                                           0, nullptr);
                cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                     pipelines_.simple_text);
                cmd_buf.bindVertexBuffers(0, 1, &p_text_overlay_->p_vert_buf->buf, &vb_offset);
                cmd_buf.bindIndexBuffer(p_text_overlay_->p_idx_buf->buf, 0, vk::IndexType::eUint32);
                cmd_buf.drawIndexed(p_text_overlay_->draw_index_count, 1, 0, 0, 0);

                // write timestamp
                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eColorAttachmentOutput, data.query_pool, QUERY_ONSCREEN_STOP);

                cmd_buf.endRenderPass();
            }

            cmd_buf.end();

            const vk::PipelineStageFlags wait_stages{vk::PipelineStageFlagBits::eColorAttachmentOutput};
            auto submit_info = vk::SubmitInfo(1, &back.swapchain_image_acquire_semaphore,
                                              &wait_stages,
                                              1, &cmd_buf,
                                              1, &back.onscreen_render_semaphore);

            base::assert_success(p_dev_->graphics_queue.submit(
                1,
                &submit_info,
                data.graphics_submit_fence));

            // get query results
            base::assert_success(vkGetQueryPoolResults(static_cast<VkDevice>(p_dev_->dev),
                                                       static_cast<VkQueryPool>(data.query_pool),
                                                       0, query_count,
                                                       sizeof(uint32_t) * query_count,
                                                       &data.query_data,
                                                       sizeof(uint32_t),
                                                       static_cast<VkQueryResultFlagBits>(vk::QueryResultFlagBits::eWait)));
        }

        // compute

        {
            base::assert_success(p_dev_->dev.waitForFences(1,
                                                           &data.compute_submit_fence,
                                                           VK_TRUE,
                                                           UINT64_MAX));
            p_dev_->dev.resetFences(1, &data.compute_submit_fence);

            auto &cmd_buf = data.compute_cmd_buffer;
            cmd_buf.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

            cmd_buf.resetQueryPool(data.query_pool, QUERY_COMPUTE_MIPCHAIN_START, 2);

            int query_count = 2;
            cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_COMPUTE_MIPCHAIN_START);

            cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                       pipeline_layouts_.depth_compute,
                                       0, 1, &desc_set_depth_staging_,
                                       0, nullptr);
            // copy pipeline
            cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines_.copy_compute);
            update_push_constants_(0);
            cmd_buf.pushConstants(pipeline_layouts_.depth_compute,
                                  vk::ShaderStageFlagBits::eCompute,
                                  0, sizeof(Mipmap_level_info), &level_info_);
            auto x = static_cast<uint32_t> ((level_info_.dst_mipmap_size.x - 1) / 32 + 1);
            auto y = static_cast<uint32_t> ((level_info_.dst_mipmap_size.y - 1) / 32 + 1);
            cmd_buf.dispatch(x, y, 1);

            // mipmap pipeline
            cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines_.mipmap_compute);
            for (int i = 1; i < p_depth_dst_->mip_levels; i++) {
                if (first_invocation_depth_staging_) first_invocation_depth_staging_ = false;
                update_push_constants_(i);
                cmd_buf.pushConstants(pipeline_layouts_.depth_compute,
                                      vk::ShaderStageFlagBits::eCompute,
                                      0, sizeof(Mipmap_level_info), &level_info_);
                x = static_cast<uint32_t> ((level_info_.dst_mipmap_size.x - 1) / 32 + 1);
                y = static_cast<uint32_t> ((level_info_.dst_mipmap_size.y - 1) / 32 + 1);
                cmd_buf.dispatch(x, y, 1);
            }

            cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eComputeShader, data.query_pool, QUERY_COMPUTE_MIPCHAIN_STOP);

            // visibility pipeline

            if (!first_invocation_depth_dst_) {
                cmd_buf.resetQueryPool(data.query_pool, QUERY_COMPUTE_VISIBILITY_START, 2);

                query_count += 2;
                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_COMPUTE_VISIBILITY_START);

                // read depth_dst texture from the last tranfer operations
                cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines_.visibility_compute);
                vk::DescriptorSet desc_sets[2] = {
                    desc_set_visibility_,
                    data.desc_set
                };
                cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                           pipeline_layouts_.visibility_compute,
                                           0, 2, desc_sets,
                                           1, &data.dynamic_offset);
                cmd_buf.pushConstants(pipeline_layouts_.visibility_compute,
                                      vk::ShaderStageFlagBits::eCompute,
                                      0, sizeof(Visibility_consts), &visibility_consts_);
                x = (p_model_->mdi_no_batching_cmd_draw_info.draw_count - 1) / 64 + 1;
                cmd_buf.dispatch(x, 1, 1);

                cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, QUERY_COMPUTE_VISIBILITY_STOP);
            }

            cmd_buf.end();

            const vk::PipelineStageFlags wait_stages{vk::PipelineStageFlagBits::eComputeShader};
            auto submit_info = vk::SubmitInfo(1, &back.onscreen_render_semaphore,
                                              &wait_stages,
                                              1, &cmd_buf,
                                              1, &back.compute_complete_semaphore);

            base::assert_success(p_dev_->compute_queue.submit(
                1,
                &submit_info,
                data.compute_submit_fence));

            // get query results
            base::assert_success(vkGetQueryPoolResults(static_cast<VkDevice>(p_dev_->dev),
                                                       static_cast<VkQueryPool>(data.query_pool),
                                                       QUERY_COMPUTE_MIPCHAIN_START, query_count,
                                                       sizeof(uint32_t) * query_count,
                                                       &data.query_data.data[QUERY_COMPUTE_MIPCHAIN_START],
                                                       sizeof(uint32_t),
                                                       static_cast<VkQueryResultFlagBits>(vk::QueryResultFlagBits::eWait)));
        }

        frame_data_idx_ = (frame_data_idx_ + 1) % frame_data_count_;
    }
};
