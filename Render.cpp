#include "Render.hpp"

#include "VK.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../Lib/stb/stb_image.h"
	
#include <GLFW/glfw3.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <deque>

Render::Render(RTG &rtg_, Scene &scene_) : rtg(rtg_) , scene(scene_) {
	//select a depth format:
	//at least on of these two must be supported, arrourding to the spec; but neihet are required

	depth_format = rtg.helpers.find_image_format(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32 },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	//create rendering pass
	{
		//Attachemetns
		std::array< VkAttachmentDescription, 2 > attachments{
			VkAttachmentDescription{ //0 - color attachment:
				.format = rtg.surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = rtg.present_layout,
			},
			VkAttachmentDescription{ //1 - depth attachment:
				.format = depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},
		};
		
		//subpasses
		VkAttachmentReference color_attachment_ref{
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference depth_attachment_ref{
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment_ref,
			.pDepthStencilAttachment = &depth_attachment_ref,
		};
		//dependencies
			// thi sdefer the image load actions for attachments
		std::array< VkSubpassDependency, 2 > dependencies{
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			},
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		}};

		VkRenderPassCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = uint32_t(dependencies.size()),
			.pDependencies = dependencies.data(),
		};

		VK(vkCreateRenderPass(rtg.device, &create_info, nullptr, &render_pass));
	}
	{ //create command pool
		VkCommandPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.graphics_queue_family.value(),
		};
		VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool));
	}

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{//create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); //for easier to read counting

		std::array < VkDescriptorPoolSize, 2> pool_sizes{

			VkDescriptorPoolSize{ //union buffer desciptors
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 2 * per_workspace, //one descriptor per set, one set per workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1 * per_workspace, //one descriptoper set, one set per workspace
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, // because CCREATE_FREE_DESCRIPTOR_SET_BIT isin;t include , we can't free individual descript allocated for this pool
			.maxSets = 3 * per_workspace, //two set per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
	}

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		{ //allocate command buffer:
			VkCommandBufferAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &workspace.command_buffer));
		}

		workspace.Camera_src = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // goign to have the gpu copy this from memory - transfer_bit
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host visible memory, coherenet (no special sync needed)
			Helpers::Mapped // get a pointer to teh memory

		);
		workspace.Camera = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //going to use a uniform buffer, also ging to have GPU copy into this memory
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // GPU local memory 
			Helpers::Unmapped //don;t get a pinter to memoery
		);

		{// allocated descriptor set for Camera descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lines_pipeline.set0_Camera,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Camera_descriptors));
		}
		//allocate for world

		workspace.World_src = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // goign to have the gpu copy this from memory - transfer_bit
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host visible memory, coherenet (no special sync needed)
			Helpers::Mapped // get a pointer to teh memory

		);
		workspace.World = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //going to use a uniform buffer, also ging to have GPU copy into this memory
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // GPU local memory 
			Helpers::Unmapped //don;t get a pinter to memoery
		);

		{// allocated descriptor set for World descriptor s
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set0_World,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.World_descriptors));

			//not weill actula fill in this descirpt set beflow 
		}

		{// allocate descriptor 
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set1_Transforms,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Transforms_descriptors));
		}
		
		{// point descript to Camera buffer:
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			VkDescriptorBufferInfo World_info{
				.buffer = workspace.World.handle,
				.offset = 0,
				.range = workspace.World.size,
			};

			std::array < VkWriteDescriptorSet, 2> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &World_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device, //device 
				uint32_t(writes.size()),// descriptor write count
				writes.data(), //pDescriptorWrites
				0, //descriptroyCopyCount
				nullptr  //pDescriptorCopies
			);
		}
	}

	{//create object vertices 
		std::vector < PosNorTanTexVertex > vertices;

		//reserve space and assign vao vbo via scene information
		vertices.resize(scene.vertices_count);
		uint32_t new_vertices_start = 0;
		size_t mesh_count = scene.meshes.size();
		mesh_vertices.assign(mesh_count, ObjectVertices());

		//create meshes 
		for (uint32_t i = 0; i < uint32_t(mesh_count); ++i)
		{
			Scene::Mesh& cur_mesh = scene.meshes[i];
			mesh_vertices[i].count = cur_mesh.count;
			mesh_vertices[i].first = new_vertices_start;

			//find mesh source via filepath
			std::ifstream file(scene.scene_path + "/" + cur_mesh.attributes[0].source, std::ios::binary); // assuming the attribute layout holds
			if (!file.is_open())
				throw std::runtime_error("Error opening file for mesh data: " + scene.scene_path + "/" + cur_mesh.attributes[0].source);
			if (!file.read(reinterpret_cast<char*>(&vertices[new_vertices_start]), cur_mesh.count * sizeof(PosNorTanTexVertex)))
			{
				throw std::runtime_error("Failed to read mesh data: " + scene.scene_path + "/" + cur_mesh.attributes[0].source);
			}
			new_vertices_start += cur_mesh.count;
		}
		assert(new_vertices_start == scene.vertices_count);

		size_t bytes = vertices.size() * sizeof(vertices[0]);
		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		//copy data to buffer
		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}

	{/// Create texture

		//correct texture loading 
		stbi_set_flip_vertically_on_load(true);
		textures.reserve(scene.textures.size() + 1); // index 0 is the default texture
		{ // texture 0 = default material
			uint8_t data[4] = { 255, 255, 255, 255 };
			// make a place for the texture to live on the GPU
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = 1, .height = 1 }, //siz eof image
				VK_FORMAT_R8G8B8A8_UNORM, //HOW TO INTERPRET IMAGE DATA(in this case, linearly -encode * -bit RGBA
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sapmle and uplaod
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,	//should be device local
				Helpers::Unmapped
			));
			//transfer data
			rtg.helpers.transfer_to_image(data, sizeof(uint8_t) * 4, textures.back());
		}

		//create scene textures
		for (uint32_t i = 0; i < scene.textures.size(); ++i)
		{
			Scene::Texture& cur_texture = scene.textures[i];
			if (cur_texture.has_src)
			{
				int width, height, n;
				unsigned char* image = stbi_load((scene.scene_path + "/" + cur_texture.source).c_str(), &width, &height, &n, 4);
				if (image == NULL)
					throw std::runtime_error("Error loading texture " + scene.scene_path + cur_texture.source);
				
				// make a place for the texture to live on the GPU:
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{ .width = uint32_t(width), .height = uint32_t(height) }, // size of image
					VK_FORMAT_R8G8B8A8_UNORM,										  // how to interpret image data (in this case, linearly-encoded 8-bit RGBA) TODO: double check format
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
					Helpers::Unmapped));
				// transfer data:
				std::cout << width << ", " << height << ", " << n << std::endl;
				rtg.helpers.transfer_to_image(image, sizeof(image[0]) * width * height * 4, textures.back());
				// free image:
				stbi_image_free(image);
			}
			else
			{
				uint8_t data[4] = { uint8_t(cur_texture.value.x * 255.0f), uint8_t(cur_texture.value.y * 255.0f), uint8_t(cur_texture.value.z * 255.0f), 255 };
				// make a place for the texture to live on the GPU:
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{ .width = 1, .height = 1 }, // size of image
					VK_FORMAT_R8G8B8A8_UNORM,  // how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
					Helpers::Unmapped));

				// transfer data:
				rtg.helpers.transfer_to_image(&data, sizeof(uint8_t) * 4, textures.back());
			}
		}
	
	}

	{ //make image views for the texture
		texture_views.reserve(textures.size());
		for (Helpers::AllocatedImage const& image : textures) {
			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = image.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = image.format,
				// .componet set swizling and is fine when zero-initialied 
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				}
			};

			VkImageView image_view = VK_NULL_HANDLE;
			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &image_view));

			texture_views.emplace_back(image_view);
		}
		assert(texture_views.size() == textures.size());
	}

	{//make sampler for the textures
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f, //doesn't matter if anisotropy ins't enabled
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS, // doesn't matter if compre isnt' enabled 
			.minLod = 0.0f,
			.maxLod = 0.0f ,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};

		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &texture_sampler));
	}

	{//create the texture descirptor pool
		
			uint32_t per_texture = uint32_t(textures.size()); //for easier to read counting

			std::array < VkDescriptorPoolSize, 1> pool_sizes{

				VkDescriptorPoolSize{ //union buffer descirpts
					.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = 1 * 1 * per_texture, //one descriptor per set, one set per trexure
				},
			};

			VkDescriptorPoolCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.flags = 0, // because CCREATE_FREE_DESCRIPTOR_SET_BIT isin;t include , we can't free individual descript allocated for this pool
				.maxSets = 1 * per_texture, //two set per texsture
				.poolSizeCount = uint32_t(pool_sizes.size()),
				.pPoolSizes = pool_sizes.data(),
			};

			VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &texture_descriptor_pool));
	}

	{//allocate and write the texture descriptor sets
		//Allocate and write the texture descriptor sets
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = texture_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &objects_pipeline.set2_TEXTURE,
		};
		texture_descriptors.assign(textures.size(), VK_NULL_HANDLE);

		for (VkDescriptorSet& descriptor_set : texture_descriptors) {
			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptor_set));
		}

		//write descptor for textures 
		std::vector < VkDescriptorImageInfo > infos(textures.size());
		std::vector < VkWriteDescriptorSet > writes(textures.size());

		for (Helpers::AllocatedImage const& image : textures) {
			size_t i = &image - &textures[0];

			infos[i] = VkDescriptorImageInfo{
				.sampler = texture_sampler,
				.imageView = texture_views[i],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			writes[i] = VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = texture_descriptors[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &infos[i],
			};
		}

		vkUpdateDescriptorSets(rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
	}
}

Render::~Render() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (texture_descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, texture_descriptor_pool, nullptr);
		texture_descriptor_pool = nullptr;

		//this also frees the descriptor sets allocated form the pool: 
		texture_descriptors.clear();
	}

	if (texture_sampler) {
		vkDestroySampler(rtg.device, texture_sampler, nullptr);
		texture_sampler = VK_NULL_HANDLE;
	}

	for (VkImageView& view : texture_views) {
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
	}

	texture_views.clear();

	for (auto& texture : textures) {
		rtg.helpers.destroy_image(std::move(texture));
	}

	textures.clear();

	rtg.helpers.destroy_buffer(std::move(object_vertices));

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		if (workspace.command_buffer != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.command_buffer);
			workspace.command_buffer = VK_NULL_HANDLE;
		}

		if (workspace.lines_vertices_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
		}
		if (workspace.lines_vertices.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
		}
		if (workspace.Camera_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
		}
		if (workspace.Camera.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}
		if (workspace.World_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World_src));
		}
		if (workspace.World.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World));
		}

		if (workspace.Transforms_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}
		if (workspace.Transforms.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
		}
		//tramsforms_descriptro sfreed when pool is destoryed
	}


	workspaces.clear();

	if (descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = nullptr;
		//this also free the descriptor sets allocated from the pool
	}
	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	objects_pipeline.destroy(rtg);

	// DESTORY COMMAND POOL
	if (command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(rtg.device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;
	}

	if (render_pass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(rtg.device, render_pass, nullptr);
		render_pass = VK_NULL_HANDLE;
	}
}

void Render::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	//[re]create framebuffers:
	// clearn up existing framebuffers
	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}
	//allocate depth images for framge buffer to share
	swapchain_depth_image = rtg.helpers.create_image(
		swapchain.extent,
		depth_format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);
	//create an imaga view of the depht image
	{
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_depth_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &swapchain_depth_image_view));
	}
	//create frambuffers poihnt to each swapchain image view and the share depth image view 
	swapchain_framebuffers.assign(swapchain.image_views.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < swapchain.image_views.size(); ++i) {
		std::array< VkImageView, 2 > attachments{
			swapchain.image_views[i],
			swapchain_depth_image_view,
		};
		VkFramebufferCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.width = swapchain.extent.width,
			.height = swapchain.extent.height,
			.layers = 1,
		};

		VK(vkCreateFramebuffer(rtg.device, &create_info, nullptr, &swapchain_framebuffers[i]));
	}
}

void Render::destroy_framebuffers() {
	for (VkFramebuffer& framebuffer : swapchain_framebuffers) {
		assert(framebuffer != VK_NULL_HANDLE);
		vkDestroyFramebuffer(rtg.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}
	swapchain_framebuffers.clear();

	assert(swapchain_depth_image_view != VK_NULL_HANDLE);
	vkDestroyImageView(rtg.device, swapchain_depth_image_view, nullptr);
	swapchain_depth_image_view = VK_NULL_HANDLE;

	rtg.helpers.destroy_image(std::move(swapchain_depth_image));

}


void Render::render(RTG &rtg_, RTG::RenderParams const &render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	//get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];
	VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	//record (into `workspace.command_buffer`) commands that run a `render_pass` that just clears `framebuffer`:
	//refsol::Tutorial_render_record_blank_frame(rtg, render_pass, framebuffer, &workspace.command_buffer);

	//reset the command buffer(clear old commands):
	VK(vkResetCommandBuffer(workspace.command_buffer, 0));
	{//begin recording
		VkCommandBufferBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,	// WILL RECORD AGAIN EVEERY SUBIT

		};
		VK(vkBeginCommandBuffer(workspace.command_buffer, &begin_info));
	}

	if (!lines_vertices.empty()) { //upload lines vertice:
		//[re-]allocate lines buffers is need;
		size_t needed_bytes = lines_vertices.size() * sizeof(lines_vertices[0]);
		if (workspace.lines_vertices_src.handle == VK_NULL_HANDLE ||
			workspace.lines_vertices_src.size < needed_bytes) {
			// round to the next multiple of 4k to avaoid re-allocating continuousely if vertex count grows slowly
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
			if (workspace.lines_vertices_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
			}
			if (workspace.lines_vertices.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
			}
			
			workspace.lines_vertices_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, //GOING TO HAVE gpu COPY FROM THIS MEMORY
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, //HOST-VISIBLE MORY, COHERENT(NO SPECIAL SYN NEEDED)
				Helpers::Mapped //get a pointer to the memory
			);

			workspace.lines_vertices = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //going ot use as a vertex buffer , also goin to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //GPU- local memory 
				Helpers::Unmapped // don;t get a pointer to the memory
			);


			std::cout << "Re-allocationed lines buffers to " << new_bytes << " bytes." << std::endl;
		}
		assert(workspace.lines_vertices_src.size == workspace.lines_vertices.size);
		assert(workspace.lines_vertices_src.size >= needed_bytes);

		//host-side copy int lines)vertices-stc;
		assert(workspace.lines_vertices_src.allocation.mapped);
		std::memcpy(workspace.lines_vertices_src.allocation.data(), lines_vertices.data(), needed_bytes);
		
		//GPU doing host to GPu copy 
		//decice -size copy form lines)_vertical _src -> lines_vertices 
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.lines_vertices_src.handle, workspace.lines_vertices.handle, 1, &copy_region);
	}

	{//upload camera info
		LinesPipeline::Camera camera{
			.CLIP_FROM_WORLD = CLIP_FROM_WORLD
		};
		assert(workspace.Camera_src.size == sizeof(camera));

		//host-side copy into Camera_src:
		memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));

		//ad device-sside copy form Camera_src -> cmera: 
		assert(workspace.Camera_src.size == workspace.Camera.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.Camera_src.size,
		};

		vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &copy_region);

	}

	{//upload world info
		assert(workspace.Camera_src.size = sizeof(world));

		//host-side copy into Camera_src:
		memcpy(workspace.World_src.allocation.data(), &world, sizeof(world));

		//ad device-sside copy form Camera_src -> cmera: 
		assert(workspace.World_src.size == workspace.World.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.World_src.size,
		};

		vkCmdCopyBuffer(workspace.command_buffer, workspace.World_src.handle, workspace.World.handle, 1, &copy_region);

	}

	if (!object_instances.empty()) { //upload obecjt transforms
		//[re-]allocate lines buffers is need;
		size_t needed_bytes = object_instances.size() * sizeof(ObjectsPipeline::Transform);
		if (workspace.Transforms_src.handle == VK_NULL_HANDLE ||
			workspace.Transforms_src.size < needed_bytes) {
			// round to the next multiple of 4k to avaoid re-allocating continuousely if vertex count grows slowly
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
			if (workspace.Transforms_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
			}
			if (workspace.Transforms.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
			}

			workspace.Transforms_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, //GOING TO HAVE gpu COPY FROM THIS MEMORY
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, //HOST-VISIBLE MORY, COHERENT(NO SPECIAL SYN NEEDED)
				Helpers::Mapped //get a pointer to the memory
			);

			workspace.Transforms = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //going ot use as a vertex buffer , also goin to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //GPU- local memory 
				Helpers::Unmapped // don;t get a pointer to the memory
			);

			// update the descriptor set:
			VkDescriptorBufferInfo Transforms_info{
				.buffer = workspace.Transforms.handle,
				.offset = 0,
				.range = workspace.Transforms.size,
			};

			std::array < VkWriteDescriptorSet, 1> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Transforms_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &Transforms_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()), writes.data(), //descriptors count, dat
				0, nullptr //descripto copies count , data
			);

			std::cout << "Re-allocationed objhects buffers to " << new_bytes << " bytes." << std::endl;
		}
		assert(workspace.Transforms_src.size == workspace.Transforms.size);
		assert(workspace.Transforms_src.size >= needed_bytes);

		{
			//copy transform into Transforms_src
			assert(workspace.Transforms_src.allocation.mapped);
			ObjectsPipeline::Transform* out = reinterpret_cast<ObjectsPipeline::Transform * > (workspace.Transforms_src.allocation.data());
			//strict aliasing violation, but it doesn't matter
			for (ObjectInstance const& inst : object_instances) {
				*out = inst.transform; 
				++out;
			}
		}

		//decice -size copy form lines)_vertical _src -> lines_vertices 
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Transforms_src.handle, workspace.Transforms.handle, 1, &copy_region);
	}

	{//memory barrier to make sure copies complete before rendign happens:
		VkMemoryBarrier memory_barrier{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		};

		vkCmdPipelineBarrier(workspace.command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, //srcStageMask
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // dstStagemask
			0, //dependency Flags
			1, &memory_barrier, //mmoryBarriers (count, data)
			0, nullptr, // bufferMomroyBarrier( count, data)
			0, nullptr  // imageMemoryBarrier(count, data)
		);
	}

	{//render pass
		std::array< VkClearValue, 2 > clear_values{

			VkClearValue{.color{.float32{0.0f, 0.0f, 0.0f, 1.0f } } },
			VkClearValue{.depthStencil{.depth = 1.0f, .stencil = 0 } },
		};

		VkRenderPassBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,

			.renderPass = render_pass,
			.framebuffer = framebuffer,
			.renderArea{ 
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			},
			.clearValueCount = uint32_t (clear_values.size()), 
			.pClearValues = clear_values.data(),
		};



		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	
		// todo: RUNPIPELINES HERE
		{ //set scissor and viewport rectangle
			VkExtent2D extent = rtg.swapchain_extent;
			VkOffset2D offset = { .x = 0, .y = 0 };

			VkRect2D scissor{
				.offset = offset,
				.extent = extent,
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);

			VkViewport viewport{
				.x = float(offset.x),
				.y = float(offset.y),
				.width = float(extent.width),
				.height = float(extent.height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};
			vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
		}

		//{//draw with the backgorun pipeline
		//	vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);
		//	
		//	{//push time:
		//		BackgroundPipeline::Push push{
		//			.time = float(time),
		//		};
		//		vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
		//	}
		//	vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		//}
		if (!lines_vertices.empty())
		{//draw with the lines pipeline;
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);

			{//use lines_vertice (offset 0) as vertex buffer bindign 0:
				std::array < VkBuffer, 1> vertex_buffers{ workspace.lines_vertices.handle };
				std::array <VkDeviceSize, 1> offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{//bind teh camera descript set:
				std::array < VkDescriptorSet, 1> descriptor_sets{
					workspace.Camera_descriptors, // 0. camera
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command_buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					lines_pipeline.layout, //pipline layout 
					0, // first set 
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), // descriptor set count, ptr
					0, nullptr // dynamics offsets count, ptr
				);

			}

			//draw line vertice
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}

		if(!object_instances.empty())
		{//draw with the object pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);
			
			{//push time:
				ObjectsPipeline::Push push{
					.time = float(time),
				};
				vkCmdPushConstants(workspace.command_buffer, objects_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
			}

			{//use object vertices (offset 0 ) as vertex buffer binging 0:
				
				std::array < VkBuffer, 1 > vertex_buffers{ object_vertices.handle };
				std::array < VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

			}
			{//bind world and transforms descript set:
				std::array< VkDescriptorSet, 2 > descriptor_sets{
					workspace.World_descriptors, //0: world 
					workspace.Transforms_descriptors, //1: Transforms
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer,  // command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					objects_pipeline.layout, //pipline layout
					0, //first set 
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), //descriptorr set coutn, ptr
					0, nullptr// dynamic offset cout, ptr
				);
			}

			 //camera descriptor set is stil bounde (!)


			//draw all instaces 
			for (ObjectInstance const& inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);

				//bind texture descriptor set:

				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					objects_pipeline.layout, //pipeline layout 
					2, //second set 
					1, &texture_descriptors[inst.texture], //descriptor sets count, ptr
					0, nullptr // dynamic offsets count, ptr
				);

				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}
		}


		
		vkCmdEndRenderPass(workspace.command_buffer);
	
	}

	//end recoding
	VK(vkEndCommandBuffer(workspace.command_buffer) );

	//submit `workspace.command buffer` for the GPU to run:
	{
		std::array< VkSemaphore, 1 > wait_semaphores{
			render_params.image_available
		};
		std::array< VkPipelineStageFlags, 1 > wait_stages{
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		static_assert(wait_semaphores.size() == wait_stages.size(), "every semaphore needs a stage");

		std::array< VkSemaphore, 1 > signal_semaphores{
			render_params.image_done
		};
		VkSubmitInfo submit_info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = uint32_t(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = wait_stages.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &workspace.command_buffer,
			.signalSemaphoreCount = uint32_t(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data(),
		};

		VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, render_params.workspace_available));
	}
}


void Render::update(float dt) {
	time = std::fmod(time + dt, 60.0f);

	if(camera_mode == CameraMode::Scene)
	{
		//camera rotating around the origin: 
		float ang = float(M_PI) * 2.0f * 10.0f * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
			60.0f * float(M_PI / 180.0f), //vfov 
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
			0.1f, // near 
			1000.0f //far 
		) * look_at(
			3.0f * std::cos(ang), 3.0f * std::sin(ang), 1.0f, //eye
			0.0f, 0.0f, 0.5f, //target 
			0.0f, 0.0f, 1.0f //up
		);
	}
	else if (camera_mode == CameraMode::Free) {
		CLIP_FROM_WORLD = perspective(
			free_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),  //aspect 
			free_camera.near,
			free_camera.far
			) * orbit(
				free_camera.target_x, free_camera.target_y, free_camera.target_z, 
				free_camera.azimuth, free_camera.elevation, free_camera.radius
			);
	}
	else {
		assert(0 && "only two camera modes");
	}

	{//static sun and sky 
		world.SKY_DIRECTION.x = 0.0f;
		world.SKY_DIRECTION.y = 0.0f;
		world.SKY_DIRECTION.z = 1.0f;

		world.SKY_ENERGY.r = 0.1f;
		world.SKY_ENERGY.g = 0.1f;
		world.SKY_ENERGY.b = 0.2f;

		world.SUN_DIRECTION.x = 6.0f / 23.0f; 
		world.SUN_DIRECTION.y = 13.0f / 23.0f; 
		world.SUN_DIRECTION.x = 18.0f / 23.0f;

		world.SUN_ENERGY.r = 1.0f;
		world.SUN_ENERGY.g = 1.0f;
		world.SUN_ENERGY.b = 0.9f;

	}
	
	{ // fill object instances with scene hiearchy
		object_instances.clear();

		std::deque<glm::mat4x4> transform_stack;

		std::function<void(uint32_t)> draw_node = [&](uint32_t i)
		{
				Scene::Node& cur_node = scene.nodes[i];
				//iterating through the tree to determine position
				glm::mat4x4 cur_node_transform_in_parent = (cur_node.transform.parent_from_local());
				if (transform_stack.empty())
				{
					transform_stack.push_back(cur_node_transform_in_parent);
				}
				else
				{
					glm::mat4x4 parent_node_transform_in_world = transform_stack.back();
					transform_stack.push_back(parent_node_transform_in_world * cur_node_transform_in_parent);
				}

				// draw children mesh
				for (uint32_t child_index : cur_node.children)
				{
					draw_node(child_index);
				}

				// draw own mesh
				if (int32_t cur_mesh_index = cur_node.mesh_index; cur_mesh_index != -1)
				{
					mat4 WORLD_FROM_LOCAL = to_mat4(transform_stack.back());

					uint32_t texture_index = 0;
					if (scene.meshes[cur_mesh_index].material_index != -1)
					{
						texture_index = scene.materials[scene.meshes[cur_mesh_index].material_index].texture_index + 1;
					}

					object_instances.emplace_back(ObjectInstance{
						.vertices = mesh_vertices[cur_mesh_index],   // <-- ObjectVertices slice
						.transform{
							.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
							.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
							.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
						},
						.texture = 1,
						});

					transform_stack.pop_back();
				}

		};
		// traverse the scene hiearchy:
		for (uint32_t j = 0; j < scene.root_nodes.size(); ++j)
		{
			transform_stack.clear();
			draw_node(scene.root_nodes[j]);
		}
	}

}


void Render::on_input(InputEvent const &evt) {
	//if there is a current saction, it get input priority: 
	if (action) {
		action(evt);
		return;
	}


	//general controls:
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_TAB) {
		// swithc camera mode 
		camera_mode = CameraMode((int(camera_mode) + 1) % 2);
		return;
	}

	//free camera controls:
	if (camera_mode == CameraMode::Free) {
		if (evt.type == InputEvent::MouseWheel) {
			//change distance by 10% every scoll click : 
			free_camera.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);

			//make sure camera isn't too close or too far from target: 
			free_camera.radius = std::max(free_camera.radius, 0.5f * free_camera.near);
			free_camera.radius = std::min(free_camera.radius, 2.0f * free_camera.far);
			return;
		}

		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
			//start panning
			
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = free_camera;


			action = [this, init_x, init_y, init_camera](InputEvent const& evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					//cancle uypon button lifted: 
					action = nullptr;
					return;
				}
				if (evt.type == InputEvent::MouseMotion) {
					//handle motion 

					//image height at plane of target pont: 
					float height = 2.0f * std::tan(free_camera.fov * 0.5f) * free_camera.radius;

					//motion, therefore at target piont: 
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height;
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height * height;
					//negative belcoe glfw useing y-down coordinat system 


					//compute camera transform to extract right 
					mat4 camera_from_world = orbit(
						init_camera.target_x, init_camera.target_y, init_camera.target_z,
						init_camera.azimuth, init_camera.elevation, init_camera.radius
					);

					//move the desiere ddistance
					free_camera.target_x = init_camera.target_x - dx * camera_from_world[0] - dy * camera_from_world[1];
					free_camera.target_y = init_camera.target_y - dx * camera_from_world[4] - dy * camera_from_world[5];
					free_camera.target_z = init_camera.target_z - dx * camera_from_world[8] - dy * camera_from_world[9];

					return;
				}
				};

		}
		else if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
			//start tumbling


			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = free_camera;

			action = [this, init_x, init_y, init_camera](InputEvent const& evt) {
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					action = nullptr; 
					return;
				}
				if (evt.type == InputEvent::MouseMotion) {
					//motion, normalized so 1.0 is window height:
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height;
				float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height; //note: negated because glfw uses y-down coordinate system

					//rotate camera based on motion:
					float speed = float(M_PI); //how much rotation happens at one full window height
					float flip_x = (std::abs(init_camera.elevation) > 0.5f * float(M_PI) ? -1.0f : 1.0f); //switch azimuth rotation when camera is upside-down
					free_camera.azimuth = init_camera.azimuth - dx * speed * flip_x;
					free_camera.elevation = init_camera.elevation - dy * speed;

					//reduce azimuth and elevation to [-pi,pi] range:
					const float twopi = 2.0f * float(M_PI);
					free_camera.azimuth -= std::round(free_camera.azimuth / twopi) * twopi;
					free_camera.elevation -= std::round(free_camera.elevation / twopi) * twopi;
					return;
				}
			};

			return;
		}

	}
}
