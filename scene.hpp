#pragma once

#include "VK.hpp"
#include "glm.hpp"
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <>
/**
 * Loads from .s72 format and manages a hierarchy of transformations.
 */

struct Scene {

    struct Transform {
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::quat rotation = glm::quat(0.0f, 0.0f, 0.0f, 1.0f); // x, y, z, w
        glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);

        glm::mat4x4 local_to_parent() const;  // graphics 362
        glm::mat4x4 parent_to_local() const;
    };


    struct Texture {
        std::variant<float, glm::vec3, std::string> value;
        bool is_2D = true; //if false, environment cube
        bool has_src = false;
        enum Vkformat
        {
            Linear = 0,
            SRGB= 1,
            RGBE = 2,
        } format = VKformat::Linear;

        Texture(std::string value_, bool single_channel_) : value(value_), is_2D(true), has_src(true), single_channel(single_channel_) {};
        Texture(float value_) : value(value_), is_2D(true), has_src(false), single_channel(true) {};
        Texture(std::string value_) : value(value_), is_2D(true), has_src(true), single_channel(false) {};
        Texture(glm::vec3 value_) : value(value_), is_2D(true), has_src(false), single_channel(false) {};
        Texture() : value(glm::vec3{ 1,1,1 }), is_2D(true), has_src(false), single_channel(false) {};

        enum struct DefaultTexture : uint8_t {
            DefaultAlbedo = 0,
            DefaultRoughness = 1,
            DefaultMetalness = 2,
            DefaultNormal = 3,
            DefaultDisplacement = 4,
        };
    };

    struct Material {

        enum MaterialType : uint8_t {
            Lambertian,
            PBR,
            Mirror,
            Environment
        } material_type;


        std::string name;
        uint32_t normal_index = 3;
        uint32_t displacement_index = 4;
        struct MatLambertian {
            uint32_t albedo_index = 0;
        };

        struct MatPBR {
            uint32_t albedo_index = 0;
            uint32_t roughness_index = 1;
            uint32_t metalness_index = 2;
        };

        std::variant<std::monostate, MatLambertian, MatPBR> material_textures;
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


        uint32_t material_index = 0; // default material at index 0
    };

    struct Camera {
        std::string name;
        float aspect; // image aspect ratio (width / height).
        float vfov; // vertical field of view in radians.
        float near;
        float far = -1.0f; // If far <= 0, use infinite projection
        std::vector<uint32_t> local_to_world; // index 0 is root node - list of node indices to get from local to world 
    };

    struct Light {
        std::string name;
        glm::vec3 tint = glm::vec3(1.0f);
        float shadow = 0.0f;
        float angle = 0.0f;
        float strength = 1.0f;

        std::vector<uint32_t> local_to_world; // index 0 is root node - list of node indices to get from local to world 
    };

    struct Environment {
        std::string name;
        std::string source = "";
    };
    struct Node {
        std::string name;
        Transform transform;
        std::vector<uint32_t> children; // list of children, OPTOMIZE?
        int32_t cameras_index = -1;
        int32_t mesh_index = -1;
        bool environment = false;
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
    std::vector<Mesh> meshes;
    uint32_t vertices_count = 0;
    std::vector<Material> materials;
    uint32_t MatPBR_count;
    uint32_t MatLambertian_count;
    uint32_t MatEnvMirror_count; // both environment and mirror just need normal and displacement
    std::vector<Texture> textures;
    std::vector<uint32_t> root_nodes;
    std::vector<Light> lights;
    std::string scene_path;
    std::vector<Driver> drivers;
    uint8_t animation_setting;
    float return_time = 0.0f;
    Environment environment = Environment();

    // Functions
    Scene(std::string filename, std::optional<std::string> camera, uint8_t animation_setting);
    void load(std::string filename, std::optional<std::string> camera);
    void debug();
    void update_drivers(float dt);
    void set_driver_time(float t);
};