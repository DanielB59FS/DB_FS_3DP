#include "Assets/FSLogo.h"
#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#ifdef _WIN32 // must use MT platform DLL libraries on windows
#pragma comment(lib, "shaderc_combined.lib")
#endif

#define	MAX_SUBMESH_PER_DRAW	1024
#define	PI						3.14159265359f
#define	Deg2Rad(a)				(a * PI / 180)

#include <string>
#include <sstream>
#include <fstream>

#include "LevelData.h"

// Simple Vertex Shader
//const char* vertexShaderSource = "";
const char* vertexShaderSource = R"(
#pragma pack_matrix(row_major)

#define MAX_SUBMESH_PER_DRAW 1024

struct OBJ_ATTRIBUTES
{
	float3 Kd;			// diffuse reflectivity
	float d;			// dissolve (transparency)
	float3 Ks;			// specular reflectivity
	float Ns;			// specular exponent
	float3 Ka;			// ambient reflectivity
	float sharpness;	// local reflection map sharpness
	float3 Tf;			// transmission filter
	float Ni;			// optical density (index of refraction)
	float3 Ke;			// emissive reflectivity
	uint illum;			// illumination model
};

struct SHADER_MODEL_DATA
{
	float4 sunDirection, sunColor;
	float4x4 viewMatrix, projectionMatrix;
	float4x4 matricies[MAX_SUBMESH_PER_DRAW];
	OBJ_ATTRIBUTES materials[MAX_SUBMESH_PER_DRAW];
};
[[vk::binding(0)]] StructuredBuffer<SHADER_MODEL_DATA> SceneData;

struct SHADER_VARS
{
	float3 camPos;
	uint mat_ID;
	float4 sunAmbient;
	int padding[24];
};
[[vk::push_constant]] ConstantBuffer<SHADER_VARS> VARS;

struct V_IN
{
	float3 inputVertex : POSITION;
	float3 uvw : TEXCOORD;
	float3 nrm : NORMAL;
};

struct V_OUT
{
	float4 posH : SV_POSITION;
	float3 uvW : TEXCOORD;
	float3 nrmW : NORMAL;
	float3 posW : WORLD;
};

V_OUT main(V_IN inputs, uint instanceID : SV_INSTANCEID)
{
	V_OUT send = (V_OUT) 0;
    
	send.posH = mul(float4(inputs.inputVertex, 1), SceneData[0].matricies[instanceID]);
	send.posW = send.posH.xyz;
	send.posH = mul(send.posH, SceneData[0].viewMatrix);
	send.posH = mul(send.posH, SceneData[0].projectionMatrix);
	send.uvW = inputs.uvw;
	send.nrmW = mul(inputs.nrm, SceneData[0].matricies[instanceID]).xyz;
	
	return send;
}
)";

// Simple Pixel Shader
//const char* pixelShaderSource = "";
const char* pixelShaderSource = R"(
#define MAX_SUBMESH_PER_DRAW 1024

struct OBJ_ATTRIBUTES
{
	float3 Kd;			// diffuse reflectivity
	float d;			// dissolve (transparency)
	float3 Ks;			// specular reflectivity
	float Ns;			// specular exponent
	float3 Ka;			// ambient reflectivity
	float sharpness;	// local reflection map sharpness
	float3 Tf;			// transmission filter
	float Ni;			// optical density (index of refraction)
	float3 Ke;			// emissive reflectivity
	uint illum;			// illumination model
};

struct SHADER_MODEL_DATA
{
	float4 sunDirection, sunColor;
	float4x4 viewMatrix, projectionMatrix;
	float4x4 matricies[MAX_SUBMESH_PER_DRAW];
	OBJ_ATTRIBUTES materials[MAX_SUBMESH_PER_DRAW];
};
[[vk::binding(0)]] StructuredBuffer<SHADER_MODEL_DATA> SceneData;

struct SHADER_VARS
{
	float3 camPos;
	uint mat_ID;
	float4 sunAmbient;
	int padding[24];
};
[[vk::push_constant]] ConstantBuffer<SHADER_VARS> VARS;

struct V_IN
{
	float4 posH : SV_POSITION;
	float3 uvW : TEXCOORD;
	float3 nrmW : NORMAL;
	float3 posW : WORLD;
};

float4 main(V_IN args) : SV_TARGET
{
	float3 nrmW = normalize(args.nrmW);
	float lightRatio = clamp(dot(-SceneData[0].sunDirection.xyz, nrmW), 0, 1);
	
	float3 viewDir = normalize(VARS.camPos - args.posW);
	
	float3 halfVector = normalize(normalize(-SceneData[0].sunDirection.xyz) + viewDir);
	float3 reflectVector = normalize(reflect(SceneData[0].sunDirection.xyz,nrmW));
	
	float intensity = max(pow(clamp(dot(nrmW, halfVector), 0, 1), SceneData[0].materials[VARS.mat_ID].Ns), 0);
	
	//float intensity = saturate(dot(viewDir, reflectVector));
	//intensity = saturate(pow(intensity,SceneData[0].materials[mat_ID].Ns));
	
	float4 reflectedLight = SceneData[0].sunColor * float4(SceneData[0].materials[VARS.mat_ID].Ks, 1) * intensity;
	
	float4 res = saturate(lightRatio * SceneData[0].sunColor + VARS.sunAmbient) * float4(SceneData[0].materials[VARS.mat_ID].Kd, SceneData[0].materials[VARS.mat_ID].d) + reflectedLight;
	
	return res;
}
)";

// Load a shader file as a string of characters.
std::string ShaderAsString(const char* shaderFilePath) {
	std::string output;
	unsigned int stringLength = 0;
	GW::SYSTEM::GFile file; file.Create();
	file.GetFileSize(shaderFilePath, stringLength);
	if (stringLength && +file.OpenBinaryRead(shaderFilePath)) {
		output.resize(stringLength);
		file.Read(&output[0], stringLength);
	}
	else
		std::cout << "ERROR: Shader Source File \"" << shaderFilePath << "\" Not Found!" << std::endl;
	return output;
}

// Creation, Rendering & Cleanup
class Renderer {
	struct SHADER_MODEL_DATA {
		// Globally shared model data
		GW::MATH::GVECTORF sunDirection, sunColor;				// lighting info
		GW::MATH::GMATRIXF viewMatrix, projectionMatrix;		// viewing info

		// Per sub-mesh transform and material data
		GW::MATH::GMATRIXF matricies[MAX_SUBMESH_PER_DRAW];		// world space transforms
		H2B::ATTRIBUTES materials[MAX_SUBMESH_PER_DRAW];		// color/texture of surface
	};

	GW::INPUT::GInput gin;
	GW::INPUT::GController gcon;
	const float Camera_Speed = 0.03f;

	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	GW::CORE::GEventReceiver shutdown;

	// what we need at a minimum to draw a triangle
	VkDevice device = nullptr;
	VkBuffer vertexHandle = nullptr;
	VkDeviceMemory vertexData = nullptr;

	VkBuffer indiceHandle = nullptr;
	VkDeviceMemory indiceData = nullptr;

	std::vector<VkBuffer> storageHandle;
	std::vector<VkDeviceMemory> storageData;

	VkBuffer storageVertexHandle = nullptr;
	VkDeviceMemory storageVertexData = nullptr;
	VkBuffer storageIndiceHandle = nullptr;
	VkDeviceMemory storageIndiceData = nullptr;

	VkShaderModule vertexShader = nullptr;
	VkShaderModule pixelShader = nullptr;

	// pipeline settings for drawing (also required)
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;

	VkDescriptorSetLayout descriptorSetLayoutHandle = nullptr;

	VkDescriptorPool descriptorPool;

	std::vector<VkDescriptorSet> descriptorSet;
	
	GW::MATH::GMatrix matrixmath;
	GW::MATH::GMATRIXF world, view, projection;
	GW::MATH::GVECTORF lightDirection, lightColor;

	SHADER_MODEL_DATA smd = { {0},{0},{0},{0},{0},{0} };
	
	GW::SYSTEM::GLog log;

	LevelData dataOrientedLoader;

public:
	struct SHADER_VARS {
		OBJ_VEC3 camPos;
		unsigned int mat_ID;
		GW::MATH::GVECTORF sunAmbient;
		int padding[24];
	};

	struct Vertex {
		GW::MATH::GVECTORF pos;
		OBJ_VEC3 uvw, nrm;
	};

	void UniformInitilization() {
		GW::MATH::GVECTORF vec = { 0 };
		world = view = GW::MATH::GIdentityMatrixF;
		projection = GW::MATH::GIdentityMatrixF;
		lightDirection = { -1,-1,2,0 };
		lightColor = { 0.9f,0.9f,1.f,1.f };

		matrixmath.Create();

		matrixmath.TranslateGlobalF(view, GW::MATH::GVECTORF { 0.75f, 0.25f, -1.5f }, view);
		matrixmath.GetTranslationF(view, vec);
		matrixmath.LookAtLHF(vec, GW::MATH::GVECTORF { 0.15f, 0.75f, 0, 0 }, GW::MATH::GVECTORF { 0, 1, 0, 0 }, view);

		float aspectRatio = 0;
		vlk.GetAspectRatio(aspectRatio);
		matrixmath.ProjectionVulkanLHF(Deg2Rad(65), aspectRatio, 0.1, 100, projection);

		smd.viewMatrix = view;
		smd.projectionMatrix = projection;
		GW::MATH::GVector::NormalizeF(lightDirection, smd.sunDirection);
		smd.sunColor = lightColor;
	}

	void GeometryInitilization(VkPhysicalDevice& physicalDevice) {
		/***************** GEOMETRY INTIALIZATION ******************/
		// Grab the device & physical device so we can allocate some stuff
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);

		CputoGpu(physicalDevice, dataOrientedLoader.levelVertices, dataOrientedLoader.levelIndices);
	}

	//void CreateVertexIndiceBuffers(std::vector<H2B::VERTEX>& verts, std::vector<unsigned int>& indices) {
	//	// TODO: Part 1c
	//	// Create Vertex Buffer
	//	for (int i = 0; i < FSLogo_vertexcount; ++i)
	//		verts.push_back({ FSLogo_vertices[i].pos, FSLogo_vertices[i].uvw, FSLogo_vertices[i].nrm });
	//		//verts.push_back({ GW::MATH::GVECTORF { FSLogo_vertices[i].pos.x, FSLogo_vertices[i].pos.y, FSLogo_vertices[i].pos.z, 1 }, FSLogo_vertices[i].uvw, FSLogo_vertices[i].nrm });

	//	for (int i = 0; i < FSLogo_indexcount; ++i)
	//		indices.push_back(FSLogo_indices[i]);
	//}

	void CputoGpu(VkPhysicalDevice physicalDevice, std::vector<H2B::VERTEX>& verts, std::vector<unsigned int>& indices) {
		// Transfer triangle data to the vertex buffer. (staging would be prefered here)
		GvkHelper::create_buffer(physicalDevice, device, sizeof(H2B::VERTEX) * verts.size(),
								 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
								 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexHandle, &vertexData);
		GvkHelper::write_to_buffer(device, vertexData, verts.data(), sizeof(H2B::VERTEX) * verts.size());
		
		GvkHelper::create_buffer(physicalDevice, device, sizeof(unsigned int) * indices.size(),
								 VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
								 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &indiceHandle, &indiceData);
		GvkHelper::write_to_buffer(device, indiceData, indices.data(), sizeof(unsigned int) * indices.size());
	}

	void CreateStorageBuffer(unsigned int& maxFrames, VkPhysicalDevice physicalDevice) {
		memcpy(smd.matricies, dataOrientedLoader.levelTransforms.data(), min(MAX_SUBMESH_PER_DRAW, dataOrientedLoader.levelTransforms.size()) * sizeof(GW::MATH::GMATRIXF));
		
		for (size_t i = 0; i < min(MAX_SUBMESH_PER_DRAW, dataOrientedLoader.levelMaterials.size()); ++i)
			smd.materials[i] = dataOrientedLoader.levelMaterials[i].attrib;

		vlk.GetSwapchainImageCount(maxFrames);
		storageHandle.resize(maxFrames);
		storageData.resize(maxFrames);

		for (int i = 0; i < maxFrames; ++i) {
			GvkHelper::create_buffer(physicalDevice, device, sizeof(SHADER_MODEL_DATA),
									 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &storageHandle[i], &storageData[i]);

			GvkHelper::write_to_buffer(device, storageData[i], &smd, sizeof(SHADER_MODEL_DATA));
		}
	}

	void ShaderInitilization(shaderc_compiler_t& compiler, shaderc_compile_options_t& options) {
		compiler = shaderc_compiler_initialize();
		options = shaderc_compile_options_initialize();
		
		shaderc_compile_options_set_source_language(options, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(options, false);

#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(options);
#endif
	}

	void CompileCreateVertexShader(shaderc_compiler_t compiler, shaderc_compile_options_t options, const char* shaderSource) {
		// Create Vertex Shader
		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
																	   compiler, shaderSource, strlen(shaderSource),
																	   shaderc_vertex_shader, "main.vert", "main", options);
		
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Vertex Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
										(char*)shaderc_result_get_bytes(result), &vertexShader);
		shaderc_result_release(result); // done
	}

	void CompileCreatePixelShader(shaderc_compiler_t compiler, shaderc_compile_options_t options, const char* shaderSource) {
		// Create Pixel Shader
		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
																	   compiler, shaderSource, strlen(shaderSource),
																	   shaderc_fragment_shader, "main.frag", "main", options);
		
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Pixel Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
										(char*)shaderc_result_get_bytes(result), &pixelShader);
		shaderc_result_release(result); // done
	}

	void LoadCreateShader(const char* filePath, VkShaderModule& shader) {
		std::ifstream fis(filePath, std::ifstream::in | std::ifstream::binary);
		std::string str = ReadFile(fis);
		GvkHelper::create_shader_module(device, str.size(), // load into Vulkan
										const_cast<char*>(str.c_str()), &shader);
	}

	//VkPipelineShaderStageCreateInfo* CreateStageInfo(VkPipelineShaderStageCreateInfo stage_create_info[], size_t size) {
	//	// Create Stage Info for Vertex Shader
	//	stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	//	stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	//	stage_create_info[0].module = vertexShader;
	//	stage_create_info[0].pName = "main";
	//	// Create Stage Info for Fragment Shader
	//	stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	//	stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	//	stage_create_info[1].module = pixelShader;
	//	stage_create_info[1].pName = "main";

	//	return stage_create_info;
	//}

	std::string ReadFile(std::ifstream& in) {
		std::ostringstream sstr;
		sstr << in.rdbuf();
		return sstr.str();
	}

	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk) {
		log.Create("../LevelLoaderLog.txt");
		log.EnableConsoleLogging(true);

		dataOrientedLoader.LoadLevel("../GameLevel.txt", "../Models", log);

		win = _win;
		vlk = _vlk;
		unsigned int width, height;
		win.GetClientWidth(width);
		win.GetClientHeight(height);

		gin.Create(win);
		gcon.Create();

		UniformInitilization();

		/***************** GEOMETRY INTIALIZATION ******************/
		// Grab the device & physical device so we can allocate some stuff
		unsigned int maxFrames = 0;
		VkPhysicalDevice physicalDevice = nullptr;

		GeometryInitilization(physicalDevice);
		CreateStorageBuffer(maxFrames, physicalDevice);

		/***************** SHADER INTIALIZATION ******************/
		// Intialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = nullptr;
		shaderc_compile_options_t options = nullptr;

		ShaderInitilization(compiler, options);

		// Create Vertex Shader
		CompileCreateVertexShader(compiler, options, vertexShaderSource);
		//LoadCreateShader("./Debug/mainVert.cso", vertexShader);

		// Create Pixel Shader
		CompileCreatePixelShader(compiler, options, pixelShaderSource);
		//LoadCreateShader("./Debug/mainFrag.cso", pixelShader);

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
		assembly_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		assembly_create_info.primitiveRestartEnable = false;
		
		// Vertex Input State
		VkVertexInputBindingDescription vertex_binding_description = {};
		vertex_binding_description.binding = 0;
		vertex_binding_description.stride = sizeof(H2B::VERTEX) * 1;
		vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		
		VkVertexInputAttributeDescription vertex_attribute_description[3] = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, //uv, normal, etc....
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(H2B::VECTOR) },
			{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(H2B::VECTOR) * 2 }
		};
		
		VkPipelineVertexInputStateCreateInfo input_vertex_info = {};
		input_vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		input_vertex_info.vertexBindingDescriptionCount = 1;
		input_vertex_info.pVertexBindingDescriptions = &vertex_binding_description;
		input_vertex_info.vertexAttributeDescriptionCount = 3;
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

		VkDescriptorSetLayoutBinding des_set_layout_binding = {};
		des_set_layout_binding.descriptorCount = 1;
		des_set_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		des_set_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		des_set_layout_binding.binding = 0;
		des_set_layout_binding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo des_set_layout_create_info = {};
		des_set_layout_create_info.bindingCount = 1;
		des_set_layout_create_info.flags = 0;
		des_set_layout_create_info.pBindings = &des_set_layout_binding;
		des_set_layout_create_info.pNext = nullptr;
		des_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

		vkCreateDescriptorSetLayout(device, &des_set_layout_create_info, nullptr, &descriptorSetLayoutHandle);
		
		VkDescriptorPoolSize des_pool_size = {};
		des_pool_size.descriptorCount = maxFrames;
		des_pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

		VkDescriptorPoolCreateInfo des_pool_create_info = {};
		des_pool_create_info.flags = 0;
		des_pool_create_info.maxSets = maxFrames;
		des_pool_create_info.pNext = nullptr;
		des_pool_create_info.poolSizeCount = 1;
		des_pool_create_info.pPoolSizes = &des_pool_size;
		des_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		vkCreateDescriptorPool(device, &des_pool_create_info, nullptr, &descriptorPool);
		
		VkDescriptorSetAllocateInfo des_set_alloc_info = {};
		des_set_alloc_info.descriptorPool = descriptorPool;
		des_set_alloc_info.descriptorSetCount = 1;
		des_set_alloc_info.pNext = nullptr;
		des_set_alloc_info.pSetLayouts = &descriptorSetLayoutHandle;
		des_set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;

		descriptorSet.resize(maxFrames);
		for (int i = 0; i < maxFrames; ++i)
			vkAllocateDescriptorSets(device, &des_set_alloc_info, &descriptorSet[i]);
		
		VkWriteDescriptorSet des_set_write = {};
		des_set_write.descriptorCount = 1;
		des_set_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		des_set_write.dstArrayElement = 0;
		des_set_write.dstBinding = 0;
		des_set_write.pImageInfo = VK_NULL_HANDLE;
		des_set_write.pNext = nullptr;
		des_set_write.pTexelBufferView = VK_NULL_HANDLE;
		des_set_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

		for (int i = 0; i < maxFrames; ++i) {
			des_set_write.dstSet = descriptorSet[i];
			VkDescriptorBufferInfo dbinfo = { storageHandle[i], 0, VK_WHOLE_SIZE };
			des_set_write.pBufferInfo = &dbinfo;
			vkUpdateDescriptorSets(device, 1, &des_set_write, 0, nullptr);
		}
		
		// Descriptor pipeline layout
		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_create_info.setLayoutCount = 1;
		pipeline_layout_create_info.pSetLayouts = &descriptorSetLayoutHandle;
		
		VkPushConstantRange range { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SHADER_VARS) };
		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &range;
		
		vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipelineLayout);
		
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

	void Update() {
		UpdateCamera();
		
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentImage(currentBuffer);
		GvkHelper::write_to_buffer(device, storageData[currentBuffer], &smd, sizeof(SHADER_MODEL_DATA));
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
		matrixmath.InverseF(smd.viewMatrix, tempView);

		gin.GetState(G_KEY_W, state1);
		total_Z_Change = state1;
		gin.GetState(G_KEY_S, state1);
		total_Z_Change -= state1;
		state1 = 0;
		gcon.GetState(0, G_LY_AXIS, state1);
		total_Z_Change += state1;

		gin.GetState(G_KEY_D, state1);
		total_X_Change = state1;
		gin.GetState(G_KEY_A, state1);
		total_X_Change -= state1;
		state1 = 0;
		gcon.GetState(0, G_LX_AXIS, state1);
		total_X_Change += state1;

		matrixmath.TranslateLocalF(tempView, GW::MATH::GVECTORF { total_X_Change * perFrameSpeed,0,total_Z_Change * perFrameSpeed, 0 }, tempView);

		float Thumb_Speed = PI * deltatime,
			total_Pitch = Deg2Rad(65);
		GW::GReturn ret = gin.GetMouseDelta(state1, state2);
		if (ret == GW::GReturn::REDUNDANT) {
			state1 = 0;
			state2 = 0;
		}
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
		state1 = 0;
		gcon.GetState(0, G_RIGHT_TRIGGER_AXIS, state1);
		total_Y_Change += state1;
		state1 = 0;
		gcon.GetState(0, G_LEFT_TRIGGER_AXIS, state1);
		total_Y_Change -= state1;

		matrixmath.TranslateGlobalF(tempView, GW::MATH::GVECTORF { 0,total_Y_Change * Camera_Speed * deltatime,0,0 }, tempView);

		matrixmath.InverseF(tempView, smd.viewMatrix);
	}

	/*bool ParseModels() {

		return true;
	}

	bool ParseLevel(const char* fileName) {
		std::string line, key;
		std::ifstream fis(fileName);
		GW::MATH::GMATRIXF value = { 0 };

		if (fis.fail()) return false;

		while (!fis.eof()) {
			std::getline(fis, line);
			std::cout << line << std::endl;
		}
		return true;

		while (!fis.eof()) {
			std::getline(fis, line);
			if (0 != line.compare("MESH")) continue;

			std::getline(fis, line);
			key = strtok(const_cast<char*>(line.c_str()), ".\n\r");
			key += ".h2b";
			std::getline(fis, line, '(');
			for (int i = 0; i < 16; ++i) {
				if (0 == i % 4) {
					std::getline(fis, line, '(');
					line = strtok(const_cast<char*>(line.c_str()), "(,)");
				}
				else
					line = strtok(nullptr, "(,");
				std::istringstream sstr(line);
				sstr >> value.data[i];
			}
		}

		return true;
	}*/

	void Render() {
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

		// now we can draw
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexHandle, offsets);
		
		vkCmdBindIndexBuffer(commandBuffer, indiceHandle, 0, VK_INDEX_TYPE_UINT32);
		
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet[currentBuffer], 0, nullptr);
		
		GW::MATH::GVECTORF translation = { 0 };
		matrixmath.GetTranslationF(smd.viewMatrix, translation);
		OBJ_VEC3 camPos = { 0.75f, 0.25f, -1.5f };

		SHADER_VARS vars { camPos, 0, { 0.25f,0.25f,0.35f,1 }, { 0 } };
		
		for (auto instance : dataOrientedLoader.levelInstances) {
			LevelData::LEVEL_MODEL& model = dataOrientedLoader.levelModels[instance.modelIndex];
			
			vars.mat_ID = model.materialStart;

			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SHADER_VARS), &vars);

			vkCmdDrawIndexed(commandBuffer, model.indexCount, instance.transformCount, model.indexStart, model.vertexStart, instance.transformStart);

			//	planned solution for submesh material drawing:
			//for (size_t i = model.meshStart; i < model.meshStart + model.meshCount; ++i) {
			//	vars.mat_ID = dataOrientedLoader.levelMeshes[i].materialIndex;

			//	vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SHADER_VARS), &vars);
			//	
			//	//vkCmdDrawIndexed(commandBuffer, dataOrientedLoader.levelMeshes[i].drawInfo.indexCount, instance.transformCount, dataOrientedLoader.levelMeshes[i].drawInfo.indexOffset, model.vertexStart, instance.transformStart);
			//	vkCmdDrawIndexed(commandBuffer, dataOrientedLoader.levelMeshes[i].drawInfo.indexCount, instance.transformCount, dataOrientedLoader.levelMeshes[i].drawInfo.indexOffset, model.vertexStart, instance.transformStart);
			//}
		}
	}

private:
	void CleanUp() {
		// wait till everything has completed
		vkDeviceWaitIdle(device);
		
		// Release allocated buffers, shaders & pipeline
		vkDestroyBuffer(device, vertexHandle, nullptr);
		vkFreeMemory(device, vertexData, nullptr);
		vkDestroyBuffer(device, indiceHandle, nullptr);
		vkFreeMemory(device, indiceData, nullptr);

		for (int i = 0; i < storageData.size(); ++i) {
			vkDestroyBuffer(device, storageHandle[i], nullptr);
			vkFreeMemory(device, storageData[i], nullptr);
		}
		storageData.clear();
		storageHandle.clear();

		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, pixelShader, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayoutHandle, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
	}
};
