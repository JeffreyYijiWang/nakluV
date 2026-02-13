#pragma once

#include "VK.hpp"
#include "glm.hpp"
#include <string>
#include <vector>
#include <optional>
#include <map>

/**
 * Loads from .s72 format and manages a hierarchy of transformations.
 */

struct Scene {

    struct Transform {
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::quat rotation = glm::quat(0.0f, 0.0f, 0.0f, 1.0f); // x, y, z, w
        glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);

        glm::mat4x4 parent_from_local() const;  // graphics 362
        glm::mat4x4 local_from_parent() const;
    };


    struct Texture {
        std::string source = "";
        glm::vec3 value;
        bool is_2D = true;
        bool has_src = false;
        /*enum Vkformat  optional format
        {
            Linear = 0,
            SRGB= 1,
            RGBE = 2,
        } format = Linear;*/

        Texture(std::string source_) : source(source_), is_2D(true), has_src(true) {};
        Texture(glm::vec3 value_) : value(value_), is_2D(true), has_src(false) {};
        Texture() : value({ 1,1,1 }), is_2D(true), has_src(false) {};
    };

    struct Material {
        std::string name;
        uint32_t texture_index;
    };

    struct Mesh {
        std::string name;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        uint32_t count = 0;
        struct Attribute {
            std::string source = "";
            uint32_t offset;
            uint32_t stride;
            VkFormat format;
        };
        Attribute attributes[4]; // Position, Normal, Tangent, TexCoord


        int32_t material_index = -1;
    };

    struct Camera {
        std::string name;
        float aspect; // image aspect ratio (width / height).
        float vfov; // vertical field of view in radians.
        float near;
        float far = -1.0f; // If far <= 0, use infinite projection
    };

    struct Light {
        std::string name;
        glm::vec3 tint = glm::vec3(1.0f);
        float shadow = 0.0f;
        float angle = 0.0f;
        float strength = 1.0f;
    };

    struct Node {
        std::string name;
        Transform transform;
        std::vector<uint32_t> children; // list of children, OPTOMIZE?
        int32_t cameras_index = -1;
        int32_t mesh_index = -1;
        // int32_t environment = -1; 
        int32_t light_index = -1;
    };


    struct Driver
    {
        std::string name;
        uint32_t node_index;
        enum Channel
        {
            Translation = 0,
            Scale = 1,
            Rotation = 2,
        } channel;
        std::vector<float> times;
        std::vector<float> values;
        enum InterpolationMode
        {
            STEP,
            LINEAR,
            SLERP,
        } interpolation = LINEAR;
        uint32_t cur_time_index = 0;
        float cur_time = 0.0f;
    };

    // Data Storage
    std::vector<Node> nodes;
    std::vector<Camera> cameras;
    int32_t requested_camera_index = -1;
    std::vector<Light> lights;
    std::vector<Mesh> meshes;
    uint32_t vertices_count = 0;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<uint32_t> root_nodes;
    std::string scene_path;
    std::vector<Driver> drivers;
    uint8_t animation_setting;

    // Functions
    Scene(std::string const& filename);
    void load(std::string const& filename);
    void debug();

};