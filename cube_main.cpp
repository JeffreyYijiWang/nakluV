
#include "CubePipeline.hpp"

#include "RTG.hpp"

#include "Scene.hpp"
#include "glm.hpp"
#include "../Lib/stb/stb_image_write.h"
#include <iostream>


struct GPUFace {
	Helpers::AllocatedImage image;
	Helpers::AllocatedBuffer buffer; //for transform info
	VkImageView view = VK_NULL_HANDLE;
	VkDescriptorSet descriptors = VK_NULL_HANDLE;
	void create(RTG& rtg, VkDescriptorPool descriptor_pool, CubePipeline const& pipeline, uint32_t const sz, glm::vec3* const data) {

		std::vector< glm::vec4 > data_padded;
		data_padded.reserve(sz * sz);
		for (uint32_t i = 0; i < sz * sz; ++i) {
			data_padded.emplace_back(glm::vec4(data[i], 0.0f));
		}

		//create image:
		image = rtg.helpers.create_image(
			VkExtent2D{ .width = sz, .height = sz },
			VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		//actually upload the image:
		//rtg.helpers.transfer_to_image(data_padded.data(), sizeof(data_padded[0]) * (size_t)sz * (size_t)sz, image, VK_IMAGE_LAYOUT_GENERAL);
		rtg.helpers.transfer_to_image(data_padded.data(), sizeof(data_padded[0]) * (size_t)sz * (size_t)sz, image);

		//---- buffer ----
		{ //buffer:
			buffer = rtg.helpers.create_buffer(
				sizeof(CubePipeline::Face),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			);

			CubePipeline::Face face_info{};

			glm::vec3 s = glm::vec3(0.0f, 0.0f, -1.0f);
			glm::vec3 t = glm::vec3(0.0f, -1.0f, -0.0f);
			glm::vec3 center = glm::vec3(1.0f, 0.0f, 0.0f);

			face_info.WORLD_FROM_PX.m0 = 2.0f * s.x / float(sz);
			face_info.WORLD_FROM_PX.m1 = 2.0f * s.y / float(sz);
			face_info.WORLD_FROM_PX.m2 = 2.0f * s.z / float(sz);

			face_info.WORLD_FROM_PX.m3 = 2.0f * t.x / float(sz);
			face_info.WORLD_FROM_PX.m4 = 2.0f * t.y / float(sz);
			face_info.WORLD_FROM_PX.m5 = 2.0f * t.z / float(sz);

			float corner = 1.0f - 2.0f / float(sz) * 0.5f;
			face_info.WORLD_FROM_PX.m6 = center.x - corner * s.x - corner * t.x;
			face_info.WORLD_FROM_PX.m7 = center.y - corner * s.y - corner * t.y;
			face_info.WORLD_FROM_PX.m8 = center.z - corner * s.z - corner * t.z;

			rtg.helpers.transfer_to_buffer(&face_info, sizeof(face_info), buffer);
		}

		{ //image view:
			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = image.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = image.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};
			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &view));
		}

		{ //descriptor set with world_from_px and storage image:
			{ //allocate:
				VkDescriptorSetAllocateInfo alloc_info{
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
					.descriptorPool = descriptor_pool,
					.descriptorSetCount = 1,
					.pSetLayouts = &pipeline.set01_face,
				};
				VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptors));
			}

			//write:
			VkDescriptorBufferInfo buffer_info{
				.buffer = buffer.handle,
				.offset = 0,
				.range = buffer.size,
			};
			VkDescriptorImageInfo image_info{
				.imageView = view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			};
			std::array< VkWriteDescriptorSet, 2 > writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = descriptors,
					.dstBinding = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &buffer_info,
				},

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = descriptors,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &image_info,
				},
			};

			vkUpdateDescriptorSets(rtg.device,
				uint32_t(writes.size()), writes.data(),
				0, nullptr
			);

		}

	}
	void destroy(RTG& rtg) {
		//well c'mon we just quit the process anyway the driver can take care of it for us right?
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
		rtg.helpers.destroy_buffer(std::move(buffer));
		rtg.helpers.destroy_image(std::move(image));
	}
};

int main(int argc, char** argv) {
	//main wrapped in a try-catch so we can print some debug info about uncaught exceptions:
	try {

		//configure application:
		RTG::Configuration configuration;
		configuration.cube = true;

		configuration.application_info = VkApplicationInfo{
			.pApplicationName = "Cube Render",
			.applicationVersion = VK_MAKE_VERSION(0,0,0),
			.pEngineName = "Unknown",
			.engineVersion = VK_MAKE_VERSION(0,0,0),
			.apiVersion = VK_API_VERSION_1_3
		};

		bool print_usage = false;

		try {
			configuration.parse(argc, argv);
		}
		catch (std::runtime_error& e) {
			std::cerr << "Failed to parse arguments:\n" << e.what() << std::endl;
			print_usage = true;
		}

		if (print_usage) {
			std::cerr << "Usage:" << std::endl;
			RTG::Configuration::usage([](const char* arg, const char* desc) {
				std::cerr << "    " << arg << "\n        " << desc << std::endl;
				});
			return 1;
		}

		configuration.headless = true;


		//loads vulkan library, creates surface, initializes helpers:
		RTG rtg(configuration);

		
		//TODO: create computation pipeline
		CubePipeline pipeline;
		pipeline.create(rtg);

		//TODO: load images / create descriptors


		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		{ //create descriptor pool
			std::array< VkDescriptorPoolSize, 2> pool_sizes{
				VkDescriptorPoolSize{
					.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.descriptorCount = 6 * 2 + 1, //one for each input and output cube face, plus one for params
				},
				VkDescriptorPoolSize{
					.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						.descriptorCount = 6 * 2, //one for each input and output cube face
				},
			};

			VkDescriptorPoolCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.flags = 0, //because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
				.maxSets = 12 + 1, //one set per in/out cube face, plus one for params
				.poolSizeCount = uint32_t(pool_sizes.size()),
				.pPoolSizes = pool_sizes.data(),
			};

			VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
		}

		VkCommandPool command_pool = VK_NULL_HANDLE;
		{ //create command pool
			VkCommandPoolCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
				.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
				.queueFamilyIndex = rtg.graphics_queue_family.value(),
			};
			VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool));
		}

		VkCommandBuffer command_buffer = VK_NULL_HANDLE;
		{ //allocate a command buffer from the command pool:
			VkCommandBufferAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &command_buffer));
		}

		size_t sz = 128;
		std::vector< glm::vec3 > data(sz * sz, glm::vec3(1.0f, 0.0f, 1.0f));


		GPUFace in_face;
		in_face.create(rtg, descriptor_pool, pipeline, (uint32_t)sz, data.data());
		GPUFace out_face;
		out_face.create(rtg, descriptor_pool, pipeline, (uint32_t)sz, data.data());

		{ //run pipeline

			VK(vkResetCommandBuffer(command_buffer, 0));

			{ //begin recording:
				VkCommandBufferBeginInfo begin_info{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
					.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //will record again every submit
				};
				VK(vkBeginCommandBuffer(command_buffer, &begin_info));
			}

			//use the cube pipeline:
			vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);

			{ //bind in/out descriptor sets:
				std::array< VkDescriptorSet, 2 > descriptor_sets{
					in_face.descriptors,
					out_face.descriptors,
					/*params_descriptors, */
				};
				vkCmdBindDescriptorSets(
					command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_COMPUTE, //pipeline bind point
					pipeline.layout, //pipeline layout
					0, //first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), //descriptor sets count, ptr
					0, nullptr //dynamic offsets count, ptr
				);
			}


			//actually run the thing:
			vkCmdDispatchBase(command_buffer, 0, 0, 1, (uint32_t)sz, (uint32_t) sz, 1);

			//done recording:
			VK(vkEndCommandBuffer(command_buffer));

			{ //submit command buffer:
				VkSubmitInfo submit_info{
					.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
					.commandBufferCount = 1,
					.pCommandBuffers = &command_buffer,
				};

				VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, nullptr));
			}

			VK(vkDeviceWaitIdle(rtg.device));
		}

		std::cout << "Computing: done." << std::endl;


		//TODO: get back results

		//TODO: destroy all the things
		in_face.destroy(rtg);
		out_face.destroy(rtg);

		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = VK_NULL_HANDLE;

		vkDestroyCommandPool(rtg.device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;

		pipeline.destroy(rtg);

	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return 1;
	}
}