#include "CubePipeline.hpp"

#include "Helpers.hpp"

#include "VK.hpp"

static uint32_t comp_brdf[] =
#include "spv/brdf.comp.inl"
    ;

static uint32_t comp_irradiance[] =
#include "spv/lambertian-irradiance.comp.inl"
    ;

static uint32_t comp_specular[] =
#include "spv/ggx.comp.inl"
    ;
;
;

void CubePipeline::create(RTG &rtg)
{
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        };

        VkDescriptorSetLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = uint32_t(bindings.size()),
            .pBindings = bindings.data(),
        };

        VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set0_env));
    }

    {
        VkPushConstantRange range{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(IrradiancePush),
        };

        VkPipelineLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &set0_env,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        };

        VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &irradiance_layout));
    }

    {
        VkPushConstantRange range{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(SpecularPush),
        };

        VkPipelineLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &set0_env,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        };

        VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &specular_layout));
    }

    {
        VkDescriptorSetLayoutBinding binding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };

        VkDescriptorSetLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &binding,
        };

        VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set1_brdf));
    }

    {
        VkPushConstantRange range{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(BrdfPush),
        };

        VkPipelineLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &set1_brdf,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        };

        VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &brdf_layout));
    }

    auto make_compute = [&](uint32_t const *code, size_t bytes, VkPipelineLayout layout, VkPipeline *out)
    {
        VkShaderModule mod = rtg.helpers.create_shader_module(code, bytes);

        VkPipelineShaderStageCreateInfo shader_stage{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = mod,
            .pName = "main",
        };

        VkComputePipelineCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = shader_stage,
            .layout = layout,
        };

        VK(vkCreateComputePipelines(rtg.device, VK_NULL_HANDLE, 1, &create_info, nullptr, out));
        vkDestroyShaderModule(rtg.device, mod, nullptr);
    };

    make_compute(comp_irradiance, sizeof(comp_irradiance), irradiance_layout, &irradiance_pipeline);
    make_compute(comp_specular, sizeof(comp_specular), specular_layout, &specular_pipeline);
    make_compute(comp_brdf, sizeof(comp_brdf), brdf_layout, &brdf_pipeline);
}

void CubePipeline::destroy(RTG &rtg)
{

    if (irradiance_pipeline)
        vkDestroyPipeline(rtg.device, irradiance_pipeline, nullptr);
    if (specular_pipeline)
        vkDestroyPipeline(rtg.device, specular_pipeline, nullptr);
    if (brdf_pipeline)
        vkDestroyPipeline(rtg.device, brdf_pipeline, nullptr);
    if (irradiance_layout)
        vkDestroyPipelineLayout(rtg.device, irradiance_layout, nullptr);
    if (specular_layout)
        vkDestroyPipelineLayout(rtg.device, specular_layout, nullptr);
    if (brdf_layout)
        vkDestroyPipelineLayout(rtg.device, brdf_layout, nullptr);

    irradiance_pipeline = VK_NULL_HANDLE;
    specular_pipeline = VK_NULL_HANDLE;
    brdf_pipeline = VK_NULL_HANDLE;
    irradiance_layout = VK_NULL_HANDLE;
    specular_layout = VK_NULL_HANDLE;
    brdf_layout = VK_NULL_HANDLE;
    if (set1_brdf != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(rtg.device, set1_brdf, nullptr);
        set1_brdf = VK_NULL_HANDLE;
    }

    if (set0_env != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(rtg.device, set0_env, nullptr);
        set0_env = VK_NULL_HANDLE;
    }
}
