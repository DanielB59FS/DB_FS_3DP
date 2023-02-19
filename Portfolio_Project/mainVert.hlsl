// TODO: 2i
#pragma pack_matrix(row_major)
// an ultra simple hlsl vertex shader
// TODO: Part 2b
#define MAX_SUBMESH_PER_DRAW 10
struct OBJ_ATTRIBUTES
{
	float3 Kd; // diffuse reflectivity
	float d; // dissolve (transparency)
	float3 Ks; // specular reflectivity
	float Ns; // specular exponent
	float3 Ka; // ambient reflectivity
	float sharpness; // local reflection map sharpness
	float3 Tf; // transmission filter
	float Ni; // optical density (index of refraction)
	float3 Ke; // emissive reflectivity
	uint illum; // illumination model
};
struct SHADER_MODEL_DATA
{
	float4 sunDirection, sunColor;
	float4x4 viewMatrix, projectionMatrix;
	float4x4 matricies[MAX_SUBMESH_PER_DRAW];
	OBJ_ATTRIBUTES materials[MAX_SUBMESH_PER_DRAW];
};
[[vk::binding(0)]] StructuredBuffer<SHADER_MODEL_DATA> SceneData;
// TODO: Part 4g
// TODO: Part 2i
// TODO: Part 3e
struct SHADER_VARS
{
	float3 camPos;
	uint mesh_ID;
	float4 sunAmbient;
	int padding[24];
};
[[vk::push_constant]] ConstantBuffer<SHADER_VARS> VARS;
// TODO: Part 4a
// TODO: Part 1f
// TODO: Part 4b
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
//V_OUT main(float3 inputVertex : POSITION, float3 uvw : TEXCOORD, float3 nrm : NORMAL, uint vertexInstanceID : SV_INSTANCEID) : SV_POSITION
V_OUT main(V_IN inputs, uint vertexInstanceID : SV_INSTANCEID)
{
	V_OUT send = (V_OUT) 0;
    //send.posH = mul(mul(inputVertex, SceneData[0].matricies[mesh_ID]), mul(SceneData[0].viewMatrix, SceneData[0].projectionMatrix));
	send.posH = mul(float4(inputs.inputVertex, 1), SceneData[0].matricies[VARS.mesh_ID]);
	send.posW = send.posH.xyz;
	send.posH = mul(send.posH, SceneData[0].viewMatrix);
	send.posH = mul(send.posH, SceneData[0].projectionMatrix);
	// TODO: Part 1h
	// TODO: Part 2i
		// TODO: Part 4e
	// TODO: Part 4b
		// TODO: Part 4e
	send.uvW = inputs.uvw;
	send.nrmW = mul(inputs.nrm, SceneData[0].matricies[VARS.mesh_ID]).xyz;
	return send;
}