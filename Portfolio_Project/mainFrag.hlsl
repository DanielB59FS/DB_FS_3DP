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
	uint mesh_ID;
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
	
	float intensity = max(pow(clamp(dot(nrmW, halfVector), 0, 1), SceneData[0].materials[VARS.mesh_ID].Ns), 0);
	
	//float intensity = saturate(dot(viewDir, reflectVector));
	//intensity = saturate(pow(intensity,SceneData[0].materials[mesh_ID].Ns));
	
	float4 reflectedLight = SceneData[0].sunColor * float4(SceneData[0].materials[VARS.mesh_ID].Ks, 1) * intensity;
	
	float4 res = saturate(lightRatio * SceneData[0].sunColor + VARS.sunAmbient) * float4(SceneData[0].materials[VARS.mesh_ID].Kd, SceneData[0].materials[VARS.mesh_ID].d) + reflectedLight;
	
	return res;
}