// minimalistic code to draw a single triangle, this is not part of the API.
#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#ifdef _WIN32 // must use MT platform DLL libraries on windows
	#pragma comment(lib, "shaderc_combined.lib") 
#endif
#define	PI	3.14159265359f
#define	Deg2Rad(a)	(a * PI / 180)
// Simple Vertex Shader
const char* vertexShaderSource = R"(
// an ultra simple hlsl vertex shader

struct SHADER_VARS
{
	matrix wm;
	matrix view;
};
[[vk::push_constant]] ConstantBuffer<SHADER_VARS> ob;
float4 main(float4 inputVertex : POSITION) : SV_POSITION
{
	inputVertex = mul(ob.wm, inputVertex);
	inputVertex = mul(ob.view, inputVertex);
	return float4(inputVertex);
}
)";
// Simple Pixel Shader
const char* pixelShaderSource = R"(
// an ultra simple hlsl pixel shader
float4 main() : SV_TARGET 
{	
	return float4(0.89f ,0.34f, 0.14f, 0);
}
)";
// Creation, Rendering & Cleanup
class Renderer
{
	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	GW::CORE::GEventReceiver shutdown;
	
	GW::INPUT::GInput gin;
	GW::INPUT::GController gcon;
	const float Camera_Speed = 0.03f;
	
	GW::MATH::GMatrix matrixmath;
	
	GW::MATH::GMATRIXF world[6] = { { 0 },{ 0 },{ 0 },{ 0 },{ 0 },{ 0 } };
	
	GW::MATH::GMATRIXF view = { 0 };
	
	GW::MATH::GMATRIXF proj = { 0 };
	// what we need at a minimum to draw a triangle
	VkDevice device = nullptr;
	VkBuffer vertexHandle = nullptr;
	VkDeviceMemory vertexData = nullptr;
	VkShaderModule vertexShader = nullptr;
	VkShaderModule pixelShader = nullptr;
	// pipeline settings for drawing (also required)
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;
public:
	
	struct Vertex { float x, y, z, w; };
	
	struct SHADER_VARS { GW::MATH::GMATRIXF wm, view; };
	
	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk)
	{
		win = _win;
		vlk = _vlk;
		unsigned int width, height;
		win.GetClientWidth(width);
		win.GetClientHeight(height);
		
		gin.Create(win);
		gcon.Create();
		
		matrixmath.Create();

		
		matrixmath.IdentityF(world[0]);
		matrixmath.TranslateGlobalF(world[0], GW::MATH::GVECTORF { 0, -0.5f, 0, 0 }, world[0]);
		matrixmath.RotateXGlobalF(world[0], Deg2Rad(90), world[0]);
		
		matrixmath.IdentityF(world[1]);
		matrixmath.TranslateGlobalF(world[1], GW::MATH::GVECTORF { -0.5f, 0, 0, 0 }, world[1]);
		matrixmath.RotateYGlobalF(world[1], Deg2Rad(-90), world[1]);
		
		matrixmath.IdentityF(world[2]);
		matrixmath.TranslateGlobalF(world[2], GW::MATH::GVECTORF { 0.5f, 0, 0, 0 }, world[2]);
		matrixmath.RotateYGlobalF(world[2], Deg2Rad(90), world[2]);
		
		matrixmath.IdentityF(world[3]);
		matrixmath.TranslateGlobalF(world[3], GW::MATH::GVECTORF { 0, 0, -0.5f, 0 }, world[3]);
		matrixmath.RotateYGlobalF(world[3], Deg2Rad(180), world[3]);
		
		matrixmath.IdentityF(world[4]);
		matrixmath.TranslateGlobalF(world[4], GW::MATH::GVECTORF { 0, 0, 0.5f, 0 }, world[4]);
		
		matrixmath.IdentityF(world[5]);
		matrixmath.TranslateGlobalF(world[5], GW::MATH::GVECTORF { 0, 0.5f, 0, 0 }, world[5]);
		matrixmath.RotateXGlobalF(world[5], Deg2Rad(-90), world[5]);

		
		GW::MATH::GVECTORF eye { 0 }, at { 0 };
		matrixmath.IdentityF(view);
		matrixmath.TranslateGlobalF(view, GW::MATH::GVECTORF { 0.25f, -0.125f, -0.25f, 0 }, view);

		matrixmath.GetTranslationF(world[0], at);
		matrixmath.GetTranslationF(view, eye);
		matrixmath.LookAtLHF(eye, at, GW::MATH::GVECTORF { 0, 1, 0, 0 }, view);

		/***************** GEOMETRY INTIALIZATION ******************/
		// Grab the device & physical device so we can allocate some stuff
		VkPhysicalDevice physicalDevice = nullptr;
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);

		// Create Vertex Buffer
		std::vector<Vertex> verts;
		for (float i = -0.5f, count = 0; count < 26; i = i + 0.04f, ++count) {
			verts.push_back(Vertex {i, 0.5f, 0, 1});
			verts.push_back(Vertex {i, -0.5f, 0, 1});
			verts.push_back(Vertex {-0.5f, i, 0, 1});
			verts.push_back(Vertex {0.5f, i, 0, 1});
		}

		// Transfer triangle data to the vertex buffer. (staging would be prefered here)
		GvkHelper::create_buffer(physicalDevice, device, sizeof(Vertex) * verts.size(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexHandle, &vertexData);
		GvkHelper::write_to_buffer(device, vertexData, verts.data(), sizeof(Vertex) * verts.size());

		/***************** SHADER INTIALIZATION ******************/
		// Intialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t options = shaderc_compile_options_initialize();
		shaderc_compile_options_set_source_language(options, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(options, false);
#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(options);
#endif
		// Create Vertex Shader
		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderSource, strlen(vertexShaderSource),
			shaderc_vertex_shader, "main.vert", "main", options);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Vertex Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &vertexShader);
		shaderc_result_release(result); // done
		// Create Pixel Shader
		result = shaderc_compile_into_spv( // compile
			compiler, pixelShaderSource, strlen(pixelShaderSource),
			shaderc_fragment_shader, "main.frag", "main", options);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Pixel Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &pixelShader);
		shaderc_result_release(result); // done
		// Free runtime shader compiler resources
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);

		/***************** PIPELINE INTIALIZATION ******************/
		// Create Pipeline & Layout (Thanks Tiny!)
		VkRenderPass renderPass;
		vlk.GetRenderPass((void**)&renderPass);
		VkPipelineShaderStageCreateInfo stage_create_info[2] = {};
		// Create Stage Info for Vertex Shader
		stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create_info[0].module = vertexShader;
		stage_create_info[0].pName = "main";
		// Create Stage Info for Fragment Shader
		stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create_info[1].module = pixelShader;
		stage_create_info[1].pName = "main";
		// Assembly State
		VkPipelineInputAssemblyStateCreateInfo assembly_create_info = {};
		assembly_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assembly_create_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;	//	can also use LINE_STRIP instead, without changing verts (Part 1B)
		assembly_create_info.primitiveRestartEnable = false;
		// Vertex Input State
		VkVertexInputBindingDescription vertex_binding_description = {};
		vertex_binding_description.binding = 0;
		vertex_binding_description.stride = sizeof(Vertex) * 1;
		vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		
		VkVertexInputAttributeDescription vertex_attribute_description[1] = {
			{ 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 } //uv, normal, etc....
		};
		VkPipelineVertexInputStateCreateInfo input_vertex_info = {};
		input_vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		input_vertex_info.vertexBindingDescriptionCount = 1;
		input_vertex_info.pVertexBindingDescriptions = &vertex_binding_description;
		input_vertex_info.vertexAttributeDescriptionCount = 1;
		input_vertex_info.pVertexAttributeDescriptions = vertex_attribute_description;
		// Viewport State (we still need to set this up even though we will overwrite the values)
		VkViewport viewport = {
            0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1
        };
        VkRect2D scissor = { {0, 0}, {width, height} };
		VkPipelineViewportStateCreateInfo viewport_create_info = {};
		viewport_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_create_info.viewportCount = 1;
		viewport_create_info.pViewports = &viewport;
		viewport_create_info.scissorCount = 1;
		viewport_create_info.pScissors = &scissor;
		// Rasterizer State
		VkPipelineRasterizationStateCreateInfo rasterization_create_info = {};
		rasterization_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization_create_info.rasterizerDiscardEnable = VK_FALSE;
		rasterization_create_info.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization_create_info.lineWidth = 1.0f;
		rasterization_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterization_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterization_create_info.depthClampEnable = VK_FALSE;
		rasterization_create_info.depthBiasEnable = VK_FALSE;
		rasterization_create_info.depthBiasClamp = 0.0f;
		rasterization_create_info.depthBiasConstantFactor = 0.0f;
		rasterization_create_info.depthBiasSlopeFactor = 0.0f;
		// Multisampling State
		VkPipelineMultisampleStateCreateInfo multisample_create_info = {};
		multisample_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample_create_info.sampleShadingEnable = VK_FALSE;
		multisample_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisample_create_info.minSampleShading = 1.0f;
		multisample_create_info.pSampleMask = VK_NULL_HANDLE;
		multisample_create_info.alphaToCoverageEnable = VK_FALSE;
		multisample_create_info.alphaToOneEnable = VK_FALSE;
		// Depth-Stencil State
		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {};
		depth_stencil_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_stencil_create_info.depthTestEnable = VK_TRUE;
		depth_stencil_create_info.depthWriteEnable = VK_TRUE;
		depth_stencil_create_info.depthCompareOp = VK_COMPARE_OP_LESS;
		depth_stencil_create_info.depthBoundsTestEnable = VK_FALSE;
		depth_stencil_create_info.minDepthBounds = 0.0f;
		depth_stencil_create_info.maxDepthBounds = 1.0f;
		depth_stencil_create_info.stencilTestEnable = VK_FALSE;
		// Color Blending Attachment & State
		VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
		color_blend_attachment_state.colorWriteMask = 0xF;
		color_blend_attachment_state.blendEnable = VK_FALSE;
		color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
		color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
		VkPipelineColorBlendStateCreateInfo color_blend_create_info = {};
		color_blend_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend_create_info.logicOpEnable = VK_FALSE;
		color_blend_create_info.logicOp = VK_LOGIC_OP_COPY;
		color_blend_create_info.attachmentCount = 1;
		color_blend_create_info.pAttachments = &color_blend_attachment_state;
		color_blend_create_info.blendConstants[0] = 0.0f;
		color_blend_create_info.blendConstants[1] = 0.0f;
		color_blend_create_info.blendConstants[2] = 0.0f;
		color_blend_create_info.blendConstants[3] = 0.0f;
		// Dynamic State 
		VkDynamicState dynamic_state[2] = { 
			// By setting these we do not need to re-create the pipeline on Resize
			VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamic_create_info = {};
		dynamic_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_create_info.dynamicStateCount = 2;
		dynamic_create_info.pDynamicStates = dynamic_state;
		
		VkPushConstantRange range { VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SHADER_VARS) };
		// Descriptor pipeline layout
		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 0;
		pipeline_layout_create_info.pSetLayouts = VK_NULL_HANDLE;
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &range;
		vkCreatePipelineLayout(device, &pipeline_layout_create_info, 
			nullptr, &pipelineLayout);
	    // Pipeline State... (FINALLY) 
		VkGraphicsPipelineCreateInfo pipeline_create_info = {};
		pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = stage_create_info;
		pipeline_create_info.pInputAssemblyState = &assembly_create_info;
		pipeline_create_info.pVertexInputState = &input_vertex_info;
		pipeline_create_info.pViewportState = &viewport_create_info;
		pipeline_create_info.pRasterizationState = &rasterization_create_info;
		pipeline_create_info.pMultisampleState = &multisample_create_info;
		pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
		pipeline_create_info.pColorBlendState = &color_blend_create_info;
		pipeline_create_info.pDynamicState = &dynamic_create_info;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderPass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, 
			&pipeline_create_info, nullptr, &pipeline);

		/***************** CLEANUP / SHUTDOWN ******************/
		// GVulkanSurface will inform us when to release any allocated resources
		shutdown.Create(vlk, [&]() {
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			}
		});
	}
	void Render()
	{
		// grab the current Vulkan commandBuffer
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentImage(currentBuffer);
		VkCommandBuffer commandBuffer;
		vlk.GetCommandBuffer(currentBuffer, (void**)&commandBuffer);
		// what is the current client area dimensions?
		unsigned int width, height;
		win.GetClientWidth(width);
		win.GetClientHeight(height);
		// setup the pipeline's dynamic settings
		VkViewport viewport = {
            0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1
        };
        VkRect2D scissor = { {0, 0}, {width, height} };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		
		//matrixmath.IdentityF(proj);
		float aspectRatio = 0;
		vlk.GetAspectRatio(aspectRatio);
		matrixmath.ProjectionVulkanLHF(Deg2Rad(65), aspectRatio, 0.1, 100, proj);
		
		SHADER_VARS sv { { 0 }, view };
		matrixmath.MultiplyMatrixF(sv.view, proj, sv.view);
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexHandle, offsets);
		for (int i = 0; i < 6; ++i) {
			sv.wm = world[i];
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SHADER_VARS), &sv);
			// now we can draw
			vkCmdDraw(commandBuffer, 104, 1, 0, 0);
		}
	}
	
	void UpdateCamera() {
		static std::chrono::steady_clock::time_point t1, t2;
		t2 = std::chrono::steady_clock::now();
		auto deltatime = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
		t1 = t2;

		float state1 = 0, state2 = 0;
		unsigned int state3 = 0;

		float perFrameSpeed = Camera_Speed * deltatime, total_Z_Change = 0, total_X_Change = 0;

		GW::MATH::GMATRIXF translationMatrix = { 0 };
		GW::MATH::GMATRIXF tempView = { 0 };
		matrixmath.InverseF(view, tempView);

		gin.GetState(G_KEY_W, state1);
		total_Z_Change = state1;
		gin.GetState(G_KEY_S, state1);
		total_Z_Change -= state1;
		/*gcon.GetState(0, G_LY_AXIS, state);
		total_Z_Change += state;*/
		
		gin.GetState(G_KEY_D, state1);
		total_X_Change = state1;
		gin.GetState(G_KEY_A, state1);
		total_X_Change -= state1;
		/*gcon.GetState(0, G_LX_AXIS, state);
		total_X_Change += state;*/

		matrixmath.TranslateLocalF(tempView, GW::MATH::GVECTORF { total_X_Change * perFrameSpeed,0,total_Z_Change * perFrameSpeed, 0 }, tempView);

		float Thumb_Speed = PI * deltatime,
			total_Pitch = Deg2Rad(65);
		gin.GetMouseDelta(state1, state2);
		total_Pitch *= state2;
		win.GetClientHeight(state3);
		total_Pitch /= state3;

		matrixmath.RotateXLocalF(tempView, total_Pitch * deltatime, tempView);

		float total_Yaw = Deg2Rad(65);
		vlk.GetAspectRatio(state2);
		win.GetClientWidth(state3);
		total_Yaw *= state2 * state1 / state3;

		matrixmath.RotateYGlobalF(tempView, total_Yaw * deltatime, tempView);

		float total_Y_Change = 0;
		gin.GetState(G_KEY_SPACE, state1);
		total_Y_Change = state1;
		gin.GetState(G_KEY_LEFTSHIFT, state1);
		total_Y_Change -= state1;
		gcon.GetState(0, G_RIGHT_TRIGGER_AXIS, state1);
		total_Y_Change += state1;
		gcon.GetState(0, G_LEFT_TRIGGER_AXIS, state1);
		total_Y_Change -= state1;

		matrixmath.TranslateGlobalF(tempView, GW::MATH::GVECTORF { 0,total_Y_Change * Camera_Speed * deltatime,0,0 }, tempView);
		
		matrixmath.InverseF(tempView, view);
	}
private:
	void CleanUp()
	{
		// wait till everything has completed
		vkDeviceWaitIdle(device);
		// Release allocated buffers, shaders & pipeline
		vkDestroyBuffer(device, vertexHandle, nullptr);
		vkFreeMemory(device, vertexData, nullptr);
		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, pixelShader, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
	}
};
