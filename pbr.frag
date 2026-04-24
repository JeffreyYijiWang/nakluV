#version 450 

#ifndef TONEMAP
	#include "tonemap.glsl"
#endif

layout(set=0,binding=0,std140) uniform World {
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY; //energy supplied by sky to a surface patch with normal = SKY_DIRECTION
	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY; //energy supplied by sun to a surface patch with normal = SUN_DIRECTION
	vec3 CAMERA_POSITION;
	float ENVIRONMENT_MIPS;
};
layout(push_constant) uniform tone_map{
	float expose;
	int toneMapMode;
};
layout(set=0,binding=1) uniform samplerCube IRRADIANCE_MAP;
layout(set=0, binding=2) uniform samplerCube ENVIRONMENT;
layout(set=0, binding=3) uniform sampler2D BRDF_LUT;

layout(set=2, binding=0) uniform sampler2D NORMAL;
layout(set=2, binding=1) uniform sampler2D DISPLACEMENT;
layout(set=2, binding=2) uniform sampler2D ALBEDO;
layout(set=2, binding=3) uniform sampler2D ROUGHNESS;
layout(set=2, binding=4) uniform sampler2D METALNESS;

layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in mat3 TBN;

layout(location=0) out vec4 outColor;

const float PI = 3.1415926538;

//partly from https://learnopengl.com/PBR/IBL/Specular-IBL and https://learnopengl.com/code_viewer_gh.php?code=src/6.pbr/2.2.2.ibl_specular_textured/2.2.2.pbr.fs

vec3 FresnelSchlickRoughness(float cosTheta, float roughness, vec3 F0)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {


	vec3 F0 = vec3(0.04,0.04,0.04);
	vec3 albedo = texture(ALBEDO, texCoord).rgb;
	float metalness = texture(METALNESS, texCoord).r;
	//tint for metallic surface
	F0 = mix(F0, albedo, metalness);

	// Sample the normal map and convert from [0,1] to [-1,1]

    vec3 normal_rgb = texture(NORMAL, texCoord).rgb; 
    vec3 tangentNormal = normalize(normal_rgb * 2.0 - 1.0); 

    // Transform the normal from tangent space to world space
    vec3 worldNormal = normalize(TBN * tangentNormal);

	vec3 viewDir = normalize(CAMERA_POSITION - position);
	    float NdotV = max(dot(worldNormal, viewDir), 0.0);
	
	float roughness = clamp(texture(ROUGHNESS, texCoord).r, 0.04, 1.0);

float mip = roughness * max(ENVIRONMENT_MIPS - 1.0, 0.0);

vec3 radiance = textureLod(
    ENVIRONMENT,
    normalize(reflect(-viewDir, worldNormal)),
    mip
).rgb;
	vec3 irradiance = textureLod(ENVIRONMENT, worldNormal, ENVIRONMENT_MIPS).rgb;


	vec2 brdfCoord = vec2(NdotV ,roughness);
    vec2 brdf = texture(BRDF_LUT, vec2(NdotV, roughness)).rg;

	vec3 F = FresnelSchlickRoughness(NdotV , roughness, F0);
	vec3 kS = F;
	vec3 kD = 1.0 - kS;

	kD *= 1.0 - metalness;


	vec3 specular = radiance * (F * brdf.x + brdf.y);
   vec3 diffuse = kD * albedo * irradiance ;
	vec3 hdr = diffuse + specular;

	vec3 exposed = exposure(hdr, expose);

	vec3 mapped;
	if (toneMapMode == 0) {
		mapped = exposed;
	}  else if(toneMapMode == 1) {
		mapped = ACESFitted(exposed);
	}
	else{
		mapped = gamma_correction(exposed);
	}

	outColor = vec4(hdr, 1.0f);
}