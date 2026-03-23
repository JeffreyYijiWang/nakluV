#pragma once
#include "RTG.hpp"

struct CubePipeline {
	//descriptor set layouts:
	VkDescriptorSetLayout set01_face = VK_NULL_HANDLE; //used for input
	VkDescriptorSetLayout set2_params = VK_NULL_HANDLE; //used for ggx only

	//types for descriptors:
	struct Face {
		struct {
			float m0, m1, m2, padding0_;
			float m3, m4, m5, padding1_;
			float m6, m7, m8, padding2_;
		} WORLD_FROM_PX;
	};
	static_assert(sizeof(Face) == (3 * 4) * 4, "Face descriptor is the expected size.");

	struct Params {
			int32_t mode;         // 0 = LambertianDirect, 1 = LambertianMonteCarlo, 2 = GGX ...
			int32_t sample_count; // MC samples, 0 for direct
			float roughness;      // used for GGX, ignored for Lambertian
			float pad0;
	}cube_param;
	static_assert(sizeof(Params) == 16, "Params descriptor is the expected size.");

	//no push constants

	VkPipelineLayout layout = VK_NULL_HANDLE;

	VkPipeline handle = VK_NULL_HANDLE;

	void create(RTG&);
	void destroy(RTG&);

};