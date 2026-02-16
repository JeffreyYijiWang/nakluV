#pragma once

#include "PosColVertex.hpp"
#include "PosNorTexVertex.hpp"
#include "PosNorTanTexVertex.hpp"
#include "mat4.hpp"
#include "RTG.hpp"
#include "scene.hpp"
#include "glm.hpp"

struct Render : RTG::Application {

	Render(RTG &, Scene &);
	Render(Render const &) = delete; //you shouldn't be copying this object
	~Render();

	//kept for use in destructor:
	RTG &rtg;

	//scene reference information
	Scene& scene;

	//--------------------------------------------------------------------
	//Resources that last the lifetime of the application:

	//chosen format for depth buffer:
	VkFormat depth_format{};
	//Render passes describe how pipelines write to images:
	VkRenderPass render_pass = VK_NULL_HANDLE;

	//Pipelines:
	struct BackgroundPipeline {
		//no descriptor set layout

		struct Push {
			float time;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		//no vertex bindings

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} background_pipeline;

	struct LinesPipeline {
		
		//discriptor set layout 
		VkDescriptorSetLayout set0_Camera = VK_NULL_HANDLE;

		//types for descriptors:
		struct Camera {
			mat4 CLIP_FROM_WORLD;
		};
		static_assert(sizeof(Camera) == 16 * 4, "camera buffer structure is packed");


		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosColVertex;

		VkPipeline handle = VK_NULL_HANDLE;
		
		void create(RTG&, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG&);
	} lines_pipeline;


	struct ObjectsPipeline {

		// descriptor set layout 
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_TEXTURE = VK_NULL_HANDLE; 
		
		// types for descriptors:
		struct World {
			struct { float x, y, z, padding_; } SKY_DIRECTION;
			struct { float r, g, b, padding_; } SKY_ENERGY;
			struct { float x, y, z, padding_; } SUN_DIRECTION;
			struct { float r, g, b, padding_; } SUN_ENERGY;
		};

		static_assert(sizeof(World) == 4 * 4 + 4 * 4 + 4 * 4 + 4 * 4, "World is the expected size.");
		struct Transform {
			mat4 CLIP_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL_NORMAL;
		};
		static_assert(sizeof(Transform) == 16 * 4 + 16 * 4 + 16 * 4, " Transform is the expected size.");
		// no push constnat s
		struct Push {
			float time;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosNorTanTexVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG&, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG&);
	}objects_pipeline;

	//pools from which per-workspace things are allocated:
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	//workspaces hold per-render resources:
	struct Workspace {
		VkCommandBuffer command_buffer = VK_NULL_HANDLE; //from the command pool above;
		//reset at the start of every render.

		//location of lines data: (  streamed i.e copied over to GPU per-frame)
		Helpers::AllocatedBuffer lines_vertices_src; //host cherent; mapped
		Helpers::AllocatedBuffer lines_vertices; // device -local

		//location for LinesPipelines::Camera data: (streamed to GPU per Frame )
		Helpers::AllocatedBuffer Camera_src; // host coherent; mapped
		Helpers::AllocatedBuffer Camera; //device local
		VkDescriptorSet Camera_descriptors; //references Camera

		//location of ObjectPipeline::World data (stream to GPU fram e
		Helpers::AllocatedBuffer World_src; //host coherenet ; mapped
		Helpers::AllocatedBuffer World; //device set locat 
		VkDescriptorSet World_descriptors; // reference ot the world

		//locat for ObjectsPipeline::Transforma data: (stream to GPU per frame).
		Helpers::AllocatedBuffer Transforms_src; // host coherent ; mapped
		Helpers::AllocatedBuffer Transforms; //decice local
		VkDescriptorSet Transforms_descriptors; // references Transforms

	};
	std::vector< Workspace > workspaces;

	//-------------------------------------------------------------------
	//static scene resources:
	Helpers::AllocatedBuffer object_vertices;
	struct ObjectVertices {
		uint32_t first = 0;
		uint32_t count = 0;
	};

	std::vector<ObjectVertices> mesh_vertices;

	std::vector <Helpers::AllocatedImage> textures;
	std::vector < VkImageView > texture_views;
	VkSampler texture_sampler = VK_NULL_HANDLE;
	VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
	std::vector< VkDescriptorSet > texture_descriptors;
	//--------------------------------------------------------------------
	//Resources that change when the swapchain is resized:

	virtual void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;

	Helpers::AllocatedImage swapchain_depth_image;
	VkImageView swapchain_depth_image_view = VK_NULL_HANDLE;
	std::vector< VkFramebuffer > swapchain_framebuffers;
	//used from on_swapchain and the destructor: (framebuffers are created in on_swapchain)
	void destroy_framebuffers();

	//--------------------------------------------------------------------
	//Resources that change when time passes or the user interacts:
	struct OrbitCamera;

	virtual void update(float dt) override;
	virtual void on_input(InputEvent const &) override;

	//modal action, intercepts inputs:
	std::function< void(InputEvent const&)> action;

	float time = 0.0f;

	//for selection between cameras: 
	enum class CameraMode {
		Scene = 0, 
		Free = 1, 
		Debug = 2
	} camera_mode = CameraMode::Scene;

	CameraMode culling_camera = CameraMode::Scene;

	//used when camera_mode  = CameraMode::Free: 
	struct OrbitCamera {
		float target_x = 0.0f, target_y = 0.0f, target_z = 0.0f; //where the caerm is looking + orginting 
		float radius = 2.0f; //distancing crom thje camera 
		float azimuth = 0.0f;  //counterclockwise angle around z axis between x axis and camera direction (radian) 
		float elevation = 0.25f * float(M_PI); // angle up from xy plane to camera direction(radian s)
		float fov = 60.0f / 180.0f * float(M_PI); //vertical field of view (radians)
		float near = 0.1f; // near clippingplan 
		float far = 1000.0f; // far clipping plane
	} user_camera, debug_camera;

	//computer dd form the current camera (as set by camera_mode) during ujpdate()
	mat4 CLIP_FROM_WORLD;

	//CullingFrustum scene_cam_frustum, user_cam_frustum;

	std::array<glm::mat4x4, 3> clip_from_view;
	std::array<glm::mat4x4, 3> view_from_world;
	 
	virtual void update_free_camera(OrbitCamera& camc, CameraMode type);

	std::vector < LinesPipeline::Vertex > lines_vertices;

	ObjectsPipeline::World world;

	struct ObjectInstance {
		ObjectVertices vertices;
		ObjectsPipeline::Transform transform;
		uint32_t texture = 0;
	};

	std::vector < ObjectInstance > object_instances;
	//--------------------------------------------------------------------
	//Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};
