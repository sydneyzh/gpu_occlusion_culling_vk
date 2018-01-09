#pragma once
#include "Shell.hpp"
#include "Swapchain.hpp"
#include "simple.vert.h"
#include "simple.frag.h"
#include "culling.vert.h"
#include "culling.geom.h"

#define BACK_BUFFER_COUNT 3
#define FONT_FILENAME "RobotoMonoMedium"

class Program : public base::Program_base
{
public:
    Program(const bool enable_validation,
            const bool use_culling,
            Prog_info *p_info,
            Shell *p_shell,
            base::Camera *p_camera,
            const std::string &model_filename) :
        Program_base(enable_validation, p_info, p_shell),
        use_culling_(use_culling),
        p_info_(p_info),
        p_shell_(p_shell),
        p_camera_(p_camera),
        model_filename_(model_filename)
    {
        p_camera_->update_aspect(p_info->width(), p_info->height());
        req_phy_dev_features_.geometryShader = VK_TRUE;

        std::cout << ">> enable validation: " << enable_validation_ << std::endl;
        std::cout << ">> use culling: " << use_culling_ << std::endl;
    }

    ~Program() override
    {
        p_dev_->dev.waitIdle();
        destroy_pipelines_();
        destroy_shaders_();
        destroy_descriptors_();
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
        init_descriptors_();
        init_shaders_();
        init_pipelines_();
    }

private:
    const bool use_culling_;
    Prog_info * p_info_{nullptr};
    Shell *p_shell_{nullptr};
    base::Camera *p_camera_{nullptr};
    const std::string model_filename_;

    /* ---------------------------------------------------------- */

    struct Back_buffer
    {
        uint32_t swapchain_image_idx{0};
        vk::Semaphore swapchain_image_acquire_semaphore{};
        vk::Semaphore onscreen_render_semaphore{};
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
            p_dev_->dev.destroyFence(back.present_queue_submit_fence);
            back_buffers_.pop_front();
        }
    }

    /* ---------------------------------------------------------- */

    vk::CommandPool graphics_cmd_pool_;

    void init_command_pools_()
    {
        graphics_cmd_pool_ = p_dev_->dev.createCommandPool(
            vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                      p_phy_dev_->graphics_queue_family_idx));
    }

    void destroy_command_pools_()
    {
        p_dev_->dev.destroyCommandPool(graphics_cmd_pool_);
    }

    /* ---------------------------------------------------------- */

    base::Model *p_model_{nullptr};

    void init_model_()
    {
        p_model_ = new base::Model(p_phy_dev_, p_dev_, graphics_cmd_pool_);

        auto model_path = base::data_dir();
        model_path.append("models/" + model_filename_);
        auto components = std::vector<base::Vertex_component>
        {
            base::VERT_COMP_POSITION,
            base::VERT_COMP_NORMAL
        };
        base::Vertex_layout layout(components);
        p_model_->load(model_path, layout);

        // update camera
        auto di = p_model_->aabb.get_diagonal();
        p_camera_->cam_far = 3.f * glm::length(di);
        auto half_size = p_model_->aabb.get_half_size();
        p_camera_->eye_pos.x = half_size.x / 2.f;
        p_camera_->eye_pos.y = half_size.y / 2.f;
        p_camera_->eye_pos.z = half_size.z;
        p_camera_->target = {0.f, 0.f, 0.f};
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

    static const uint32_t max_query_count_{8};
    struct Query_data
    {
        uint32_t data[max_query_count_];
    };

    struct UBO
    {
        glm::mat4 model;
        glm::mat4 normal;
        glm::mat4 view;
        glm::mat4 projection_clip;
        float cam_near;
        float cam_far;
    } ubo_;

    struct Frame_data
    {
        vk::DescriptorSet desc_set;
        uint8_t *mapped{nullptr};
        uint32_t dynamic_offset{0};

        vk::CommandBuffer cmd_buffer;
        vk::Fence submit_fence;

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
        p_global_uniforms_->update_desc_buf_info(0, VK_WHOLE_SIZE);
        base::allocate_and_bind_buffer_memory(p_phy_dev_,
                                              p_dev_,
                                              global_uniforms_mem_,
                                              1, &p_global_uniforms_,
                                              aligned_size * frame_data_count_);

        std::vector<vk::CommandBuffer> graphics_cmd_buffers(frame_data_count_);
        graphics_cmd_buffers = p_dev_->dev.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo(graphics_cmd_pool_,
                                          vk::CommandBufferLevel::ePrimary,
                                          static_cast<uint32_t>(graphics_cmd_buffers.size())));
        int idx = 0;
        uint8_t *base = reinterpret_cast<uint8_t *>(p_global_uniforms_->mapped);
        for (auto &data : frame_data_vector_) {
            data.dynamic_offset = idx * aligned_size;
            data.mapped = base + idx * aligned_size;
            data.cmd_buffer = graphics_cmd_buffers[idx];
            data.submit_fence = p_dev_->dev.createFence(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
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
            p_dev_->dev.destroyFence(data.submit_fence);
            p_dev_->dev.destroyQueryPool(data.query_pool);
        }
    }

    /* ---------------------------------------------------------- */

    // rp simple 
    // subpass onscreen (color, depth)

    // rp culling
    // subpass #0 depth (depth)
    // subpass #1 onscreen (color)

    base::Render_pass *p_rp_simple_{nullptr};
    base::Render_pass *p_rp_culling_{nullptr};

    vk::Format depth_format_{vk::Format::eD32Sfloat};

    std::vector<vk::ClearValue> clear_values_ = {
        vk::ClearColorValue(std::array<float, 4>{0.f, 0.f, 0.f, 1.f}),
        vk::ClearDepthStencilValue(1.f, 0),
        vk::ClearDepthStencilValue(1.f, 0)
    };

    void init_render_passes_()
    {
        p_rp_simple_ = new base::Render_pass(p_dev_, 2, clear_values_.data());
        p_rp_culling_ = new base::Render_pass(p_dev_, 3, clear_values_.data());

        vk::AttachmentDescription attachments[3] = {
            // color
            vk::AttachmentDescription({},
            surface_format_.format,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::ePresentSrcKHR),
            // depth
            vk::AttachmentDescription({},
            depth_format_,
            vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            vk::AttachmentLoadOp::eDontCare,
            vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eDepthStencilAttachmentOptimal),
            // depth
            vk::AttachmentDescription()
        };

        attachments[2] = attachments[1]; 

        vk::AttachmentReference references[3] = {
            // color
            vk::AttachmentReference(0,
            vk::ImageLayout::eColorAttachmentOptimal),
            // depth
            vk::AttachmentReference(1,
            vk::ImageLayout::eDepthStencilAttachmentOptimal),
            // depth
            vk::AttachmentReference(2,
            vk::ImageLayout::eDepthStencilAttachmentOptimal),
        };

        vk::SubpassDescription subpasses[2];

        // rp simple

        subpasses[0] = vk::SubpassDescription({},
                                              vk::PipelineBindPoint::eGraphics,
                                              0, nullptr,
                                              1, &references[0],
                                              nullptr,
                                              &references[1],
                                              0, nullptr);

        p_rp_simple_->create(2, attachments, 
                             1, subpasses,
                             0, nullptr);

        // rp culling

        attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
        attachments[1].finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        subpasses[0] = vk::SubpassDescription({},
                                              vk::PipelineBindPoint::eGraphics,
                                              0, nullptr,
                                              0, nullptr,
                                              nullptr,
                                              &references[1],
                                              0, nullptr);
        subpasses[1] = vk::SubpassDescription({},
                                              vk::PipelineBindPoint::eGraphics,
                                              0, nullptr,
                                              1, &references[0],
                                              nullptr,
                                              &references[2],
                                              0, nullptr);

        vk::SubpassDependency dependencies[1] = {
            vk::SubpassDependency(0,
            1,
            vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::PipelineStageFlagBits::eVertexShader,
            vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits::eShaderRead,
            vk::DependencyFlagBits::eByRegion)
        };

        p_rp_culling_->create(3, attachments,
                              2, subpasses,
                              1, dependencies);
    }

    void destroy_render_passes_()
    {
        delete p_rp_simple_;
        delete p_rp_culling_;
    }

    /* ---------------------------------------------------------- */

    Swapchain *p_swapchain_{nullptr};

    void init_swapchain_()
    {
        p_swapchain_ = new Swapchain(p_phy_dev_,
                                     p_dev_,
                                     surface_,
                                     surface_format_,
                                     depth_format_,
                                     BACK_BUFFER_COUNT,
                                     use_culling_ ? p_rp_culling_ : p_rp_simple_,
                                     use_culling_ ? 3 : 2);
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

    vk::DescriptorPool desc_pool_;

    struct Descriptor_set_layouts
    {
        vk::DescriptorSetLayout frame_data;
        vk::DescriptorSetLayout depth_tex;
        vk::DescriptorSetLayout font_tex;
    } desc_set_layouts_;

    vk::DescriptorSet desc_set_font_tex_;
    vk::DescriptorSet desc_set_depth_tex_;

    void init_descriptors_()
    {
        // layout
        vk::DescriptorSetLayoutBinding binding{
            0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eVertex
        };
        desc_set_layouts_.frame_data = p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo({}, 1, &binding));

        binding = vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eVertex
        };
        desc_set_layouts_.depth_tex = p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo({}, 1, &binding));

        binding = vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment
        };
        desc_set_layouts_.font_tex = p_dev_->dev.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo({}, 1, &binding));

        // pool
        std::vector<vk::DescriptorPoolSize> pool_sizes =
        {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, frame_data_count_),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)
        };
        desc_pool_ = p_dev_->dev.createDescriptorPool(
            vk::DescriptorPoolCreateInfo({},
                                         frame_data_count_ + 2,
                                         static_cast<uint32_t>(pool_sizes.size()),
                                         pool_sizes.data()));

        // desc set
        std::vector<vk::DescriptorSetLayout> set_layouts;
        std::vector<vk::WriteDescriptorSet> writes;
        for (auto i = 0; i < frame_data_count_; i++) {
            set_layouts.push_back(desc_set_layouts_.frame_data);
        }
        set_layouts.push_back(desc_set_layouts_.depth_tex);
        set_layouts.push_back(desc_set_layouts_.font_tex);

        std::vector<vk::DescriptorSet> desc_sets =
            p_dev_->dev.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(desc_pool_,
                                                                             static_cast<uint32_t>(set_layouts.size()),
                                                                             set_layouts.data()));
        uint32_t idx = 0;
        for (auto &data : frame_data_vector_) {
            data.desc_set = desc_sets[idx++];
            writes.emplace_back(data.desc_set,
                                0, 0,
                                1, vk::DescriptorType::eUniformBufferDynamic, nullptr,
                                &p_global_uniforms_->desc_buf_info, nullptr);
        }
        desc_set_depth_tex_ = desc_sets[idx++];
        writes.emplace_back(desc_set_depth_tex_,
                            0, 0,
                            1, vk::DescriptorType::eCombinedImageSampler, &p_swapchain_->p_prepass_depth_attachment->desc_image_info);
        desc_set_font_tex_ = desc_sets[idx];
        writes.emplace_back(desc_set_font_tex_,
                            0, 0,
                            1, vk::DescriptorType::eCombinedImageSampler, &p_text_overlay_->p_font->p_tex->desc_image_info);
        p_dev_->dev.updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                         writes.data(),
                                         0, nullptr);
    }

    void destroy_descriptors_()
    {
        p_dev_->dev.destroyDescriptorPool(desc_pool_);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.frame_data);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.depth_tex);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layouts_.font_tex);
    }

    /* ---------------------------------------------------------- */

    base::Shader *p_simple_vs_{nullptr};
    base::Shader *p_simple_fs_{nullptr};
    base::Shader *p_culling_vs_{nullptr};
    base::Shader *p_culling_gs_{nullptr};

    void init_shaders_()
    {
        p_simple_vs_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eVertex);
        p_simple_fs_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eFragment);
        p_culling_vs_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eVertex);
        p_culling_gs_ = new base::Shader(p_dev_, vk::ShaderStageFlagBits::eGeometry);
        p_simple_vs_->generate(sizeof(simple_vert), simple_vert);
        p_simple_fs_->generate(sizeof(simple_frag), simple_frag);
        p_culling_vs_->generate(sizeof(culling_vert), culling_vert);
        p_culling_gs_->generate(sizeof(culling_geom), culling_geom);
    }

    void destroy_shaders_()
    {
        delete p_simple_vs_;
        delete p_simple_fs_;
        delete p_culling_vs_;
        delete p_culling_gs_;
    }

    /* ---------------------------------------------------------- */

    struct Pipelines
    {
        vk::Pipeline simple;
        vk::Pipeline depth;
        vk::Pipeline culling;
        vk::Pipeline simple_text;
        vk::Pipeline culling_text;
    } pipelines_;

    struct Pipeline_layouts
    {
        vk::PipelineLayout simple;
        vk::PipelineLayout culling;
        vk::PipelineLayout text;
    } pipeline_layouts_;

    vk::DescriptorSet desc_set_culling_[2];

    void init_pipelines_()
    {
        vk::DescriptorSetLayout layouts[3] = {
            desc_set_layouts_.frame_data,
            desc_set_layouts_.depth_tex,
            desc_set_layouts_.font_tex,
        };
        // pipeline layouts
        pipeline_layouts_.simple = p_dev_->dev.createPipelineLayout(
            vk::PipelineLayoutCreateInfo({},
                                         1, layouts,
                                         0, nullptr));
        pipeline_layouts_.culling = p_dev_->dev.createPipelineLayout(
            vk::PipelineLayoutCreateInfo({},
                                         2, layouts,
                                         0, nullptr));
        pipeline_layouts_.text = p_dev_->dev.createPipelineLayout(
            vk::PipelineLayoutCreateInfo({},
                                         1, &layouts[2],
                                         0, nullptr));
        desc_set_culling_[1] = desc_set_depth_tex_;

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
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
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

        vk::PipelineShaderStageCreateInfo shader_stages[3];
        shader_stages[0] = p_simple_vs_->create_pipeline_stage_info();
        shader_stages[1] = p_simple_fs_->create_pipeline_stage_info();

        vk::PipelineVertexInputStateCreateInfo vertex_input_state(
            {},
            1,
            &p_model_->vi_binding,
            2,
            p_model_->vi_attribs.data());

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

        // depth

        color_blend_state.attachmentCount = 0;
        pipeline_ci.stageCount = 1;
        pipeline_ci.layout = pipeline_layouts_.culling;
        pipeline_ci.renderPass = p_rp_culling_->rp;
        pipelines_.depth = p_dev_->dev.createGraphicsPipeline(
            nullptr, pipeline_ci);

        // culling

        color_blend_state.attachmentCount = 1;
        shader_stages[0] = p_culling_vs_->create_pipeline_stage_info();
        shader_stages[1] = p_culling_gs_->create_pipeline_stage_info();
        shader_stages[2] = p_simple_fs_->create_pipeline_stage_info();
        pipeline_ci.stageCount = 3;
        pipeline_ci.subpass = 1;
        pipelines_.culling = p_dev_->dev.createGraphicsPipeline(
            nullptr, pipeline_ci);

        // text

        vertex_input_state.vertexBindingDescriptionCount = p_text_overlay_->vi_binding_count;
        vertex_input_state.vertexAttributeDescriptionCount = p_text_overlay_->vi_attrib_count;
        vertex_input_state.pVertexBindingDescriptions = p_text_overlay_->vi_bindings;
        vertex_input_state.pVertexAttributeDescriptions = p_text_overlay_->vi_attribs;

        shader_stages[0] = p_text_overlay_->p_vs->create_pipeline_stage_info();
        shader_stages[1] = p_text_overlay_->p_fs->create_pipeline_stage_info();
        pipeline_ci.stageCount = 2;

        blend_attachment_state.blendEnable = VK_TRUE;
        //depth_stencil_state.depthTestEnable = VK_FALSE;
        //depth_stencil_state.depthWriteEnable = VK_FALSE;

        pipeline_ci.layout = pipeline_layouts_.text;

        pipeline_ci.flags = vk::PipelineCreateFlagBits::eAllowDerivatives;

        pipelines_.culling_text = p_dev_->dev.createGraphicsPipeline(
            nullptr, pipeline_ci);

        //

        pipeline_ci.flags = vk::PipelineCreateFlagBits::eDerivative;
        pipeline_ci.basePipelineHandle = pipelines_.culling_text;
        pipeline_ci.basePipelineIndex = -1;

        pipeline_ci.renderPass = p_rp_simple_->rp;
        pipeline_ci.subpass = 0;

        pipelines_.simple_text = p_dev_->dev.createGraphicsPipeline(
            nullptr, pipeline_ci);
    }

    void destroy_pipelines_()
    {
        p_dev_->dev.destroyPipeline(pipelines_.simple);
        p_dev_->dev.destroyPipeline(pipelines_.depth);
        p_dev_->dev.destroyPipeline(pipelines_.culling);
        p_dev_->dev.destroyPipeline(pipelines_.culling_text);
        p_dev_->dev.destroyPipeline(pipelines_.simple_text);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.simple);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.culling);
        p_dev_->dev.destroyPipelineLayout(pipeline_layouts_.text);
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
        vk::PresentInfoKHR present_info(1, &back.onscreen_render_semaphore,
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

        auto mapped = reinterpret_cast<UBO *>(data.mapped);
        memcpy(mapped, &ubo_, sizeof(UBO));
    }

    void generate_text_(Frame_data &data, std::string &text)
    {
        std::stringstream ss;
        ss << (use_culling_ ? "" : "not ") << "using culling" << "\n";
        ss << base::timestamp_str(data.query_data.data[1] - data.query_data.data[0]) << " ms";
        text = ss.str();
    }

    std::string text_overlay_content_;

    void on_frame_(float elapsed_time, float delta_time)
    {
        const vk::DeviceSize vb_offset{0};

        auto &data = frame_data_vector_[frame_data_idx_];
        auto &back = acquired_back_buf_;

        base::assert_success(p_dev_->dev.waitForFences(1,
                                                       &data.submit_fence,
                                                       VK_TRUE,
                                                       UINT64_MAX));
        p_dev_->dev.resetFences(1, &data.submit_fence);

        update_uniforms_(data);
        if (fps_counter_.frame_count() == 0) {
            generate_text_(data, text_overlay_content_);
            p_text_overlay_->update_text(text_overlay_content_, 0.05, 0.1, 14, p_info_->width(), p_info_->height());
        }

        auto &cmd_buf = data.cmd_buffer;
        cmd_buf.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        // reset query pool
        cmd_buf.resetQueryPool(data.query_pool, 0, 2);

        cmd_buf.setViewport(0, 1, &p_swapchain_->onscreen_viewport);
        cmd_buf.setScissor(0, 1, &p_swapchain_->onscreen_scissor);

        auto &rp_begin = use_culling_ ? p_rp_culling_->rp_begin : p_rp_simple_->rp_begin;
        rp_begin.renderArea.extent = p_swapchain_->curr_extent();
        rp_begin.framebuffer = p_swapchain_->framebuffers[back.swapchain_image_idx];
        cmd_buf.beginRenderPass(&rp_begin, vk::SubpassContents::eInline);

        // write timestamp
        cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, data.query_pool, 0);

        cmd_buf.bindVertexBuffers(0, 1, &p_model_->p_vert_buffer->buf, &vb_offset);
        cmd_buf.bindIndexBuffer(p_model_->p_idx_buffer->buf, 0, vk::IndexType::eUint32);

        if (use_culling_) desc_set_culling_[0] = data.desc_set;
        cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   use_culling_ ? pipeline_layouts_.culling : pipeline_layouts_.simple,
                                   0, use_culling_ ? 2 : 1,
                                   use_culling_ ? desc_set_culling_ : &data.desc_set,
                                   1, &data.dynamic_offset);

        cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                             use_culling_ ? pipelines_.depth : pipelines_.simple);
        cmd_buf.drawIndexed(p_model_->indices, 1, 0, 0, 0);

        if (use_culling_) {
            cmd_buf.nextSubpass(vk::SubpassContents::eInline);
            cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                 pipelines_.culling);
            cmd_buf.drawIndexed(p_model_->indices, 1, 0, 0, 0);
        }

        // text

        cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   pipeline_layouts_.text,
                                   0, 1,
                                   &desc_set_font_tex_,
                                   0, nullptr);
        cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                             use_culling_ ? pipelines_.culling_text : pipelines_.simple_text);
        cmd_buf.bindVertexBuffers(0, 1, &p_text_overlay_->p_vert_buf->buf, &vb_offset);
        cmd_buf.bindIndexBuffer(p_text_overlay_->p_idx_buf->buf, 0, vk::IndexType::eUint32);
        cmd_buf.drawIndexed(p_text_overlay_->draw_index_count, 1, 0, 0, 0);

        // write timestamp
        cmd_buf.writeTimestamp(vk::PipelineStageFlagBits::eColorAttachmentOutput, data.query_pool, 1);

        cmd_buf.endRenderPass();
        cmd_buf.end();

        const vk::PipelineStageFlags wait_stages{vk::PipelineStageFlagBits::eColorAttachmentOutput};
        auto submit_info = vk::SubmitInfo(1, &back.swapchain_image_acquire_semaphore,
                                          &wait_stages,
                                          1, &cmd_buf,
                                          1, &back.onscreen_render_semaphore);

        base::assert_success(p_dev_->graphics_queue.submit(
            1,
            &submit_info,
            data.submit_fence));

        // get query results
        base::assert_success(vkGetQueryPoolResults(static_cast<VkDevice>(p_dev_->dev),
                                                   static_cast<VkQueryPool>(data.query_pool),
                                                   0, 2,
                                                   sizeof(uint32_t) * 2,
                                                   &data.query_data,
                                                   sizeof(uint32_t),
                                                   static_cast<VkQueryResultFlagBits>(vk::QueryResultFlagBits::eWait)));

        frame_data_idx_ = (frame_data_idx_ + 1) % frame_data_count_;
    }
};
