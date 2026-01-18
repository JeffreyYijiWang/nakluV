#include "Tutorial.hpp"

#include "VK.hpp"
#include "refsol.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);
	}
}

Tutorial::~Tutorial() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);

		if (workspace.lines_vertices_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
		}
		if (workspace.lines_vertices.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
		}
	}
	workspaces.clear();

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);

	refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
}

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	//[re]create framebuffers:
	refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}

void Tutorial::destroy_framebuffers() {
	refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}


void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params) {
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

			VkClearValue{.color{.float32{1.0f, 0.73f, 0.23f, 0.2f } } },
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
		{ //set scissor rectangle
			VkRect2D scissor{
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
		}
		{
			VkViewport viewport{
				.x = 0.0f,
				.y = 0.0f,
				.width = float(rtg.swapchain_extent.width),
				.height = float(rtg.swapchain_extent.height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};
			vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
		}

		{//draw with the backgorun pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);
			
			{//push time:
				BackgroundPipeline::Push push{
					.time = float(time),
				};
				vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
			}
			vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		}

		{//draw with the lines pipeline;
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);

			{//use lines_vertice (offset 0) as vertex buffer bindign 0:
				std::array < VkBuffer, 1> vertex_buffers{ workspace.lines_vertices.handle };
				std::array <VkDeviceSize, 1> offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
		
			}

			//draw line vertice
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}


		
		vkCmdEndRenderPass(workspace.command_buffer);
	
	}

	//end recoding
	VK(vkEndCommandBuffer(workspace.command_buffer) );

	//submit `workspace.command buffer` for the GPU to run:
	refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
}


void Tutorial::update(float dt) {
	time = std::fmod(time + dt, 60.0f);

	//make ann 'x';
	/*lines_vertices.clear();
	lines_vertices.reserve(4);
	lines_vertices.emplace_back(PosColVertex{
		.Position{.x = -1.0f, .y = -1.0f, .z = 0.0f },
		.Color{ .r = 0xff, .g = 0xff, .b= 0xff, .a = 0xff }
	});
	lines_vertices.emplace_back(PosColVertex{
		.Position{.x = 1.0f, .y = 1.0f, .z = 0.0f },
		.Color{.r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff }
	});
	lines_vertices.emplace_back(PosColVertex{
		.Position{.x = -1.0f, .y = 1.0f, .z = 0.0f },
		.Color{.r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff }
	});
	lines_vertices.emplace_back(PosColVertex{
		.Position{.x = 1.0f, .y = -1.0f, .z = 0.0f },
		.Color{.r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff }
	});
	assert(lines_vertices.size() == 4);*/

	{//make some crossing lines at differnt depths:
		lines_vertices.clear();
		constexpr size_t count = 2 * 30 + 2 * 30;
		lines_vertices.reserve(count);

		//hoirizontal lines at z = 0.5 :
		for (uint32_t i = 0; i < 30;++i) {
			float y = (i + 0.5f)/ 30.0f * 2.0f - 1.0f;
			lines_vertices.emplace_back(PosColVertex{
				.Position{ .x = -1.0f, .y = y, .z = 0.5f},
				.Color {.r = 0xff, .g= 0xff, .b = 0x00, .a  = 0xff},
				});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = 1.0f, .y = y, .z = 0.5f},
				.Color {.r = 0xff, .g = 0xff, .b = 0x00, .a = 0xff},
				});

		}

		//vetical lines at z = 0.0 (near) through 1.0 far:
		for (uint32_t i = 0; i < 30;++i) {
			float x = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
			float z = (i + 0.5f) / 30.0f;
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = -1.0f, .z = z},
				.Color {.r = 0x44, .g = 0x00, .b = 0x00, .a = 0xff},
				});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = 1.0f, .z = z},
				.Color {.r = 0x44, .g = 0x00, .b = 0x00, .a = 0xff},
				});

		}
		assert(lines_vertices.size() == count);

	}
}


void Tutorial::on_input(InputEvent const &) {
}
