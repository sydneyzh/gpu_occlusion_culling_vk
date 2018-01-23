#pragma once
#include "Aabb.hpp"
#include "Physical_device.hpp"
#include "Device.hpp"
#include "Buffer.hpp"
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <vector>

namespace base
{
typedef enum Vertex_component
{
    VERT_COMP_VEC4,
    VERT_COMP_COLOR3,
    VERT_COMP_POSITION,
    VERT_COMP_NORMAL,
    VERT_COMP_TANGENT,
    VERT_COMP_BITANGENT,
    VERT_COMP_UV,
    VERT_COMP_FLOAT
} Vertex_component;

struct Vertex_layout
{
    std::vector<Vertex_component> comps;

    Vertex_layout() = default;

    explicit Vertex_layout(const std::vector<Vertex_component> &comps) :
        comps(comps)
    {}

    explicit Vertex_layout(Vertex_component comp) {
        comps.emplace_back(comp);
    }

    uint32_t get_stride()
    {
        uint32_t res = 0;
        for (auto &comp : comps) {
            switch (comp) {
                case VERT_COMP_VEC4:res += 4 * sizeof(float);
                    break;
                case VERT_COMP_POSITION:
                case VERT_COMP_NORMAL:
                case VERT_COMP_TANGENT:
                case VERT_COMP_BITANGENT:
                case VERT_COMP_COLOR3:res += 3 * sizeof(float);
                    break;
                case VERT_COMP_UV:res += 2 * sizeof(float);
                    break;
                case VERT_COMP_FLOAT:res += sizeof(float);
                    break;
                default:
                    break;
            }
        }
        return res;
    }
};

struct Mesh
{
    uint32_t material_idx;
    uint32_t idx_base;
    uint32_t idx_count;
    int32_t vert_offset;
    glm::vec4 min;
    glm::vec4 max;
};

class Geometries
{
public:
    Buffer *p_vert_buffer{nullptr};
    Buffer *p_idx_buffer{nullptr};
    vk::DeviceMemory vert_buffer_mem;
    vk::DeviceMemory idx_buffer_mem;

    Vertex_layout vertex_layout;
    uint32_t stride{0};
    uint32_t indices{0};

    vk::VertexInputBindingDescription vi_binding;
    std::vector<vk::VertexInputAttributeDescription> vi_attribs;
    uint32_t vi_bind_id{0};

    std::vector<Mesh> meshes{};

    Geometries(Physical_device *p_phy_dev,
               Device *p_dev,
               Vertex_layout vertex_layout) :
        p_phy_dev_(p_phy_dev),
        p_dev_(p_dev),
        vertex_layout(vertex_layout)
    {
        stride = vertex_layout.get_stride();
    }

    ~Geometries()
    {
        p_dev_->dev.freeMemory(vert_buffer_mem);
        p_dev_->dev.freeMemory(idx_buffer_mem);
        delete p_vert_buffer;
        delete p_idx_buffer;
    }

    void init(const aiScene *p_scene,
              vk::CommandBuffer cmd_buffer)
    {

        std::vector<float> vdata;
        std::vector<uint32_t> idata;

        uint32_t idx_base = 0;
        int32_t vert_offset = 0;

        for (uint32_t m = 0; m < p_scene->mNumMeshes; m++) {
            auto p_mesh = p_scene->mMeshes[m];

            glm::vec3 min{FLT_MAX};
            glm::vec3 max(FLT_MIN);

            bool has_color = p_mesh->HasVertexColors(0);
            bool has_normal = p_mesh->HasNormals();
            bool has_uv = p_mesh->HasTextureCoords(0);
            bool has_tangent = p_mesh->HasTangentsAndBitangents();

            for (uint32_t v = 0; v < p_mesh->mNumVertices; v++) {
                for (auto &comp : vertex_layout.comps) {
                    switch (comp) {
                        case VERT_COMP_POSITION:
                        {
                            float x = (p_mesh->mVertices[v].x);
                            float y = (p_mesh->mVertices[v].y);
                            float z = (p_mesh->mVertices[v].z);
                            vdata.push_back(x);
                            vdata.push_back(y);
                            vdata.push_back(z);
                            min.x = std::min(x, min.x);
                            min.y = std::min(y, min.y);
                            min.z = std::min(z, min.z);
                            max.x = std::max(x, max.x);
                            max.y = std::max(y, max.y);
                            max.z = std::max(z, max.z);
                        }
                        break;
                        case VERT_COMP_NORMAL:
                        {
                            assert(has_normal);
                            vdata.push_back(p_mesh->mNormals[v].x);
                            vdata.push_back(p_mesh->mNormals[v].y);
                            vdata.push_back(p_mesh->mNormals[v].z);
                        }
                        break;
                        case VERT_COMP_UV:
                        {
                            assert(has_uv);
                            aiVector3D pTexCoord = p_mesh->mTextureCoords[0][v];
                            vdata.push_back(pTexCoord.x);
                            vdata.push_back(pTexCoord.y);
                        }
                        break;
                        case VERT_COMP_COLOR3:
                            if (has_color) {
                                vdata.push_back(p_mesh->mColors[v]->r);
                                vdata.push_back(p_mesh->mColors[v]->g);
                                vdata.push_back(p_mesh->mColors[v]->b);
                            } else {
                                vdata.push_back(1.f);
                                vdata.push_back(1.f);
                                vdata.push_back(1.f);
                            }
                            break;
                        case VERT_COMP_TANGENT:
                        {
                            assert(has_tangent);
                            vdata.push_back(p_mesh->mTangents[v].x);
                            vdata.push_back(p_mesh->mTangents[v].y);
                            vdata.push_back(p_mesh->mTangents[v].z);
                        }
                        break;
                        case VERT_COMP_BITANGENT:
                        {
                            assert(has_tangent);
                            vdata.push_back(p_mesh->mBitangents[v].x);
                            vdata.push_back(p_mesh->mBitangents[v].y);
                            vdata.push_back(p_mesh->mBitangents[v].z);
                        }
                        break;
                        case VERT_COMP_VEC4:
                        {
                            // to be implemented in child class
                            vdata.push_back(0.f);
                            vdata.push_back(0.f);
                            vdata.push_back(0.f);
                            vdata.push_back(0.f);
                        }
                        break;
                        case VERT_COMP_FLOAT:
                        {
                            // to be implemented in child class
                            vdata.push_back(0.f);
                        }
                        break;
                        default:throw std::runtime_error("Invalid vertex component.");
                    } // switch component
                } // loop components
            } // loop vertices 

            for (uint32_t f = 0; f < p_mesh->mNumFaces; f++) {
                for (uint32_t j = 0; j < 3; j++) { // triangulated
                    idata.emplace_back(p_mesh->mFaces[f].mIndices[j]);
                }
            }
            uint32_t idx_count = 3 * p_mesh->mNumFaces;// triangulated
            meshes.push_back({p_mesh->mMaterialIndex,
                             idx_base,
                             idx_count,
                             vert_offset,
                             glm::vec4(min, 1.f),
                             glm::vec4(max, 1.f)});
            vert_offset += p_mesh->mNumVertices;
            idx_base += idx_count;
        } // loop meshes

        indices = static_cast<uint32_t>(idata.size());

        // attribute buffers

        const vk::DeviceSize vert_buf_size = vdata.size() * sizeof(vdata[0]);
        const vk::DeviceSize idx_buf_size = idata.size() * sizeof(idata[0]);

        // create device local buffers
        p_vert_buffer = new Buffer(p_dev_,
                                   vert_buf_size,
                                   vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                   vk::MemoryPropertyFlagBits::eDeviceLocal,
                                   vk::SharingMode::eExclusive);
        p_idx_buffer = new Buffer(p_dev_,
                                  idx_buf_size,
                                  vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                  vk::MemoryPropertyFlagBits::eDeviceLocal,
                                  vk::SharingMode::eExclusive);

        allocate_and_bind_buffer_memory(p_phy_dev_,
                                        p_dev_,
                                        vert_buffer_mem, 1, &p_vert_buffer);

        allocate_and_bind_buffer_memory(p_phy_dev_,
                                        p_dev_,
                                        idx_buffer_mem, 1, &p_idx_buffer);

        update_device_local_buffer_memory(
            p_phy_dev_,
            p_dev_,
            p_vert_buffer,
            vert_buffer_mem,
            vert_buf_size,
            vdata.data(), 0,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eVertexInput,
            vk::AccessFlags(), vk::AccessFlagBits::eVertexAttributeRead,
            cmd_buffer);

        update_device_local_buffer_memory(
            p_phy_dev_,
            p_dev_,
            p_idx_buffer,
            idx_buffer_mem,
            idx_buf_size,
            idata.data(), 0,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eVertexInput,
            vk::AccessFlags(), vk::AccessFlagBits::eIndexRead,
            cmd_buffer);

        // vi bindings
        vi_binding = vk::VertexInputBindingDescription(0, stride, vk::VertexInputRate::eVertex);

        // vi attribs
        uint32_t idx = 0;
        vk::DeviceSize offset = 0;
        for (auto &comp : vertex_layout.comps) {
            switch (comp) {
                case VERT_COMP_FLOAT:
                    vi_attribs.emplace_back(idx++,
                                            vi_bind_id,
                                            vk::Format::eR32Sfloat,
                                            offset);
                    offset += sizeof(float);
                    break;
                case VERT_COMP_UV:
                    vi_attribs.emplace_back(idx++,
                                            vi_bind_id,
                                            vk::Format::eR32G32Sfloat,
                                            offset);
                    offset += sizeof(float) * 2;
                    break;
                case VERT_COMP_POSITION:
                case VERT_COMP_NORMAL:
                case VERT_COMP_TANGENT:
                case VERT_COMP_BITANGENT:
                case VERT_COMP_COLOR3:
                    vi_attribs.emplace_back(idx++,
                                            vi_bind_id,
                                            vk::Format::eR32G32B32Sfloat,
                                            offset);
                    offset += sizeof(float) * 3;
                    break;
                case VERT_COMP_VEC4:
                    vi_attribs.emplace_back(idx++,
                                            vi_bind_id,
                                            vk::Format::eR32G32B32A32Sfloat,
                                            offset);
                    offset += sizeof(float) * 4;
                    break;
            }
        }
    }

private:
    Physical_device * p_phy_dev_;
    Device *p_dev_;
};
} // namespace base
