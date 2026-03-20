#include "CubePipeline.hpp"

#include "Helpers.hpp"

#include "VK.hpp"

static uint32_t comp_code[] =
#include "spv/cube.comp.inl"
;


void CubePipeline::create(RTG& rtg) {
    VkShaderModule comp_module = rtg.helpers.create_shader_module(comp_code);

    {//the set01_face holds the input and output image
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
            VkDescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
        };

        VkDescriptorSetLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = uint32_t(bindings.size()),
            .pBindings = bindings.data(),
        };

        VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set01_face));
    }

    { //the set2 layout holds roughness info (and maybe, someday, more brdf params):
        std::array< VkDescriptorSetLayoutBinding, 1 > bindings{
            VkDescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
        };

        VkDescriptorSetLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = uint32_t(bindings.size()),
            .pBindings = bindings.data(),
        };

        VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set2_params));
    }

    {//create pipeline layout:
        std::array<VkDescriptorSetLayout, 3> layouts{
            set01_face,
            set01_face,
            set2_params,
        };

        VkPipelineLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = uint32_t(layouts.size()),
            .pSetLayouts = layouts.data(),
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
        };
        VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &layout));
    }

    { //create pipeline:
        VkPipelineShaderStageCreateInfo shader_stage{
           .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
           .stage = VK_SHADER_STAGE_COMPUTE_BIT,
           .module = comp_module,
           .pName = "main",
        };
        //all of the above structures bundled into one pipeline create_info
        VkComputePipelineCreateInfo create_info{
           .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
           .stage = shader_stage,
           .layout = layout,
        };

        VK(vkCreateComputePipelines(rtg.device, VK_NULL_HANDLE, 1, &create_info, nullptr, &handle));

        // Destroy shader module after pipeline creation
        vkDestroyShaderModule(rtg.device, comp_module, nullptr);

    }
}

void CubePipeline::destroy(RTG& rtg) {
    if (handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(rtg.device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }

    if (layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(rtg.device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }

    if (set2_params != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(rtg.device, set2_params, nullptr);
        set2_params = VK_NULL_HANDLE;
    }

    if (set01_face != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(rtg.device, set01_face, nullptr);
        set01_face = VK_NULL_HANDLE;
    }
}