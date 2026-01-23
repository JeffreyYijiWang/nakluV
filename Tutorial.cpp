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
	objects_pipeline.create(rtg, render_pass, 0);

	{//create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); //for easier to read counting

		std::array < VkDescriptorPoolSize, 1> pool_sizes{
			//we only need uniform buffer descriptor for the momenbt:
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1 * per_workspace, //one descriptor per set, one set per workspace
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, // because CCREATE_FREE_DESCRIPTOR_SET_BIT isin;t include , we can't free individual descript allocated for this pool
			.maxSets = 1 * per_workspace, //one set per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
	}

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);

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

		{// allocated descriptor set for Cmaer descirpt 
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lines_pipeline.set0_Camera,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Camera_descriptors));
		}
		
		//todo: descruotir write
		{// point descript to Camera buffer:
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			std::array < VkWriteDescriptorSet, 1> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
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
		std::vector < PosNorTexVertex > vertices;

		{ // A [-1,1] x [-1, 1 x {0} quadrilater:
			plane_vertices.first = uint32_t(vertices.size());
			vertices.emplace_back(PosNorTexVertex{
				.Position{.x = -1.0f, .y = -1.0f, .z = 0.0f},
				.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 0.0f, .t = 0.0f },
				});

			vertices.emplace_back(PosNorTexVertex{
				.Position{.x =1.0f, .y = -1.0f, .z = 0.0f},
				.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 1.0f, .t = 0.0f },
				});

			vertices.emplace_back(PosNorTexVertex{
				.Position{.x = -11.0f, .y = 1.0f, .z = 0.0f},
				.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 0.0f, .t = 1.0f },
				});
			vertices.emplace_back(PosNorTexVertex{
				.Position{.x = 1.0f, .y = 1.0f, .z = 0.0f},
				.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 1.0f, .t = 1.0f },
				});
			vertices.emplace_back(PosNorTexVertex{
				.Position{.x = -1.0f, .y = -1.0f, .z = 0.0f},
				.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 0.0f, .t = 1.0f },
				});
			vertices.emplace_back(PosNorTexVertex{
				.Position{.x = 1.0f, .y = -1.0f, .z = 0.0f},
				.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 1.0f, .t = 0.0f },
				});

			plane_vertices.count = uint32_t(vertices.size()) - plane_vertices.first;
		}

		{//a torus 
			torus_vertices.first = uint32_t(vertices.size());

			// will parmeteriz with (u,v ) where;
			// -u is angle aroudn the main axis (+z)
			// - v is angle aroudn the tube

			constexpr float R1 = 0.75f; // main radius
			constexpr float R2 = 0.15f; // tube raidu

			constexpr uint32_t U_STEPS = 20;
			constexpr uint32_t V_STEPS = 16; 

			//texture repreats aroudn the torus:
			constexpr float V_REPEATS = 2.0f;
			constexpr float U_REPEATS = int(V_REPEATS / R2 * R1 + 0.999f);

			//approcimaty square , rounded up

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				//convert steps to angles:
				// doing the mod since trig on 2M_PI may not exacty match 0
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.0f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.0f * float(M_PI);

				vertices.emplace_back(PosNorTexVertex{
					.Position{
						.x = (R1 + R2 * std::cos(va)) * std::cos(ua),
						.y = (R1 + R2 * std::cos(va)) * std::sin(ua),
						.z = R2 * std::sin(va),
					},
					.Normal {
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord{
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					emplace_vertex(ui, vi);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui, vi+1);

					emplace_vertex(ui, vi+1);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui+1, vi+1);
				}
			}
			torus_vertices.count = uint32_t(vertices.size()) - torus_vertices.first;

		}
		
		//A single traingle:

		vertices.emplace_back(PosNorTexVertex{
			.Position{.x = 0.0f, .y = 0.0f, .z = 0.0f},
			.Normal{ .x = 0.0f, .y = 0.0f, .z = 1.0f},
			.TexCoord{.s = 0.0f, .t = 0.0f},
			});

		vertices.emplace_back(PosNorTexVertex{
			.Position{.x = 1.0f, .y = 0.0f, .z = 0.0f},
			.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
			.TexCoord{.s = 1.0f, .t = 0.0f},
			});

		vertices.emplace_back(PosNorTexVertex{
			.Position{.x = 0.0f, .y = 1.0f, .z = 0.0f},
			.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
			.TexCoord{.s = 0.0f, .t = 1.0f},
		});

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

}

Tutorial::~Tutorial() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	rtg.helpers.destroy_buffer(std::move(object_vertices));

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
		if (workspace.Camera_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
		}
		if (workspace.Camera.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}
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

		{//draw with the object pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);
			
			{//use object vertices (offset 0 ) as vertex buffer binging 0:
				
				std::array < VkBuffer, 1 > vertex_buffers{ object_vertices.handle };
				std::array < VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

			}

			 //camera descriptor set is stil bounde (!)

			//draw all vertices :
			vkCmdDraw(workspace.command_buffer, uint32_t(object_vertices.size / sizeof(ObjectsPipeline::Vertex)), 1, 0, 0);
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

	{//ROTATING =CAMER THE ORIGIN	
		float ang = float(M_PI) * 2.0f * 10.0f * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
			60.0f * float(M_PI) / 180.0f,//vfov
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
			0.1f, //new
			1000.0f //far
		) * look_at(
			3.0f * std::cos(ang), 3.0f * std::sin(ang), 1.0f, //eye
			0.0f, 0.0f, 0.5f, //target
			0.0f, 0.0f, 1.0f //up
		);

	}

	////https://mathworld.wolfram.com/Helicoid.html
	//{ //helixoid 
	//	lines_vertices.clear();

	//	//tessellation:
	//	constexpr uint32_t U_STEPS = 70;   //radial samples
	//	constexpr uint32_t V_STEPS = 80;  //angular samples
	//	constexpr float U_MAX = 1.0f;

	//	//how many turns:
	//	constexpr float TURNS = 2.3f;
	//	const float v_min = 0.0f;
	//	const float v_max = 2.0f * float(M_PI) * TURNS;

	//	//pitch control
	//	constexpr float c = 0.90f;
	//	const float z_min = c * v_min;
	//	const float z_max = c * v_max;
	//	const float inv_z_range = 1.0f / (z_max - z_min);

	//	
	//	const size_t v_dir_segments = size_t(U_STEPS + 1) * size_t(V_STEPS);
	//	const size_t u_dir_segments = size_t(V_STEPS + 1) * size_t(U_STEPS);
	//	const size_t total_vertices = 2 * (v_dir_segments + u_dir_segments);
	//	lines_vertices.reserve(total_vertices);

	//	//smooth fade
	//	auto smoothstep = [](float e0, float e1, float x) {
	//		x = (x - e0) / (e1 - e0);
	//		if (x < 0.0f) x = 0.0f;
	//		if (x > 1.0f) x = 1.0f;
	//		return x * x * (3.0f - 2.0f * x);
	//		};

	//	auto shade_to_black_floor = [](uint8_t c, float k, float floor_k) -> uint8_t {
	//		float kk = floor_k + (1.0f - floor_k) * k;
	//		float cf = float(c) * kk;
	//		if (cf < 0.0f) cf = 0.0f;
	//		if (cf > 255.0f) cf = 255.0f;
	//		return uint8_t(cf);
	//		};


	//	auto push_line = [&](float ax, float ay, float az,
	//		float bx, float by, float bz,
	//		uint8_t r, uint8_t g, uint8_t b, uint8_t a) {

	//			lines_vertices.emplace_back(PosColVertex{
	//				.Position{.x = ax, .y = ay, .z = az },
	//				.Color{.r = r, .g = g, .b = b, .a = a },
	//				});
	//			lines_vertices.emplace_back(PosColVertex{
	//				.Position{.x = bx, .y = by, .z = bz },
	//				.Color{.r = r, .g = g, .b = b, .a = a },
	//				});
	//		};

	//	//v-direction lines 
	//	for (uint32_t iu = 0; iu <= U_STEPS; ++iu) {
	//		const float u = (float(iu) / float(U_STEPS)) * U_MAX;

	//		//simple color cue by radius:
	//		const float u01 = (U_MAX > 0.0f) ? (u / U_MAX) : 0.0f;
	//		const uint8_t cr = uint8_t(0x40 + (0xBF * (1.0f - u01)));
	//		const uint8_t cg = 0x80;
	//		const uint8_t cb = 0xFF;

	//		for (uint32_t iv = 0; iv < V_STEPS; ++iv) {
	//			const float t0 = float(iv) / float(V_STEPS);
	//			const float t1 = float(iv + 1) / float(V_STEPS);
	//			const float v0 = v_min + (v_max - v_min) * t0;
	//			const float v1 = v_min + (v_max - v_min) * t1;

	//			//p0:
	//			const float x0 = 1.0f * u * std::cos(v0);
	//			const float y0 = 1.0f * u * std::sin(v0);
	//			const float z0 = (c * v0 - z_min) * inv_z_range; 

	//			//p1:
	//			const float x1 = 1.0f * u * std::cos(v1);
	//			const float y1 = 1.0f * u * std::sin(v1);
	//			const float z1 = (c * v1 - z_min) * inv_z_range; 

	//			float v_mid = 0.5f * (v0 + v1);
	//			float z_mid01 = (c * v_mid - z_min) * inv_z_range; 

	//	
	//			float t2 = z_mid01;
	//			const float edge = 0.25f;
	//			float fade_in = smoothstep(0.0f, edge, t2);
	//			float fade_out = 1.0f - smoothstep(1.0f - edge, 1.0f, t2);
	//			float fade = fade_in * fade_out; // 0..1


	//			const float floor_k = 0.10f; 
	//			uint8_t r2 = shade_to_black_floor(cr, fade, floor_k);
	//			uint8_t g2 = shade_to_black_floor(cg, fade, floor_k);
	//			uint8_t b2 = shade_to_black_floor(cb, fade, floor_k);

	//			push_line(x0, y0, z0, x1, y1, z1, r2, g2, b2, 0xFF);

	//		}
	//	}

	//	
	//	for (uint32_t iv = 0; iv <= V_STEPS; ++iv) {
	//		const float t = float(iv) / float(V_STEPS);
	//		const float v = v_min + (v_max - v_min) * t;

	//		const float wave_speed = 2.0f; // how fast 
	//		const float wave_freq = 3.0f;   // number of ripples 
	//		const float wave_amp = 0.20f;  // radial modulation strength

	//		const float wave = 1.0f + wave_amp * std::sin(wave_freq * v - wave_speed * time);

	//		const uint8_t cr = 0xFF;
	//		const uint8_t cg = uint8_t(0x60 + 0x80 * std::sin(v));
	//		const uint8_t cb = 0x20;

	//		for (uint32_t iu = 0; iu < U_STEPS; ++iu) {
	//			const float u0 = (float(iu) / float(U_STEPS)) * U_MAX;
	//			const float u1 = (float(iu + 1) / float(U_STEPS)) * U_MAX;

	//			//p0: red-yelo lines coming out of the cone 

	//			const float r0 = 1.4f * u0 * wave;
	//			const float x0 = r0 * std::cos(v);
	//			const float y0 = r0 * std::sin(v);


	//			const float z0 = (c * v - z_min) * inv_z_range;

	//			//p1:
	//			const float x1 = r0 * u1 * std::cos(v);
	//			const float y1 = r0 * u1 * std::sin(v);
	//			const float z1 = (c * v - z_min) * inv_z_range;

	//			float t_fade = z0;
	//			const float edge1 = 0.15f;
	//			float fade_in = smoothstep(0.0f, edge1, t_fade);
	//			float fade_out = 1.0f - smoothstep(1.0f - edge1, 1.0f, t_fade);
	//			float fade = fade_in * fade_out;

	//			const float floor_k = 0.25f;
	//			uint8_t r2 = shade_to_black_floor(cr, fade, floor_k);
	//			uint8_t g2 = shade_to_black_floor(cg, fade, floor_k);
	//			uint8_t b2 = shade_to_black_floor(cb, fade, floor_k);

	//			push_line(x0, y0, z0, x1, y1, z1, r2, g2, b2, 0xFF);
	//		}
	//	}

	//	assert(lines_vertices.size() == total_vertices);
	//}

	{//make some crossing lines at differnt depths:
		//lines_vertices.clear();
		//constexpr size_t count = 2 * 30 + 2 * 30;
		//lines_vertices.reserve(count);

		////hoirizontal lines at z = 0.5 :
		//for (uint32_t i = 0; i < 30;++i) {
		//	float y = (i + 0.5f)/ 30.0f * 2.0f - 1.0f;
		//	lines_vertices.emplace_back(PosColVertex{
		//		.Position{ .x = -1.0f, .y = y, .z = 0.5f},
		//		.Color {.r = 0xff, .g= 0xff, .b = 0x00, .a  = 0xff},
		//		});
		//	lines_vertices.emplace_back(PosColVertex{
		//		.Position{.x = 1.0f, .y = y, .z = 0.5f},
		//		.Color {.r = 0xff, .g = 0xff, .b = 0x00, .a = 0xff},
		//		});

		//}

		////vetical lines at z = 0.0 (near) through 1.0 far:
		//for (uint32_t i = 0; i < 30;++i) {
		//	float x = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
		//	float z = (i + 0.5f) / 30.0f;
		//	lines_vertices.emplace_back(PosColVertex{
		//		.Position{.x = x, .y = -1.0f, .z = z},
		//		.Color {.r = 0x44, .g = 0x00, .b = 0x00, .a = 0xff},
		//		});
		//	lines_vertices.emplace_back(PosColVertex{
		//		.Position{.x = x, .y = 1.0f, .z = z},
		//		.Color {.r = 0x44, .g = 0x00, .b = 0x00, .a = 0xff},
		//		});

		//}
		//assert(lines_vertices.size() == count);

	}
	

}


void Tutorial::on_input(InputEvent const &) {
}
