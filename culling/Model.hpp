#pragma once
#include  "stdafx.h"
#include <glm/gtc/type_ptr.hpp>
#define MSG_PREFIX "-- MODEL: "
#define DUMMY_TEX_PATH "dummy/dummy_rgba_unorm.ktx" 
#define DUMMY_NORMAL_TEX_PATH "dummy/dummy_normal_rgba_unorm.ktx" 

const std::string empty_str = std::string();

class Material_texture2D : public base::Texture2D
{
public:
    Material_texture2D(base::Physical_device *p_phy_dev,
                       base::Device *p_dev,
                       const std::string file_path,
                       vk::CommandPool &graphics_cmd_pool,
                       vk::Format format) :
        base::Texture2D(p_phy_dev, p_dev)
    {
        load(file_path,
             graphics_cmd_pool,
             format,
             vk::ImageUsageFlagBits::eSampled,
             vk::ImageLayout::eShaderReadOnlyOptimal,
             true,
             vk::SamplerCreateInfo({},
                                   vk::Filter::eLinear,
                                   vk::Filter::eLinear,
                                   vk::SamplerMipmapMode::eLinear,
                                   vk::SamplerAddressMode::eRepeat,
                                   vk::SamplerAddressMode::eRepeat,
                                   vk::SamplerAddressMode::eRepeat,
                                   0.f, 0, 1.f, 0, vk::CompareOp::eNever, 0.f, 1.f,
                                   vk::BorderColor::eFloatOpaqueWhite, 0));
    }
};

struct Instance
{
    glm::mat4 transform;
    uint32_t mesh_idx;
};

struct Instance_attributes 
{
    glm::mat4 transform;
    float material_idx;
};

struct Instance_properties
{
    glm::mat4 transform;
    glm::vec3 min;
    float padding;
    glm::vec3 max;
    float material_idx;
};

struct Mdi_cmd
{
    uint32_t idx_count;
    uint32_t inst_count;
    uint32_t idx_base;
    int vert_offset;
    uint32_t inst_start;
    float paddings[7];
    Mdi_cmd(uint32_t idx_c, uint32_t inst_c, uint32_t idx_b, int vert_o, uint32_t inst_s) :
        idx_count(idx_c),
        inst_count(inst_c),
        idx_base(idx_b),
        vert_offset(vert_o),
        inst_start(inst_s)
    {}
};

struct Cmd_draw_info
{
    vk::Buffer indirect_cmd_buffer{};
    vk::DeviceSize offset{0};
    uint32_t draw_count{0};
    uint32_t stride{0};
};

struct Material_properties
{
    glm::vec4 tex_indices{-1.f};

    glm::vec3 diffuse{1.f};
    float alpha{1.f};

    glm::vec3 specular{0.f};
    float specular_exponent{0.f};

    glm::vec3 emissive{0.f};
    float padding{0.f};
};

class Model : public base::Model_base
{
public:
    base::Buffer *p_inst_attribs_buffer{nullptr};
    base::Buffer *p_inst_data_buffer{nullptr};
    base::Buffer *p_mdi_cmd_buffer{nullptr};
    base::Buffer *p_mdi_no_batching_cmd_buffer{nullptr};

    Cmd_draw_info mdi_cmd_draw_info{};
    Cmd_draw_info mdi_no_batching_cmd_draw_info{};

    uint32_t inst_vi_bind_id{1};
    std::vector<vk::VertexInputBindingDescription> vi_bindings{};
    std::vector<vk::VertexInputAttributeDescription> vi_attribs{};

    vk::DeviceSize mtl_buffer_aligned_size{0};

    vk::DescriptorSet desc_set{};
    vk::DescriptorSetLayout desc_set_layout{};

    Model(base::Physical_device *p_phy_dev,
          base::Device *p_dev,
          vk::CommandPool graphics_cmd_pool,
          bool has_diffuse_map = false,
          bool has_opacity_map = false,
          bool has_specular_map = false,
          bool has_normal_map = false) :
        base::Model_base(p_phy_dev,
                         p_dev,
                         graphics_cmd_pool),
        has_diffuse_map_(has_diffuse_map),
        has_opacity_map_(has_opacity_map),
        has_specular_map_(has_specular_map),
        has_normal_map_(has_normal_map)
    {}

    ~Model() override
    {
        p_dev_->dev.destroyDescriptorPool(desc_pool_);
        p_dev_->dev.destroyDescriptorSetLayout(desc_set_layout);
        p_dev_->dev.freeMemory(inst_data_buffer_mem_);
        p_dev_->dev.freeMemory(inst_attribs_buffer_mem_);
        p_dev_->dev.freeMemory(mtl_buffer_mem_);
        p_dev_->dev.freeMemory(mdi_cmd_buffer_mem_);
        delete p_inst_attribs_buffer;
        delete p_inst_data_buffer;
        delete p_mtl_buffer_;
        delete p_mdi_cmd_buffer;
        delete p_mdi_no_batching_cmd_buffer;
        for (auto p_tex : p_mtl_textures_) {
            delete p_tex;
        }
    }

    void load(const std::string &model_path,
              const base::Vertex_layout &layout,
              const int ai_flags = 0,
              const std::string &tex_dir = empty_str)
    {
        if (has_diffuse_map_ | has_opacity_map_ | has_specular_map_ | has_normal_map_) {
            tex_dir_ = tex_dir;
        }

        // load meshes and run post processing
        base::Model_base::load(model_path, layout, ai_flags);
    }

private:
    vk::DescriptorPool desc_pool_{};

    vk::DeviceMemory inst_data_buffer_mem_{};
    vk::DeviceMemory inst_attribs_buffer_mem_{};
    vk::DeviceMemory mdi_cmd_buffer_mem_{};
    vk::DeviceMemory mtl_buffer_mem_{};
    base::Buffer *p_mtl_buffer_{nullptr};

    std::string tex_dir_{""};
    std::vector<Material_texture2D *> p_mtl_textures_{};

    bool has_diffuse_map_;
    bool has_opacity_map_;
    bool has_specular_map_;
    bool has_normal_map_;

    void post_process_(const aiScene *p_scene,
                       vk::CommandBuffer cmd_buffer)
        override
    {
        init_indirect_draw_(p_scene, cmd_buffer);
        init_materials_(p_scene, cmd_buffer);
    }

    void traverse_instances_(aiNode *p_node,
                             glm::mat4 transform,
                             std::vector<Instance> &instances)
    {
        transform *= base::convert_mat(p_node->mTransformation);
        if (p_node->mNumChildren > 0) {
            for (uint32_t i = 0; i < p_node->mNumChildren; i++) {
                traverse_instances_(p_node->mChildren[i], transform, instances);
            }
        } else if (p_node->mNumMeshes > 0) {

            // only process single mesh leaf nodes
            assert(p_node->mNumMeshes == 1);
            instances.push_back({transform, p_node->mMeshes[0]});
        }
    }

    void init_indirect_draw_(const aiScene *p_scene,
                             vk::CommandBuffer cmd_buffer)
    {
        std::vector<Instance> instances;
        traverse_instances_(p_scene->mRootNode, glm::mat4(1.f), instances);

        std::vector<Instance_attributes> inst_attribs;
        std::vector<Instance_properties> inst_data;
        std::vector<vk::DrawIndexedIndirectCommand> mdi_cmds;
        std::vector<Mdi_cmd> mdi_no_batching_cmds;
        std::vector<base::Mesh> &meshes = p_geometries->meshes;
        uint32_t inst_idx = 0;
        for (auto &inst : instances) {
            auto p_mesh = &meshes[inst.mesh_idx];

            // can pack inst_attribs and inst_data into one,
            // separated just for clarity purpose

            // inst attribs for vertex shader
            inst_attribs.push_back({inst.transform,
                                   static_cast<float>(p_mesh->material_idx)
                                   });
            // inst data for compute shader
            inst_data.push_back({inst.transform,
                                p_mesh->min,
                                0.f,
                                p_mesh->max,
                                static_cast<float>(p_mesh->material_idx)});

            // mdi cmd
            // draw all instances of the same mesh per cmd 
            if (mdi_cmds.size() <= inst.mesh_idx) {
                mdi_cmds.emplace_back(p_mesh->idx_count,
                                      1,
                                      p_mesh->idx_base,
                                      p_mesh->vert_offset,
                                      inst_idx);
            } else {
                mdi_cmds[inst.mesh_idx].instanceCount++;
            }

            // mdi no batching cmd
            // draw one instance per cmd
            mdi_no_batching_cmds.emplace_back(p_mesh->idx_count,
                                              1,
                                              p_mesh->idx_base,
                                              p_mesh->vert_offset,
                                              inst_idx);
            inst_idx++;
        }

        // device local buffers

        // inst data buffer
        {
            const vk::DeviceSize inst_buf_size = inst_data.size() * sizeof(inst_data[0]);
            p_inst_data_buffer = new base::Buffer(p_dev_,
                                                  inst_buf_size,
                                                  vk::BufferUsageFlagBits::eTransferDst |
                                                  vk::BufferUsageFlagBits::eStorageBuffer,
                                                  vk::MemoryPropertyFlagBits::eDeviceLocal,
                                                  vk::SharingMode::eExclusive);
            p_inst_data_buffer->update_descriptor();

            base::allocate_and_bind_buffer_memory(p_phy_dev_,
                                                  p_dev_,
                                                  inst_data_buffer_mem_,
                                                  1,
                                                  &p_inst_data_buffer);

            base::update_device_local_buffer_memory(p_phy_dev_,
                                                    p_dev_,
                                                    p_inst_data_buffer,
                                                    inst_data_buffer_mem_,
                                                    inst_buf_size,
                                                    inst_data.data(),
                                                    0,
                                                    vk::PipelineStageFlagBits::eTopOfPipe,
                                                    vk::PipelineStageFlagBits::eComputeShader,
                                                    vk::AccessFlags(),
                                                    vk::AccessFlagBits::eShaderRead,
                                                    cmd_buffer);
        }

        // inst attribs buffer
        {
            const vk::DeviceSize inst_buf_size = inst_attribs.size() * sizeof(inst_attribs[0]);
            p_inst_attribs_buffer = new base::Buffer(p_dev_,
                                                  inst_buf_size,
                                                  vk::BufferUsageFlagBits::eVertexBuffer |
                                                  vk::BufferUsageFlagBits::eTransferDst,
                                                  vk::MemoryPropertyFlagBits::eDeviceLocal,
                                                  vk::SharingMode::eExclusive);
            p_inst_attribs_buffer->update_descriptor();

            base::allocate_and_bind_buffer_memory(p_phy_dev_,
                                                  p_dev_,
                                                  inst_attribs_buffer_mem_,
                                                  1,
                                                  &p_inst_attribs_buffer);

            base::update_device_local_buffer_memory(p_phy_dev_,
                                                    p_dev_,
                                                    p_inst_attribs_buffer,
                                                    inst_attribs_buffer_mem_,
                                                    inst_buf_size,
                                                    inst_attribs.data(),
                                                    0,
                                                    vk::PipelineStageFlagBits::eTopOfPipe,
                                                    vk::PipelineStageFlagBits::eVertexInput,
                                                    vk::AccessFlags(),
                                                    vk::AccessFlagBits::eVertexAttributeRead,
                                                    cmd_buffer);
        }

        // mdi cmd buffers
        {
            const vk::DeviceSize mdi_cmd_buf_size = mdi_cmds.size() * sizeof(mdi_cmds[0]);
            p_mdi_cmd_buffer = new base::Buffer(p_dev_,
                                                mdi_cmd_buf_size,
                                                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                                vk::MemoryPropertyFlagBits::eDeviceLocal,
                                                vk::SharingMode::eExclusive);

            const vk::DeviceSize mdi_no_batching_cmd_buf_size = mdi_no_batching_cmds.size() * sizeof(mdi_no_batching_cmds[0]);
            p_mdi_no_batching_cmd_buffer = new base::Buffer(p_dev_,
                                                            mdi_no_batching_cmd_buf_size,
                                                            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                                            vk::MemoryPropertyFlagBits::eDeviceLocal,
                                                            vk::SharingMode::eExclusive);

            base::Buffer *cmd_buffers[2] = {p_mdi_cmd_buffer, p_mdi_no_batching_cmd_buffer};
            base::allocate_and_bind_buffer_memory(p_phy_dev_,
                                                  p_dev_,
                                                  mdi_cmd_buffer_mem_,
                                                  2,
                                                  cmd_buffers);

            base::update_device_local_buffer_memory(p_phy_dev_,
                                                    p_dev_,
                                                    p_mdi_cmd_buffer,
                                                    mdi_cmd_buffer_mem_,
                                                    mdi_cmd_buf_size,
                                                    mdi_cmds.data(),
                                                    0,
                                                    vk::PipelineStageFlagBits::eTopOfPipe,
                                                    vk::PipelineStageFlagBits::eDrawIndirect,
                                                    vk::AccessFlags(),
                                                    vk::AccessFlagBits::eIndirectCommandRead,
                                                    cmd_buffer);
            base::update_device_local_buffer_memory(p_phy_dev_,
                                                    p_dev_,
                                                    p_mdi_no_batching_cmd_buffer,
                                                    mdi_cmd_buffer_mem_,
                                                    mdi_no_batching_cmd_buf_size,
                                                    mdi_no_batching_cmds.data(),
                                                    0,
                                                    vk::PipelineStageFlagBits::eTopOfPipe,
                                                    vk::PipelineStageFlagBits::eDrawIndirect,
                                                    vk::AccessFlags(),
                                                    vk::AccessFlagBits::eIndirectCommandRead,
                                                    cmd_buffer);
            p_mdi_no_batching_cmd_buffer->update_descriptor(
                0,
                p_mdi_no_batching_cmd_buffer->size
            );
        }

        // mdi cmd draw info
        {
            mdi_cmd_draw_info = {
                p_mdi_cmd_buffer->buf,
                0,
                mdi_cmds.size(),
                sizeof(mdi_cmds[0])
            };

            mdi_no_batching_cmd_draw_info = {
                p_mdi_no_batching_cmd_buffer->buf,
                0,
                mdi_no_batching_cmds.size(),
                sizeof(mdi_no_batching_cmds[0])
            };
        }

        // vertex input
        {
            std::vector<base::Vertex_component> comps = {
                // transform
                base::Vertex_component::VERT_COMP_VEC4,
                base::Vertex_component::VERT_COMP_VEC4,
                base::Vertex_component::VERT_COMP_VEC4,
                base::Vertex_component::VERT_COMP_VEC4,
                // mtl idx
                base::Vertex_component::VERT_COMP_FLOAT
            };
            base::Vertex_layout layout(comps);
            vi_bindings.push_back(p_geometries->vi_binding);
            vi_bindings.emplace_back(inst_vi_bind_id,
                                     layout.get_stride(),
                                     vk::VertexInputRate::eInstance);
            vi_attribs = p_geometries->vi_attribs;
            uint32_t offset = 0;
            for (uint32_t i = 0; i < 4; i++) {
                vi_attribs.emplace_back(vi_attribs.size(),
                                        inst_vi_bind_id,
                                        vk::Format::eR32G32B32A32Sfloat,
                                        offset);
                offset += sizeof(float) * 4;
            }
            vi_attribs.emplace_back(vi_attribs.size(),
                                    inst_vi_bind_id,
                                    vk::Format::eR32Sfloat,
                                    offset);
        }
    }

    void init_materials_(const aiScene *p_scene,
                         vk::CommandBuffer cmd_buffer)
    {
        vk::Format tex_format{vk::Format::eR8G8B8A8Unorm};
        vk::Format dummy_tex_format{vk::Format::eR8G8B8A8Unorm};

        // check compressed tex format support
        std::string tex_filename_suffix;
        bool has_compression = false;
        if (p_phy_dev_->req_features.textureCompressionBC) {
            tex_format = vk::Format::eBc3UnormBlock;
            tex_filename_suffix = "_bc3_unorm";
            std::cout << MSG_PREFIX << "using BC3 texture compression" << std::endl;
            has_compression = true;
        } else if (p_phy_dev_->req_features.textureCompressionETC2) {
            tex_format = vk::Format::eEtc2R8G8B8UnormBlock;
            tex_filename_suffix = "_etc2_unorm";
            std::cout << MSG_PREFIX << "using ETC2 texture compression" << std::endl;
            has_compression = true;
        } else {
            std::cout << MSG_PREFIX << "no texture compression" << std::endl;
        }

        std::vector<Material_properties> mtls;
        std::vector<std::string> textures;
        std::vector<vk::DescriptorImageInfo> image_info;

        for (size_t i = 0; i < p_scene->mNumMaterials; i++) {
            auto p_m = p_scene->mMaterials[i];
            Material_properties mtl;

            aiColor4D color;
            p_m->Get(AI_MATKEY_COLOR_DIFFUSE, color);
            mtl.diffuse = {color.r, color.g, color.b};

            p_m->Get(AI_MATKEY_COLOR_SPECULAR, color); 
            mtl.specular = {color.r, color.g, color.b};

            p_m->Get(AI_MATKEY_COLOR_EMISSIVE, color); 
            mtl.emissive = {color.r, color.g, color.b};

            p_m->Get(AI_MATKEY_OPACITY, mtl.alpha);
            p_m->Get(AI_MATKEY_SHININESS, mtl.specular_exponent);

            if (has_diffuse_map_) {
                setup_material_texture_(&mtl.tex_indices[0],
                                        textures,
                                        p_m,
                                        aiTextureType_DIFFUSE,
                                        has_compression,
                                        tex_filename_suffix,
                                        tex_format,
                                        DUMMY_TEX_PATH,
                                        dummy_tex_format,
                                        image_info);
            }
            if (has_opacity_map_) {
                setup_material_texture_(&mtl.tex_indices[1],
                                        textures,
                                        p_m,
                                        aiTextureType_OPACITY,
                                        has_compression,
                                        tex_filename_suffix,
                                        tex_format,
                                        DUMMY_TEX_PATH,
                                        dummy_tex_format,
                                        image_info);
            }
            if (has_specular_map_) {
                setup_material_texture_(&mtl.tex_indices[2],
                                        textures,
                                        p_m,
                                        aiTextureType_SPECULAR,
                                        has_compression,
                                        tex_filename_suffix,
                                        tex_format,
                                        DUMMY_TEX_PATH,
                                        dummy_tex_format,
                                        image_info);
            }
            if (has_normal_map_) {
                setup_material_texture_(&mtl.tex_indices[3],
                                        textures,
                                        p_m,
                                        aiTextureType_NORMALS,
                                        has_compression,
                                        tex_filename_suffix,
                                        tex_format,
                                        DUMMY_NORMAL_TEX_PATH,
                                        dummy_tex_format,
                                        image_info);
            }
            mtls.push_back(mtl);
        }
        uint32_t num_textures = textures.size();
        std::cout << MSG_PREFIX << "num materials " << mtls.size() << std::endl;
        std::cout << MSG_PREFIX << "num textures " << num_textures << std::endl;
        assert(num_textures <= p_phy_dev_->props.limits.maxImageArrayLayers);

        // mtl buffer  

        mtl_buffer_aligned_size = sizeof(Material_properties);
        const VkDeviceSize &alignment = p_phy_dev_->props.limits.minStorageBufferOffsetAlignment;
        base::align_size(mtl_buffer_aligned_size, alignment);

        vk::DeviceSize buffer_size = p_scene->mNumMaterials * mtl_buffer_aligned_size;
        void *p_mtl_data = malloc(buffer_size);

        int ptr_offset = 0;
        uint8_t *ptr = reinterpret_cast<uint8_t *>(p_mtl_data);
        size_t size = sizeof(Material_properties) / sizeof(uint8_t);
        size_t offset_size = mtl_buffer_aligned_size / sizeof(uint8_t);
        for (auto mtl : mtls) {
            memcpy(ptr + ptr_offset, reinterpret_cast<uint8_t *>(&mtl), size);
            ptr_offset += offset_size;
        }

        p_mtl_buffer_ = new base::Buffer(p_dev_,
                                         buffer_size,
                                         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                                         vk::SharingMode::eExclusive);
        allocate_and_bind_buffer_memory(p_phy_dev_,
                                        p_dev_,
                                        mtl_buffer_mem_,
                                        1,
                                        &p_mtl_buffer_);
        update_device_local_buffer_memory(p_phy_dev_,
                                          p_dev_,
                                          p_mtl_buffer_,
                                          mtl_buffer_mem_,
                                          buffer_size,
                                          p_mtl_data,
                                          0,
                                          vk::PipelineStageFlagBits::eTopOfPipe,
                                          vk::PipelineStageFlagBits::eFragmentShader,
                                          {}, vk::AccessFlagBits::eShaderRead,
                                          cmd_buffer);
        vk::DescriptorBufferInfo buffer_info{p_mtl_buffer_->buf, 0, VK_WHOLE_SIZE};
        free(p_mtl_data);

        // desc set

        std::vector<vk::DescriptorPoolSize> pool_sizes;
        pool_sizes.emplace_back(vk::DescriptorType::eStorageBuffer, 1);
        if (num_textures > 0)
            pool_sizes.emplace_back(vk::DescriptorType::eCombinedImageSampler, num_textures);

        desc_pool_ = p_dev_->dev.createDescriptorPool(
            vk::DescriptorPoolCreateInfo({},
                                         1,
                                         static_cast<uint32_t>(pool_sizes.size()),
                                         pool_sizes.data()));

        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        bindings.emplace_back(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment);
        if (num_textures > 0) {
            bindings.emplace_back(1, vk::DescriptorType::eCombinedImageSampler, num_textures, vk::ShaderStageFlagBits::eFragment);
        }
        desc_set_layout = p_dev_->dev.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({},
                                                                                                  bindings.size(),
                                                                                                  bindings.data()));
        std::vector<vk::DescriptorSet> desc_sets = p_dev_->dev.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(desc_pool_,
                                                                                                                    1,
                                                                                                                    &desc_set_layout));
        desc_set = desc_sets[0];

        std::vector<vk::WriteDescriptorSet> writes;
        writes.emplace_back(desc_set,
                            0,
                            0,
                            1,
                            vk::DescriptorType::eStorageBuffer,
                            nullptr,
                            &buffer_info,
                            nullptr);
        if (num_textures > 0) {
            writes.emplace_back(desc_set, // dst set
                                1, // dst binding
                                0, // dst array element
                                num_textures, // descriptor count
                                vk::DescriptorType::eCombinedImageSampler, // type
                                image_info.data(), // image info
                                nullptr, // buffer info
                                nullptr); // texel buffer view
        }
        p_dev_->dev.updateDescriptorSets(
            static_cast<uint32_t>(writes.size()),
            writes.data(),
            0, nullptr);
    }

    void setup_material_texture_(float *p_tex_idx,
                                 std::vector<std::string> &textures,
                                 aiMaterial *p_m,
                                 aiTextureType ai_tex_type,
                                 bool has_compression,
                                 const std::string &tex_format_suffix,
                                 vk::Format tex_format,
                                 const std::string &dummy_tex_path,
                                 vk::Format dummy_tex_format,
                                 std::vector<vk::DescriptorImageInfo> &image_info)
    {
        aiString ai_tex_filename;
        p_m->GetTexture(ai_tex_type, 0, &ai_tex_filename);
        std::string filename = std::string{ai_tex_filename.C_Str()};
        if (p_m->GetTextureCount(ai_tex_type) > 0 && filename.size() > 0) {
            assert(base::ends_with(filename, ".ktx"));
            auto it = std::find(textures.begin(), textures.end(), filename);
            if (it != textures.end()) {
                *p_tex_idx = static_cast<float>(it - textures.begin());
            } else {
                *p_tex_idx = textures.size();
                textures.push_back(filename);

                // remove relative path
                auto relative_path = filename.find_last_of("\\") + 1;
                if (relative_path < filename.length())
                    filename = filename.substr(relative_path, filename.length() - relative_path);

                if (has_compression)
                    filename.insert(filename.find(".ktx"), tex_format_suffix);

                assert(tex_dir_ != empty_str);
                auto full_path = tex_dir_ + filename;
                assert(base::file_exists(full_path));

                p_mtl_textures_.push_back(new Material_texture2D(p_phy_dev_, p_dev_,
                                                                 full_path,
                                                                 graphics_cmd_pool_,
                                                                 tex_format));
                auto p_tex = p_mtl_textures_.back();
                image_info.emplace_back(p_tex->sampler,
                                        p_tex->view,
                                        p_tex->layout);
            }
        } else {
            auto it = std::find(textures.begin(), textures.end(), dummy_tex_path);
            if (it != textures.end()) {
                *p_tex_idx = static_cast<float>(it - textures.begin());
            } else {
                *p_tex_idx = textures.size();
                textures.push_back(dummy_tex_path);

                auto full_path = base::data_dir() + dummy_tex_path;
                assert(base::file_exists(full_path));

                p_mtl_textures_.push_back(new Material_texture2D(p_phy_dev_, p_dev_,
                                                                 full_path,
                                                                 graphics_cmd_pool_,
                                                                 dummy_tex_format));
                auto p_tex = p_mtl_textures_.back();
                image_info.emplace_back(p_tex->sampler,
                                        p_tex->view,
                                        p_tex->layout);
            }
        }
    }
};
#undef MSG_PREFIX
