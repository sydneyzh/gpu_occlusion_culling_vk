#pragma once
#include "tools.hpp"
#include "Geometries.hpp"
#include <assimp/postprocess.h>
#define MSG_PREFIX "-- MODEL: "

namespace base
{
class Model_base
{
public:
    // root
    glm::mat4 model_matrix{1.f};
    glm::mat4 normal_matrix{1.f};

    Geometries *p_geometries{nullptr}; 

    Model_base(Physical_device *p_phy_dev,
          Device *p_dev,
          vk::CommandPool graphics_cmd_pool) :
        p_phy_dev_(p_phy_dev),
        p_dev_(p_dev),
        graphics_cmd_pool_(graphics_cmd_pool)
    {}

    virtual ~Model_base()
    {
        delete p_geometries; 
    }

    void load(const std::string &model_path,
              const Vertex_layout &layout,
              const int ai_flags = 0 )
    {
        assert(file_exists(model_path));
        model_path_ = model_path;
        std::cout << MSG_PREFIX << "loading file " << model_path << std::endl;

        Assimp::Importer importer;
        const aiScene *p_scene = importer.ReadFile(model_path_.c_str(),
                                                   aiProcess_Triangulate | aiProcess_RemoveRedundantMaterials | ai_flags);
        assert(p_scene);

        std::vector<vk::CommandBuffer> cmd_buffers = p_dev_->dev.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo(
                graphics_cmd_pool_,
                vk::CommandBufferLevel::ePrimary,
                1));

        p_geometries = new Geometries(p_phy_dev_, p_dev_, layout);
        p_geometries->init(p_scene, cmd_buffers[0]);
        post_process_(p_scene, cmd_buffers[0]);

        p_dev_->dev.freeCommandBuffers(graphics_cmd_pool_, cmd_buffers);
        cmd_buffers.clear();
        // scene is freed when the Importer is destroyed
    }

protected:
    base::Physical_device *p_phy_dev_;
    base::Device *p_dev_;
    vk::CommandPool graphics_cmd_pool_;
    std::string model_path_;

    virtual void post_process_(const aiScene *p_scene,
                                 vk::CommandBuffer cmd_buffer)
    {};
};
} // namespace base
#undef MSG_PREFIX