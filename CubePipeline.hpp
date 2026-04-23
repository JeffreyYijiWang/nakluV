#pragma once
#include "RTG.hpp"


struct CubePipeline
{
	// descriptor set layouts:
	VkDescriptorSetLayout set0_env = VK_NULL_HANDLE;  // used for input
	VkDescriptorSetLayout set1_brdf = VK_NULL_HANDLE; // used for ggx only

	struct IrradiancePush
	{
		uint32_t size;
		uint32_t numSamples;
	};

	struct SpecularPush // GGX
	{
		uint32_t size;
		uint32_t numSamples;
		float roughness;
		float pad;
	};

	struct BrdfPush
	{ // brdf_lut
		uint32_t size;
		uint32_t numSamples;
	};

	VkPipelineLayout irradiance_layout = VK_NULL_HANDLE;
	VkPipeline irradiance_pipeline = VK_NULL_HANDLE;

	VkPipelineLayout specular_layout = VK_NULL_HANDLE;
	VkPipeline specular_pipeline = VK_NULL_HANDLE;

	VkPipelineLayout brdf_layout = VK_NULL_HANDLE;
	VkPipeline brdf_pipeline = VK_NULL_HANDLE;

	void create(RTG &);
	void destroy(RTG &);
};