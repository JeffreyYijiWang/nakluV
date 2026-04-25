
#include "CubePipeline.hpp"

#include "RTG.hpp"

#include "Scene.hpp"
#include "glm.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../Lib/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../Lib/stb/stb_image_write.h"
#include "data_path.hpp"
#include "../Lib/sejp.hpp"
#include "timer.hpp"

#include <iostream>
#include <algorithm>

#include "../Lib/glm/glm/gtc/packing.hpp"

struct Half4
{
	uint16_t x, y, z, w;
};

// static Half4 pack_half4(glm::vec4 const &v)
// {
// 	return Half4{
// 		glm::packHalf1x16(v.x),
// 		glm::packHalf1x16(v.y),
// 		glm::packHalf1x16(v.z),
// 		glm::packHalf1x16(v.w)};
// }

static glm::vec4 unpack_half4(Half4 const &h)
{
	return glm::vec4(
		glm::unpackHalf1x16(h.x),
		glm::unpackHalf1x16(h.y),
		glm::unpackHalf1x16(h.z),
		glm::unpackHalf1x16(h.w));
}

struct GPUCubeMap
{
	Helpers::AllocatedImage image;
	VkImageView cube_view = VK_NULL_HANDLE;
	VkImageView sampled_array_view = VK_NULL_HANDLE; // for sampler2DArray
	std::vector<VkImageView> storage_views;			 // one per writable mip
	VkSampler sampler = VK_NULL_HANDLE;

	void destroy(RTG &rtg)
	{
		for (VkImageView v : storage_views)
		{
			if (v)
				vkDestroyImageView(rtg.device, v, nullptr);
		}
		storage_views.clear();

		if (cube_view)
			vkDestroyImageView(rtg.device, cube_view, nullptr);
		if (sampler)
			vkDestroySampler(rtg.device, sampler, nullptr);
		if (image.handle)
			rtg.helpers.destroy_image(std::move(image));

		cube_view = VK_NULL_HANDLE;
		sampler = VK_NULL_HANDLE;
	}
};

static VkImageView make_cube_view(RTG &rtg, Helpers::AllocatedImage const &image, uint32_t mip_count)
{
	VkImageView view = VK_NULL_HANDLE;
	VkImageViewCreateInfo ci{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image.handle,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.format = image.format,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = mip_count,
			.baseArrayLayer = 0,
			.layerCount = 6,
		},
	};
	VK(vkCreateImageView(rtg.device, &ci, nullptr, &view));
	return view;
}

VkImageView make_sampled_array_view(RTG &rtg, Helpers::AllocatedImage const &image, uint32_t mip_count)
{
	VkImageView view = VK_NULL_HANDLE;

	VkImageViewCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image.handle,
		.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		.format = image.format,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = mip_count,
			.baseArrayLayer = 0,
			.layerCount = 6,
		},
	};

	VK(vkCreateImageView(rtg.device, &create_info, nullptr, &view));
	return view;
}

static VkImageView make_storage_view_for_mip(RTG &rtg, Helpers::AllocatedImage const &image, uint32_t mip)
{
	VkImageView view = VK_NULL_HANDLE;
	VkImageViewCreateInfo ci{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image.handle,
		.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		.format = image.format,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = mip,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 6,
		},
	};
	VK(vkCreateImageView(rtg.device, &ci, nullptr, &view));
	return view;
}

static VkSampler make_cube_sampler(RTG &rtg, uint32_t mip_count)
{
	VkSampler sampler = VK_NULL_HANDLE;
	VkSamplerCreateInfo ci{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.0f,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = float(mip_count - 1),
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
		.unnormalizedCoordinates = VK_FALSE,
	};
	VK(vkCreateSampler(rtg.device, &ci, nullptr, &sampler));
	return sampler;
}
struct CPUCubeMap
{
	uint32_t size = 0;
	std::array<std::vector<glm::vec3>, 6> faces;
};

static glm::vec3 rgbe_to_linear(uint8_t r, uint8_t g, uint8_t b, uint8_t e)
{
	if (e == 0)
		return glm::vec3(0.0f);
	float scale = std::ldexp(1.0f, int(e) - (128 + 8));

	glm::vec3 c(
		float(r) * scale,
		float(g) * scale,
		float(b) * scale);

	for (int k = 0; k < 3; ++k)
	{
		if (!std::isfinite(c[k]))
			c[k] = 0.0f;
		c[k] = std::max(c[k], 0.0f);
	}
	return glm::vec3(float(r) * scale, float(g) * scale, float(b) * scale);
}

static CPUCubeMap load_rgbe_cubemap_vertical(std::string const &filename)
{
	int w = 0, h = 0, comp = 0;
	stbi_uc *pixels = stbi_load(data_path(filename).c_str(), &w, &h, &comp, 4);
	if (!pixels)
	{
		throw std::runtime_error("Failed to load cubemap PNG: " + filename);
	}

	if (h % 6 != 0)
	{
		stbi_image_free(pixels);
		throw std::runtime_error("Cubemap PNG height must be 6 * face_size.");
	}
	if (w != h / 6)
	{
		stbi_image_free(pixels);
		throw std::runtime_error("Expected vertical strip: height = 6 * width.");
	}

	uint32_t face_size = uint32_t(w);
	std::cout << face_size << std::endl;
	CPUCubeMap cube;
	cube.size = face_size;
	for (auto &face : cube.faces)
	{
		face.resize(size_t(face_size) * size_t(face_size));
	}

	// face order in your uploaded image:
	// 0:+X, 1:-X, 2:+Y, 3:-Y, 4:+Z, 5:-Z
	for (uint32_t face = 0; face < 6; ++face)
	{
		for (uint32_t y = 0; y < face_size; ++y)
		{
			for (uint32_t x = 0; x < face_size; ++x)
			{
				uint32_t src_x = x;
				uint32_t src_y = face * face_size + y;

				size_t idx = 4 * (size_t(src_y) * size_t(w) + size_t(src_x));
				uint8_t r = pixels[idx + 0];
				uint8_t g = pixels[idx + 1];
				uint8_t b = pixels[idx + 2];
				uint8_t e = pixels[idx + 3];

				cube.faces[face][size_t(y) * face_size + x] = rgbe_to_linear(r, g, b, e);
			}
		}
	}

	stbi_image_free(pixels);
	return cube;
}

static glm::u8vec4 linear_to_rgbe(glm::vec3 c)
{
	float maxc = std::max(c.r, std::max(c.g, c.b));
	if (maxc < 1e-32f)
		return glm::u8vec4(0, 0, 0, 0);

	int exp;
	float mant = std::frexp(maxc, &exp);
	float scale = mant * 256.0f / maxc;

	return glm::u8vec4(
		uint8_t(std::clamp(c.r * scale, 0.0f, 255.0f)),
		uint8_t(std::clamp(c.g * scale, 0.0f, 255.0f)),
		uint8_t(std::clamp(c.b * scale, 0.0f, 255.0f)),
		uint8_t(exp + 128));
}

static void write_rgbe_cubemap_vertical(
	std::string const &filename,
	std::array<std::vector<glm::vec4>, 6> const &faces,
	uint32_t face_size)
{
	int w = int(face_size);
	int h = int(face_size * 6);

	std::vector<uint8_t> pixels(size_t(w) * size_t(h) * 4, 0);

	for (uint32_t face = 0; face < 6; ++face)
	{
		for (uint32_t y = 0; y < face_size; ++y)
		{
			for (uint32_t x = 0; x < face_size; ++x)
			{
				glm::vec3 c = glm::vec3(faces[face][size_t(y) * face_size + x]);
				glm::u8vec4 rgbe = linear_to_rgbe(c);

				uint32_t dst_x = x;
				uint32_t dst_y = face * face_size + y;

				size_t idx = 4 * (size_t(dst_y) * size_t(w) + size_t(dst_x));
				pixels[idx + 0] = rgbe.r;
				pixels[idx + 1] = rgbe.g;
				pixels[idx + 2] = rgbe.b;
				pixels[idx + 3] = rgbe.a;
			}
		}
	}

	if (!stbi_write_png(filename.c_str(), w, h, 4, pixels.data(), w * 4))
	{
		throw std::runtime_error("Failed to write cubemap PNG: " + filename);
	}
}

static VkDescriptorSet make_ibl_descriptor(
	RTG &rtg,
	VkDescriptorPool pool,
	VkDescriptorSetLayout layout,
	VkSampler env_sampler,
	VkImageView env_cube_view,
	VkImageView out_storage_view)
{
	VkDescriptorSet set = VK_NULL_HANDLE;

	VkDescriptorSetAllocateInfo alloc_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};
	VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &set));

	VkDescriptorImageInfo env_info{
		.sampler = env_sampler,
		.imageView = env_cube_view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkDescriptorImageInfo out_info{
		.sampler = VK_NULL_HANDLE,
		.imageView = out_storage_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	std::array<VkWriteDescriptorSet, 2> writes{
		VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &env_info,
		},
		VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = set,
			.dstBinding = 1,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &out_info,
		},
	};

	vkUpdateDescriptorSets(rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
	return set;
}

static void generate_cubemap_mips(
	RTG &rtg,
	Helpers::AllocatedImage const &image,
	uint32_t base_size,
	uint32_t mip_count)
{
	if (mip_count == 0)
	{
		throw std::runtime_error("generate_cubemap_mips called with mip_count == 0");
	}

	std::cout << "about to reset cubemipcommand buffer: " << (void *)rtg.helpers.transfer_command_buffer << std::endl;
	VK(vkResetCommandBuffer(rtg.helpers.transfer_command_buffer, 0));

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK(vkBeginCommandBuffer(rtg.helpers.transfer_command_buffer, &begin_info));

	VkCommandBuffer cmd = rtg.helpers.transfer_command_buffer;

	// If there is only one mip, just transition mip 0 to SHADER_READ_ONLY_OPTIMAL.
	if (mip_count == 1)
	{
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image.handle,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 6,
			},
		};

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		VK(vkEndCommandBuffer(cmd));

		VkSubmitInfo submit_info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd,
		};

		VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
		VK(vkQueueWaitIdle(rtg.graphics_queue));
		return;
	}

	// Transition mip 0: TRANSFER_DST -> TRANSFER_SRC
	{
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image.handle,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 6,
			},
		};

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier);
	}

	uint32_t src_size = base_size;

	for (uint32_t mip = 1; mip < mip_count; ++mip)
	{
		uint32_t dst_size = std::max(1u, src_size >> 1);

		// Transition this mip: UNDEFINED -> TRANSFER_DST
		{
			VkImageMemoryBarrier barrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = image.handle,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = mip,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 6,
				},
			};

			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);
		}

		// Blit each face from mip-1 -> mip
		for (uint32_t face = 0; face < 6; ++face)
		{
			VkImageBlit blit{
				.srcSubresource{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = mip - 1,
					.baseArrayLayer = face,
					.layerCount = 1,
				},
				.srcOffsets{
					{0, 0, 0},
					{int32_t(src_size), int32_t(src_size), 1}},
				.dstSubresource{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = mip,
					.baseArrayLayer = face,
					.layerCount = 1,
				},
				.dstOffsets{
					{0, 0, 0},
					{int32_t(dst_size), int32_t(dst_size), 1}},
			};

			vkCmdBlitImage(
				cmd,
				image.handle,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image.handle,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&blit,
				VK_FILTER_LINEAR);
		}

		// Transition this mip so it can become the source for the next loop iteration.
		{
			VkImageMemoryBarrier barrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = image.handle,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = mip,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 6,
				},
			};

			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);
		}

		src_size = dst_size;
	}

	// Transition all mips to SHADER_READ_ONLY_OPTIMAL for samplerCube usage.
	{
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image.handle,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = mip_count,
				.baseArrayLayer = 0,
				.layerCount = 6,
			},
		};

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier);
	}

	VK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
	VK(vkQueueWaitIdle(rtg.graphics_queue));
}
static std::vector<glm::vec4> readback_cubemap_face(
	RTG &rtg,
	Helpers::AllocatedImage const &image,
	uint32_t face,
	uint32_t mip,
	uint32_t face_size)
{
	std::vector<Half4> raw(size_t(face_size) * size_t(face_size));

	Helpers::AllocatedBuffer staging = rtg.helpers.create_buffer(
		raw.size() * sizeof(Half4),
		VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		Helpers::Mapped);

	VK(vkResetCommandBuffer(rtg.helpers.transfer_command_buffer, 0));

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK(vkBeginCommandBuffer(rtg.helpers.transfer_command_buffer, &begin_info));

	VkImageMemoryBarrier to_transfer{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image.handle,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = mip,
			.levelCount = 1,
			.baseArrayLayer = face,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(
		rtg.helpers.transfer_command_buffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &to_transfer);

	VkBufferImageCopy region{
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = mip,
			.baseArrayLayer = face,
			.layerCount = 1,
		},
		.imageOffset{0, 0, 0},
		.imageExtent{face_size, face_size, 1},
	};

	vkCmdCopyImageToBuffer(
		rtg.helpers.transfer_command_buffer,
		image.handle,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		staging.handle,
		1,
		&region);

	VkImageMemoryBarrier back_to_general{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image.handle,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = mip,
			.levelCount = 1,
			.baseArrayLayer = face,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(
		rtg.helpers.transfer_command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &back_to_general);

	VK(vkEndCommandBuffer(rtg.helpers.transfer_command_buffer));

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &rtg.helpers.transfer_command_buffer,
	};

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
	VK(vkQueueWaitIdle(rtg.graphics_queue));

	std::memcpy(raw.data(), staging.allocation.data(), raw.size() * sizeof(Half4));
	rtg.helpers.destroy_buffer(std::move(staging));

	std::vector<glm::vec4> out(raw.size());
	for (size_t i = 0; i < raw.size(); ++i)
	{
		out[i] = unpack_half4(raw[i]);
	}

	return out;
}

int main(int argc, char **argv)
{
	// main wrapped in a try-catch so we can print some debug info about uncaught exceptions:
	try
	{

		// configure application:
		RTG::Configuration configuration;
		configuration.cube = true;

		configuration.application_info = VkApplicationInfo{
			.pApplicationName = "Cube Render",
			.applicationVersion = VK_MAKE_VERSION(0, 0, 0),
			.pEngineName = "Unknown",
			.engineVersion = VK_MAKE_VERSION(0, 0, 0),
			.apiVersion = VK_API_VERSION_1_3};

		bool print_usage = false;

		try
		{
			configuration.parse(argc, argv);
		}
		catch (std::runtime_error &e)
		{
			std::cerr << "Failed to parse arguments:\n"
					  << e.what() << std::endl;
			print_usage = true;
		}

		if (print_usage)
		{
			std::cerr << "Usage:" << std::endl;
			RTG::Configuration::cube_usage([](const char *arg, const char *desc)
										   { std::cerr << "    " << arg << "\n        " << desc << std::endl; });
			return 1;
		}

		configuration.headless = true;

		// loads vulkan library, creates surface, initializes helpers:
		RTG rtg(configuration);

		// TODO: create computation pipeline
		CubePipeline pipeline;
		pipeline.create(rtg);

		// TODO: load images / create descriptors

		VkCommandPool cube_command_pool = VK_NULL_HANDLE;
		VkCommandBuffer cube_command_buffer = VK_NULL_HANDLE;

		{
			VkCommandPoolCreateInfo pool_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
				.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
				.queueFamilyIndex = rtg.graphics_queue_family.value(),
			};
			VK(vkCreateCommandPool(rtg.device, &pool_info, nullptr, &cube_command_pool));
		}

		{
			VkCommandBufferAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = cube_command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &cube_command_buffer));
		}

		CPUCubeMap input_cube = load_rgbe_cubemap_vertical(rtg.configuration.in_image);

		uint32_t in_size = input_cube.size;
		uint32_t env_mip_count = 1u + uint32_t(std::floor(std::log2(float(in_size))));

		GPUCubeMap env_gpu;
		env_gpu.image = rtg.helpers.create_cubemap(
			{in_size, in_size},
			VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped,
			env_mip_count);

		for (uint32_t face = 0; face < 6; ++face)
		{
			std::vector<glm::vec4> face_rgba32(size_t(in_size) * size_t(in_size));

			uint32_t bad_count = 0;

			for (size_t i = 0; i < face_rgba32.size(); ++i)
			{
				glm::vec3 c = input_cube.faces[face][i];

				for (int k = 0; k < 3; ++k)
				{
					if (!std::isfinite(c[k]))
					{
						c[k] = 0.0f;
						bad_count++;
					}

					c[k] = std::max(c[k], 0.0f);
				}

				face_rgba32[i] = glm::vec4(c, 1.0f);
			}

			std::cout << "[env upload R32] face " << face
					  << " bytes = " << face_rgba32.size() * sizeof(glm::vec4)
					  << " bad = " << bad_count << std::endl;

			rtg.helpers.transfer_to_cubemap_layer(
				face_rgba32.data(),
				face_rgba32.size() * sizeof(glm::vec4),
				env_gpu.image,
				face,
				0,
				VK_IMAGE_LAYOUT_UNDEFINED);
		}

		generate_cubemap_mips(rtg, env_gpu.image, in_size, env_mip_count);

		env_gpu.cube_view = make_cube_view(rtg, env_gpu.image, env_mip_count);
		// env_gpu.sampled_array_view = make_sampled_array_view(rtg, env_gpu.image, env_mip_count);
		env_gpu.sampler = make_cube_sampler(rtg, env_mip_count);
		bool do_lambert = !rtg.configuration.lambert_out_image.empty();
		bool do_ggx = !rtg.configuration.ggx_out_image.empty();

		uint32_t lambert_size = 64;
		uint32_t ggx_levels = 0;
		uint32_t ggx_base_size = 0;

		if (do_ggx)
		{
			ggx_base_size = std::max(1u, in_size);
			uint32_t max_ggx_levels = 1u + uint32_t(std::floor(std::log2(float(ggx_base_size))));
			ggx_levels = std::min<uint32_t>(rtg.configuration.ggx_levels, max_ggx_levels);
		}

		GPUCubeMap irradiance_gpu{};
		GPUCubeMap ggx_gpu{};

		// create output cubemaps first:
		if (do_lambert)
		{
			irradiance_gpu.image = rtg.helpers.create_cubemap(
				{lambert_size, lambert_size},
				VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_STORAGE_BIT |
					VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped,
				1);

			irradiance_gpu.storage_views.push_back(
				make_storage_view_for_mip(rtg, irradiance_gpu.image, 0));
			irradiance_gpu.cube_view = make_cube_view(rtg, irradiance_gpu.image, 1);
			irradiance_gpu.sampler = make_cube_sampler(rtg, 1);
		}

		if (do_ggx)
		{
			ggx_gpu.image = rtg.helpers.create_cubemap(
				{ggx_base_size, ggx_base_size},
				VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_STORAGE_BIT |
					VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped,
				ggx_levels);

			for (uint32_t mip = 0; mip < ggx_levels; ++mip)
			{
				ggx_gpu.storage_views.push_back(
					make_storage_view_for_mip(rtg, ggx_gpu.image, mip));
			}

			ggx_gpu.cube_view = make_cube_view(rtg, ggx_gpu.image, ggx_levels);
			ggx_gpu.sampler = make_cube_sampler(rtg, ggx_levels);
		}

		// descriptor pool sized for actual usage:
		uint32_t descriptor_set_count = 0;
		if (do_lambert)
			descriptor_set_count += 1;
		if (do_ggx)
			descriptor_set_count += ggx_levels;

		VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
		if (descriptor_set_count > 0)
		{
			std::array<VkDescriptorPoolSize, 2> pool_sizes{
				VkDescriptorPoolSize{
					.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.descriptorCount = descriptor_set_count,
				},
				VkDescriptorPoolSize{
					.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.descriptorCount = descriptor_set_count,
				},
			};

			VkDescriptorPoolCreateInfo pool_ci{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.maxSets = descriptor_set_count,
				.poolSizeCount = uint32_t(pool_sizes.size()),
				.pPoolSizes = pool_sizes.data(),
			};

			VK(vkCreateDescriptorPool(rtg.device, &pool_ci, nullptr, &descriptor_pool));
		}

		// record compute work:
		std::cout << "about to record comput work  buffer: " << (void *)rtg.helpers.transfer_command_buffer << std::endl;
		VK(vkResetCommandBuffer(cube_command_buffer, 0));

		VkCommandBufferBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		VK(vkBeginCommandBuffer(cube_command_buffer, &begin_info));

		VkCommandBuffer cmd = cube_command_buffer;

		// transition storage outputs from UNDEFINED -> GENERAL before compute:
		auto transition_output_to_general = [&](Helpers::AllocatedImage const &image, uint32_t mip_count)
		{
			VkImageMemoryBarrier barrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = image.handle,
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = mip_count,
					.baseArrayLayer = 0,
					.layerCount = 6,
				},
			};

			vkCmdPipelineBarrier(
				cmd,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier);
		};

		if (do_lambert)
		{
			transition_output_to_general(irradiance_gpu.image, 1);
		}
		if (do_ggx)
		{
			transition_output_to_general(ggx_gpu.image, ggx_levels);
		}
		{

			std::unique_ptr<Timer> timer;
			timer.reset(new Timer([](double dt)
								  { std::cout << "REPORT cube-compute-dispatch-" << dt * 1000.0 << "ms" << std::endl; }));
			std::cout << "this is the size: " << in_size << std::endl;

			// Lambertian dispatch:
			if (do_lambert)
			{
				VkDescriptorSet irradiance_set = make_ibl_descriptor(
					rtg,
					descriptor_pool,
					pipeline.set0_env,
					env_gpu.sampler,
					env_gpu.cube_view,
					irradiance_gpu.storage_views[0]);

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.irradiance_pipeline);
				vkCmdBindDescriptorSets(
					cmd,
					VK_PIPELINE_BIND_POINT_COMPUTE,
					pipeline.irradiance_layout,
					0,
					1,
					&irradiance_set,
					0,
					nullptr);

				CubePipeline::IrradiancePush ipush{
					.size = lambert_size,
					.numSamples = 200000,
				};

				vkCmdPushConstants(
					cmd,
					pipeline.irradiance_layout,
					VK_SHADER_STAGE_COMPUTE_BIT,
					0,
					sizeof(ipush),
					&ipush);

				vkCmdDispatch(
					cmd,
					(lambert_size + 7) / 8,
					(lambert_size + 7) / 8,
					6);
			}

			// GGX dispatch:
			if (do_ggx)
			{
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.specular_pipeline);

				for (uint32_t mip = 0; mip < ggx_levels; ++mip)
				{
					uint32_t mip_size = std::max(1u, ggx_base_size >> mip);
					float roughness = (ggx_levels <= 1)
										  ? 0.0f
										  : float(mip) / float(ggx_levels - 1);

					VkDescriptorSet spec_set = make_ibl_descriptor(
						rtg,
						descriptor_pool,
						pipeline.set0_env,
						env_gpu.sampler,
						env_gpu.cube_view,
						ggx_gpu.storage_views[mip]);

					vkCmdBindDescriptorSets(
						cmd,
						VK_PIPELINE_BIND_POINT_COMPUTE,
						pipeline.specular_layout,
						0,
						1,
						&spec_set,
						0,
						nullptr);

					CubePipeline::SpecularPush spush{
						.size = mip_size,
						.numSamples = 190000,
						.roughness = roughness,
						.pad = 0.0f,
					};

					vkCmdPushConstants(
						cmd,
						pipeline.specular_layout,
						VK_SHADER_STAGE_COMPUTE_BIT,
						0,
						sizeof(spush),
						&spush);

					vkCmdDispatch(
						cmd,
						(mip_size + 7) / 8,
						(mip_size + 7) / 8,
						6);
				}
			}

			VK(vkEndCommandBuffer(cube_command_buffer));

			{
				VkSubmitInfo submit_info{
					.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
					.commandBufferCount = 1,
					.pCommandBuffers = &cube_command_buffer,
				};

				VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
				VK(vkQueueWaitIdle(rtg.graphics_queue));
			}

			std::cout << "Computing: done." << std::endl;
		}

		// read back + save Lambertian:
		if (do_lambert)
		{
			std::unique_ptr<Timer> timer;
			timer.reset(new Timer([](double dt)
								  { std::cout << "REPORT cube-lambert-readback-write " << dt * 1000.0 << "ms" << std::endl; }));

			std::array<std::vector<glm::vec4>, 6> result_faces;
			for (uint32_t face = 0; face < 6; ++face)
			{
				result_faces[face] = readback_cubemap_face(
					rtg, irradiance_gpu.image, face, 0, lambert_size);
			}

			write_rgbe_cubemap_vertical(
				rtg.configuration.lambert_out_image,
				result_faces,
				lambert_size);
		}

		// read back + save GGX:
		if (do_ggx)
		{
			std::unique_ptr<Timer> timer;
			timer.reset(new Timer([](double dt)
								  { std::cout << "REPORT cube-ggx-readback-write " << dt * 1000.0 << "ms" << std::endl; }));

			auto split_ext = [](std::string const &path)
			{
				size_t dot = path.find_last_of('.');
				if (dot == std::string::npos)
					return std::pair(path, std::string{});
				return std::pair(path.substr(0, dot), path.substr(dot));
			};

			auto [ggx_base, ggx_ext] = split_ext(rtg.configuration.ggx_out_image);

			for (uint32_t mip = 1; mip < ggx_levels; ++mip)
			{
				uint32_t mip_size = std::max(1u, ggx_base_size >> mip);

				std::array<std::vector<glm::vec4>, 6> faces;
				for (uint32_t face = 0; face < 6; ++face)
				{
					faces[face] = readback_cubemap_face(
						rtg, ggx_gpu.image, face, mip, mip_size);
				}

				std::string out_name = ggx_base + "." + std::to_string(mip) + ggx_ext;
				write_rgbe_cubemap_vertical(out_name, faces, mip_size);
			}
		}

		irradiance_gpu.destroy(rtg);
		ggx_gpu.destroy(rtg);
		env_gpu.destroy(rtg);

		if (descriptor_pool)
		{
			vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
			descriptor_pool = VK_NULL_HANDLE;
		}

		vkDestroyCommandPool(rtg.device, cube_command_pool, nullptr);
		cube_command_pool = VK_NULL_HANDLE;

		pipeline.destroy(rtg);
	}
	catch (std::exception &e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
		return 1;
	}
}
