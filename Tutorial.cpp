#include "Tutorial.hpp"

#include "VK.hpp"
#include "refsol.hpp"
	

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <algorithm>

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{//create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); //for easier to read counting

		std::array < VkDescriptorPoolSize, 2> pool_sizes{

			VkDescriptorPoolSize{ //union buffer descirpts
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
		//allcat for world

		workspace.World_src =rtg.helpers.create_buffer(
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

			//not we iwll fin this descirpt set in tehredn when buefer or re-allocated;
		}
		
		//todo: descruotir write
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
		std::vector < PosNorTexVertex > vertices;

		
		//{ // A [-1,1] x [-1, 1 x {0} quadrilater:
		//	plane_vertices.first = uint32_t(vertices.size());
		//	vertices.emplace_back(PosNorTexVertex{
		//		.Position{.x = -1.0f, .y = -1.0f, .z = 0.0f},
		//		.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
		//		.TexCoord{.s = 0.0f, .t = 0.0f },
		//		});

		//	vertices.emplace_back(PosNorTexVertex{
		//		.Position{.x =1.0f, .y = -1.0f, .z = 0.0f},
		//		.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
		//		.TexCoord{.s = 1.0f, .t = 0.0f },
		//		});

		//	vertices.emplace_back(PosNorTexVertex{
		//		.Position{.x = -1.0f, .y = 1.0f, .z = 0.0f},
		//		.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
		//		.TexCoord{.s = 0.0f, .t = 1.0f },
		//		});
		//	vertices.emplace_back(PosNorTexVertex{
		//		.Position{.x = 1.0f, .y = 1.0f, .z = 0.0f},
		//		.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
		//		.TexCoord{.s = 1.0f, .t = 1.0f },
		//		});
		//	vertices.emplace_back(PosNorTexVertex{
		//		.Position{.x = -1.0f, .y = 1.0f, .z = 0.0f},
		//		.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
		//		.TexCoord{.s = 0.0f, .t = 1.0f },
		//		});
		//	vertices.emplace_back(PosNorTexVertex{
		//		.Position{.x = 1.0f, .y = -1.0f, .z = 0.0f},
		//		.Normal {.x = 0.0f, .y = 0.0f, .z = 1.0f},
		//		.TexCoord{.s = 1.0f, .t = 0.0f },
		//		});

		//		plane_vertices.count = uint32_t(vertices.size()) - plane_vertices.first;
		//}

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
					emplace_vertex(ui + 1, vi);
					emplace_vertex(ui, vi + 1);

					emplace_vertex(ui, vi + 1);
					emplace_vertex(ui + 1, vi);
					emplace_vertex(ui + 1, vi + 1);
				}
			}
			torus_vertices.count = uint32_t(vertices.size()) - torus_vertices.first;

		}

		{ // wineglass (surface of revolution around +Z, height along +Z) + explicit bottom cap
			// ---------- Genus-2 implicit mesh via marching tetrahedra ----------
// Implicit surface:
// 2y(y^2 - 3x^2)(1 - z^2) + (x^2 + y^2)^2 - (9z^2 - 1)(1 - z^2) = 0

			auto f_genus2 = [](float x, float y, float z) -> float {
				float x2 = x * x, y2 = y * y, z2 = z * z;
				float one_minus_z2 = 1.0f - z2;
				float term1 = 2.0f * y * (y2 - 3.0f * x2) * one_minus_z2;
				float term2 = (x2 + y2) * (x2 + y2);
				float term3 = (9.0f * z2 - 1.0f) * one_minus_z2;
				return term1 + term2 - term3;
				};

			auto grad_genus2 = [&](float x, float y, float z) -> std::array<float, 3> {
				// finite differences (robust, simple)
				float eps = 1e-3f;
				float fx1 = f_genus2(x + eps, y, z);
				float fx0 = f_genus2(x - eps, y, z);
				float fy1 = f_genus2(x, y + eps, z);
				float fy0 = f_genus2(x, y - eps, z);
				float fz1 = f_genus2(x, y, z + eps);
				float fz0 = f_genus2(x, y, z - eps);
				return { (fx1 - fx0) / (2.0f * eps),
						 (fy1 - fy0) / (2.0f * eps),
						 (fz1 - fz0) / (2.0f * eps) };
				};

			auto normalize3 = [](std::array<float, 3> v) -> std::array<float, 3> {
				float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
				if (len < 1e-8f) return { 0.0f, 0.0f, 1.0f };
				return { v[0] / len, v[1] / len, v[2] / len };
				};

			auto lerp3 = [](std::array<float, 3> a, std::array<float, 3> b, float t) -> std::array<float, 3> {
				return { a[0] + t * (b[0] - a[0]),
						 a[1] + t * (b[1] - a[1]),
						 a[2] + t * (b[2] - a[2]) };
				};

			auto interp_zero = [&](std::array<float, 3> p0, std::array<float, 3> p1, float v0, float v1) -> std::array<float, 3> {
				// assumes v0 and v1 have opposite signs (or one is 0)
				float t = (std::abs(v1 - v0) < 1e-8f) ? 0.5f : (0.0f - v0) / (v1 - v0);
				t = std::max(0.0f, std::min(1.0f, t));
				return lerp3(p0, p1, t);
				};

			auto push_pnt = [&](std::array<float, 3> p) {
				auto g = normalize3(grad_genus2(p[0], p[1], p[2]));

				// simple UV: wrap around z axis + map z to t
				float s = 0.5f + std::atan2(p[1], p[0]) / (2.0f * float(M_PI));
				float t = 0.5f + 0.5f * p[2]; // if your bounds keep z in [-1,1]

				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = p[0], .y = p[1], .z = p[2] },
					.Normal  {.x = g[0], .y = g[1], .z = g[2] },
					.TexCoord{.s = s,    .t = t    },
					});
				};

			// Marching tetrahedra: split each cube into 6 tetrahedra.
			// Cube corner ordering (0..7):
			// 0:(0,0,0) 1:(1,0,0) 2:(1,1,0) 3:(0,1,0)
			// 4:(0,0,1) 5:(1,0,1) 6:(1,1,1) 7:(0,1,1)
			static const int tets[6][4] = {
				{0,1,3,4},
				{1,2,3,6},
				{1,3,4,6},
				{1,4,5,6},
				{3,4,6,7},
				{3,6,2,1} // (duplicate-ish split variant; fine, but you can choose a canonical set)
			};

			// A cleaner canonical 6-tet split (recommended) instead of the last line above:
			static const int tets6[6][4] = {
				{0,1,3,4},
				{1,2,3,6},
				{1,3,4,6},
				{1,4,5,6},
				{3,4,6,7},
				{1,3,6,0}
			};

			auto emit_tet = [&](std::array<float, 3> const p[4], float const val[4]) {
				// classify inside (<0) / outside (>=0)
				int inside[4], outside[4];
				int ni = 0, no = 0;
				for (int i = 0; i < 4; ++i) {
					if (val[i] < 0.0f) inside[ni++] = i;
					else outside[no++] = i;
				}

				if (ni == 0 || ni == 4) return;

				// edges are implicit between every pair; we only use the ones crossing the surface.
				if (ni == 1) {
					int a = inside[0];
					int b = outside[0], c = outside[1], d = outside[2];
					auto p_ab = interp_zero(p[a], p[b], val[a], val[b]);
					auto p_ac = interp_zero(p[a], p[c], val[a], val[c]);
					auto p_ad = interp_zero(p[a], p[d], val[a], val[d]);
					// one triangle
					push_pnt(p_ab); push_pnt(p_ac); push_pnt(p_ad);
				}
				else if (ni == 3) {
					// complement of ni==1: flip winding by swapping
					int a = outside[0];
					int b = inside[0], c = inside[1], d = inside[2];
					auto p_ab = interp_zero(p[a], p[b], val[a], val[b]);
					auto p_ac = interp_zero(p[a], p[c], val[a], val[c]);
					auto p_ad = interp_zero(p[a], p[d], val[a], val[d]);
					// one triangle (reverse order)
					push_pnt(p_ab); push_pnt(p_ad); push_pnt(p_ac);
				}
				else if (ni == 2) {
					int a = inside[0], b = inside[1];
					int c = outside[0], d = outside[1];

					auto p_ac = interp_zero(p[a], p[c], val[a], val[c]);
					auto p_ad = interp_zero(p[a], p[d], val[a], val[d]);
					auto p_bc = interp_zero(p[b], p[c], val[b], val[c]);
					auto p_bd = interp_zero(p[b], p[d], val[b], val[d]);

					// quad split into two triangles
					push_pnt(p_ac); push_pnt(p_ad); push_pnt(p_bd);
					push_pnt(p_ac); push_pnt(p_bd); push_pnt(p_bc);
				}
				};

			// ---- generate mesh in a bounding box ----
			// The surface lives naturally with z in [-1,1] (because of (1 - z^2) factor).
			// x,y extents: start with [-1.5, 1.5]; adjust if clipped.
			genus2_vertices.first = uint32_t(vertices.size());

			const int NX = 64, NY = 64, NZ = 64; // increase for smoother (cost grows quickly)
			const float xmin = -1.5f, xmax = 1.5f;
			const float ymin = -1.5f, ymax = 1.5f;
			const float zmin = -1.0f, zmax = 1.0f;

			auto gridPos = [&](int ix, int iy, int iz) -> std::array<float, 3> {
				float x = xmin + (xmax - xmin) * (float(ix) / float(NX));
				float y = ymin + (ymax - ymin) * (float(iy) / float(NY));
				float z = zmin + (zmax - zmin) * (float(iz) / float(NZ));
				return { x,y,z };
				};

			for (int iz = 0; iz < NZ; ++iz) {
				for (int iy = 0; iy < NY; ++iy) {
					for (int ix = 0; ix < NX; ++ix) {

						std::array<float, 3> P[8] = {
							gridPos(ix,   iy,   iz),
							gridPos(ix + 1, iy,   iz),
							gridPos(ix + 1, iy + 1, iz),
							gridPos(ix,   iy + 1, iz),
							gridPos(ix,   iy,   iz + 1),
							gridPos(ix + 1, iy,   iz + 1),
							gridPos(ix + 1, iy + 1, iz + 1),
							gridPos(ix,   iy + 1, iz + 1),
						};

						float V[8] = {
							f_genus2(P[0][0],P[0][1],P[0][2]),
							f_genus2(P[1][0],P[1][1],P[1][2]),
							f_genus2(P[2][0],P[2][1],P[2][2]),
							f_genus2(P[3][0],P[3][1],P[3][2]),
							f_genus2(P[4][0],P[4][1],P[4][2]),
							f_genus2(P[5][0],P[5][1],P[5][2]),
							f_genus2(P[6][0],P[6][1],P[6][2]),
							f_genus2(P[7][0],P[7][1],P[7][2]),
						};

						// quick reject: all same sign -> no surface in this cube
						int s = 0;
						for (int i = 0; i < 8; ++i) s += (V[i] < 0.0f) ? 1 : 0;
						if (s == 0 || s == 8) continue;

						// process 6 tets
						for (int t = 0; t < 6; ++t) {
							int i0 = tets6[t][0], i1 = tets6[t][1], i2 = tets6[t][2], i3 = tets6[t][3];
							std::array<float, 3> tp[4] = { P[i0], P[i1], P[i2], P[i3] };
							float tv[4] = { V[i0], V[i1], V[i2], V[i3] };
							emit_tet(tp, tv);
						}
					}
				}
			}

			genus2_vertices.count = uint32_t(vertices.size()) - genus2_vertices.first;

			// Now you can instance it with:
			// .vertices = genus2_vertices
			// -------------------------------------------------------------------

		}




		{//A single traingle:
			/*
	   vertices.emplace_back(PosNorTexVertex{
		   .Position{.x = 0.0f, .y = 0.0f, .z = 0.0f},
		   .Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f },
		   .TexCoord{.s = 0.0f, .t = 0.0f },
		   });

	   vertices.emplace_back(PosNorTexVertex{
		   .Position{.x = 1.0f, .y = 0.0f, .z = 0.0f},
		   .Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f },
		   .TexCoord{.s = 1.0f, .t = 0.0f },
		   });

	   vertices.emplace_back(PosNorTexVertex{
		   .Position{.x = 0.0f, .y = 1.0f, .z = 0.0f},
		   .Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f },
			 .TexCoord{.s = 0.0f, .t = 1.0f },
	   });
	   */
		}
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

	{/// make some texture
		textures.reserve(2);

		{//textre 0 will be dark grey / light greak check baor with ared square at the origin.
			//actual make the texture: 
			//uint32_t size = 128;
			//std::vector < uint32_t> data;
			//data.reserve(size* size);
			//for (uint32_t y = 0; y < size; ++y) {
			//	float fy = (y + 0.5f) / float(size);
			//	for (uint32_t x = 0; x < size; ++x) {
			//		float fx = (x + 0.5f) / float(size);

			//		//highlight the orgin: 
			//		if (fx < 0.05f && fy < 0.05f) data.emplace_back(0xff0000ff); //red
			//		else if ((fx < 0.5f) == (fy < 0.5f)) data.emplace_back(0xff444444); // dark grey
			//		else  data.emplace_back(0xffbbbbbb); //light grey
			//	}
			//}

			uint32_t size = 128;
			std::vector<uint32_t> data;
			data.reserve(size* size);

			auto pack_rgba = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t {
				// matches your earlier 0xff0000ff style for VK_FORMAT_R8G8B8A8_UNORM on little-endian
				return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
				};

			auto frac = [](float x) -> float { return x - std::floor(x); };

			// rotate UV by ~45 degrees to get diagonal tartan like your reference image
			const float invSize = 1.0f / float(size);
			const float s2 = 0.70710678f; // 1/sqrt(2)

			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					// UV in [0,1)
					float u = (float(x) + 0.5f) * invSize;
					float v = (float(y) + 0.5f) * invSize;

					// center then rotate:
					float cx = u - 0.5f;
					float cy = v - 0.5f;
					float ru = (cx + cy) * s2;   // along one diagonal axis
					float rv = (-cx + cy) * s2;   // along the other diagonal axis
					// shift back to make them positive-ish (not required, but convenient)
					ru += 0.5f;
					rv += 0.5f;

					// Large plaid blocks:
					float U = frac(ru * 3.0f);   // 3 repeats across texture
					float V = frac(rv * 3.0f);

					bool bigU = (U < 0.50f);
					bool bigV = (V < 0.50f);

					// Base colors (roughly matching your example: black/white/greys)
					// We'll choose a dark + light pair and mix by block parity:
					uint8_t r0 = 0, g0 = 0, b0 = 0;         // black
					uint8_t r1 = 0xbb, g1 = 0xbb, b1 = 0xbb; // light grey
					uint8_t r2 = 0x66, g2 = 0x66, b2 = 0x66; // mid grey
					uint8_t r3 = 0xee, g3 = 0xee, b3 = 0xee; // near-white

					// Pick a base block color:
					uint8_t br, bg, bb;
					if (bigU == bigV) { // diagonal “checker” feel
						// alternate between light grey and near-white with a sub-band
						br = (U < 0.25f || U > 0.75f || V < 0.25f || V > 0.75f) ? r3 : r1;
						bg = (U < 0.25f || U > 0.75f || V < 0.25f || V > 0.75f) ? g3 : g1;
						bb = (U < 0.25f || U > 0.75f || V < 0.25f || V > 0.75f) ? b3 : b1;
					}
					else {
						// alternate between black and mid grey
						br = (U < 0.18f || V < 0.18f) ? r0 : r2;
						bg = (U < 0.18f || V < 0.18f) ? g0 : g2;
						bb = (U < 0.18f || V < 0.18f) ? b0 : b2;
					}

					// Weave micro-texture (tiny checker) to mimic fabric:
					// (subtle brightness modulation)
					uint32_t weave = ((x ^ y) & 3u); // 0..3
					float weaveMul = (weave == 0u) ? 0.92f : (weave == 1u ? 0.97f : (weave == 2u ? 1.03f : 1.08f));
					auto mul8 = [&](uint8_t c) -> uint8_t {
						int ic = int(std::round(float(c) * weaveMul));
						if (ic < 0) ic = 0;
						if (ic > 255) ic = 255;
						return uint8_t(ic);
						};
					br = mul8(br); bg = mul8(bg); bb = mul8(bb);

					// Thin red stripes (tartan accent):
					// Put stripes in both rotated axes so they cross.
					auto stripe = [&](float t, float freq, float center, float halfWidth) -> bool {
						float f = frac(t * freq);
						float d = std::abs(f - center);
						d = std::min(d, 1.0f - d); // wrap distance on [0,1)
						return d < halfWidth;
						};

					bool red1 = stripe(ru, 6.0f, 0.12f, 0.012f) || stripe(ru, 6.0f, 0.62f, 0.012f);
					bool red2 = stripe(rv, 6.0f, 0.12f, 0.012f) || stripe(rv, 6.0f, 0.62f, 0.012f);

					if (red1 || red2) {
						// keep some fabric showing through (slightly dark red)
						br = uint8_t(std::min(255, int(br * 0.25f + 200.0f)));
						bg = uint8_t(std::min(255, int(bg * 0.15f + 20.0f)));
						bb = uint8_t(std::min(255, int(bb * 0.15f + 20.0f)));
					}

					data.emplace_back(pack_rgba(br, bg, bb, 0xff));
				}
			}
			assert(data.size() == size * size);

			//make palce for textur eto live on the GPU
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size, .height = size }, //siz eof image
				VK_FORMAT_R8G8B8A8_UNORM, //HOW TO INTERPRET IMAGE DATA(in this case, linearly -encode * -bit RGBA
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sapmle and uplaod
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,	//should be device local
				Helpers::Unmapped
			));

			//transfer data
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0])* data.size(), textures.back());
		}

		{
			//texture 1 will be the 'xor' texture
			//actually make the texture:
			/*uint32_t size = 256;
			std::vector< uint32_t > data;
			data.reserve(size* size);
			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					uint8_t r = uint8_t(x) ^ uint8_t(y);
					uint8_t g = uint8_t(x + 128) ^ uint8_t(y);
					uint8_t b = uint8_t(x) ^ uint8_t(y + 27);
					uint8_t a = 0xff;
					data.emplace_back(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24));
				}
			}*/

			uint32_t size = 256;
			std::vector<uint32_t> data;
			data.resize(size* size);


			struct Vec3 { float x, y, z; };

			auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
			auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
			auto clamp01 = [](float x) { return std::max(0.0f, std::min(1.0f, x)); };

			// Hash for unsigned lattice coordinates:
			auto hash2u = [](uint32_t x, uint32_t y, uint32_t seed) -> uint32_t {
				uint32_t h = seed;
				h ^= x + 0x9e3779b9u + (h << 6) + (h >> 2);
				h ^= y + 0x9e3779b9u + (h << 6) + (h >> 2);
				h ^= h >> 16; h *= 0x7feb352du;
				h ^= h >> 15; h *= 0x846ca68bu;
				h ^= h >> 16;
				return h;
				};

			// Tileable value noise in [0,1] using uint32_t lattice:
			auto value_noise = [&](float x, float y, uint32_t period, uint32_t seed) -> float {
				// x,y are non-negative in this use, so uint32_t floor is safe:
				uint32_t x0 = uint32_t(std::floor(x));
				uint32_t y0 = uint32_t(std::floor(y));
				uint32_t x1 = x0 + 1u;
				uint32_t y1 = y0 + 1u;

				auto modp = [&](uint32_t v) -> uint32_t {
					return (period == 0u) ? 0u : (v % period);
					};

				float fx = x - float(x0);
				float fy = y - float(y0);

				float u = fade(fx);
				float v = fade(fy);

				float n00 = (hash2u(modp(x0), modp(y0), seed) & 0xffffu) / 65535.0f;
				float n10 = (hash2u(modp(x1), modp(y0), seed) & 0xffffu) / 65535.0f;
				float n01 = (hash2u(modp(x0), modp(y1), seed) & 0xffffu) / 65535.0f;
				float n11 = (hash2u(modp(x1), modp(y1), seed) & 0xffffu) / 65535.0f;

				float nx0 = lerp(n00, n10, u);
				float nx1 = lerp(n01, n11, u);
				return lerp(nx0, nx1, v);
				};

			// fBm (tileable) with uint32_t periods:
			auto fbm = [&](float x, float y, uint32_t period, uint32_t seed) -> float {
				float sum = 0.0f;
				float amp = 0.5f;
				float freq = 1.0f;
				float norm = 0.0f;

				for (uint32_t o = 0; o < 5u; ++o) {
					uint32_t p = period * uint32_t(freq); // freq is 1,2,4,8,16 => exact in float
					sum += amp * value_noise(x * freq, y * freq, p, seed + 1013u * o);
					norm += amp;
					amp *= 0.5f;
					freq *= 2.0f;
				}
				return sum / std::max(1e-6f, norm);
				};

			auto pack_rgba = [](float r, float g, float b, float a) -> uint32_t {
				auto to_u8 = [](float x) -> uint32_t {
					int v = int(std::round(std::max(0.0f, std::min(1.0f, x)) * 255.0f));
					return uint32_t(v);
					};
				uint32_t R = to_u8(r);
				uint32_t G = to_u8(g);
				uint32_t B = to_u8(b);
				uint32_t A = to_u8(a);
				// 0xAABBGGRR (matches your constants)
				return (A << 24) | (B << 16) | (G << 8) | (R << 0);
				};

			// Parameters:
			const uint32_t seed = 1337u;
			const uint32_t period = 64u;      // tile period
			const float veinFreq = 8.0f;
			const float warpAmp = 2.25f;
			const float turbScale = 6.0f;
			const float contrast = 1.25f;

			const Vec3 baseLight{ 0.86f, 0.87f, 0.90f };
			const Vec3 baseDark{ 0.20f, 0.22f, 0.28f };
			const Vec3 tint{ 0.02f, 0.05f, 0.08f };

			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					float u = (x + 0.5f) / float(size);
					float v = (y + 0.5f) / float(size);

					float px = u * float(period);
					float py = v * float(period);

					float t = fbm(px * turbScale, py * turbScale, period, seed);
					float warp = (t * 2.0f - 1.0f) * warpAmp;

					const float TWO_PI = 6.28318530718f;
					float phase = (px + warp) * veinFreq;
					float veins = 0.5f + 0.5f * std::sin(TWO_PI * phase);

					float grain = fbm(px * 3.0f + 17.0f, py * 3.0f - 29.0f, period, seed ^ 0x9e3779b9u);
					grain = (grain - 0.5f) * 0.08f;

					float m = clamp01(std::pow(veins, contrast) + grain);

					float r = lerp(baseDark.x, baseLight.x, m) + tint.x * (1.0f - m);
					float g = lerp(baseDark.y, baseLight.y, m) + tint.y * (1.0f - m);
					float b = lerp(baseDark.z, baseLight.z, m) + tint.z * (1.0f - m);

					data[y * size + x] = pack_rgba(r, g, b, 1.0f);
				}
			}
			assert(data.size() == size * size);

			// make a place fore the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size, .height = size }, // size of images
				VK_FORMAT_R8G8B8A8_SRGB, //how to interspret image data (in this case, SRGB-encoded 8-bit  RGBA
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will smaple and uplaod 
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
				Helpers::Unmapped
			));

			//transfer data:
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0])* data.size(), textures.back());
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

Tutorial::~Tutorial() {
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

		if(!object_instances.empty()){//draw with the object pipeline:
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
	{
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
	}

	//{//make some crossing lines at differnt depths:
	//	lines_vertices.clear();
	//	constexpr size_t count = 2 * 30 + 2 * 30;
	//	lines_vertices.reserve(count);

	//	//hoirizontal lines at z = 0.5 :
	//	for (uint32_t i = 0; i < 30;++i) {
	//		float y = (i + 0.5f)/ 30.0f * 2.0f - 1.0f;
	//		lines_vertices.emplace_back(PosColVertex{
	//			.Position{ .x = -1.0f, .y = y, .z = 0.5f},
	//			.Color {.r = 0xff, .g= 0xff, .b = 0x00, .a  = 0xff},
	//			});
	//	lines_vertices.emplace_back(PosColVertex{
	//			.Position{.x = 1.0f, .y = y, .z = 0.5f},
	//			.Color {.r = 0xff, .g = 0xff, .b = 0x00, .a = 0xff},
	//			});

	//	}

	//	//vetical lines at z = 0.0 (near) through 1.0 far:
	//	for (uint32_t i = 0; i < 30;++i) {
	//		float x = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
	//		float z = (i + 0.5f) / 30.0f;
	//	lines_vertices.emplace_back(PosColVertex{
	//			.Position{.x = x, .y = -1.0f, .z = z},
	//			.Color {.r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff},
	//			});
	//		lines_vertices.emplace_back(PosColVertex{
	//			.Position{.x = x, .y = 1.0f, .z = z},
	//			.Color {.r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff},
	//			});
	//	}
	//	assert(lines_vertices.size() == count);

	//}
	{ //make some objects:
		object_instances.clear();

		//{ //plane translated +x by one unit:
		//	mat4 WORLD_FROM_LOCAL{
		//		1.0f, 0.0f, 0.0f, 0.0f,
		//		0.0f, 1.0f, 0.0f, 0.0f,
		//		0.0f, 0.0f, 1.0f, 0.0f,
		//		1.0f, 0.0f, 0.0f, 1.0f,
		//	};

		//	object_instances.emplace_back(ObjectInstance{
		//		.vertices = plane_vertices,
		//		.transform{
		//			.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
		//			.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
		//			.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
		//		},
		//		.texture = 1,
		//		});
		//	
		//}
		{ //torus translated -x by one unit and rotated CCW around +y:
			float ang = time / 60.0f * 2.0f * float(M_PI) * 10.0f;
			float ca = std::cos(ang);
			float sa = std::sin(ang);
			mat4 WORLD_FROM_LOCAL{
				  ca, 0.0f,  -sa, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				  sa, 0.0f,   ca, 0.0f,
				-1.0f,0.0f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = torus_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				});
		}

		{ // wineglass instance
			mat4 WORLD_FROM_LOCAL{
				1.0f,0.0f,0.0f,0.0f,
				0.0f,1.0f,0.0f,0.0f,
				0.0f,0.0f,1.0f,0.0f,
				0.0f,0.0f,0.0f,1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = genus2_vertices,   // <-- ObjectVertices slice
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				.texture = 1,
				});
		}
	}

}


void Tutorial::on_input(InputEvent const &) {
}
