//=============================================================================
// Sky.fx by Frank Luna (C) 2011 All Rights Reserved.
//
// Effect used to shade sky dome.
//=============================================================================

#include "LightHelper.fx"

// The number of sample points taken along the ray
const int nSamples = 2;
const float fSamples = 2.0;

// The scale depth (the altitude at which the average atmospheric density is found)
const float fScaleDepth = 0.25;
const float fInvScaleDepth = 4.0;

cbuffer cbPerFrame
{
	DirectionalLight gDirLights[3];
	float3 gEyePosW;
	float3 gPlanetPosW;

	float g;
	float g2;
	
	float3 v3CameraPos;		// The camera's current position
	float3 v3LightPos;		// The direction vector to the light source
	float3 v3InvWavelength;	// 1 / pow(wavelength, 4) for the red, green, and blue channels
	float fCameraHeight;	// The camera's current height
	float fCameraHeight2;	// fCameraHeight^2
	float fOuterRadius;		// The outer (atmosphere) radius
	float fOuterRadius2;	// fOuterRadius^2
	float fInnerRadius;		// The inner (planetary) radius
	float fInnerRadius2;	// fInnerRadius^2
	float fKrESun;			// Kr * ESun
	float fKmESun;			// Km * ESun
	float fKr4PI;			// Kr * 4 * PI
	float fKm4PI;			// Km * 4 * PI
	float fScale;			// 1 / (fOuterRadius - fInnerRadius)
	float fScaleOverScaleDepth;	// fScale / fScaleDepth

	float  gFogStart;
	float  gFogRange;
	float4 gFogColor; 
};

cbuffer cbPerObject
{
	float4x4 gWorld;
	float4x4 gWorldInvTranspose;
	float4x4 gWorldViewProj;
	Material gMaterial;
}; 
 
// Nonnumeric values cannot be added to a cbuffer.
 
SamplerState samLinear
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = WRAP;
	AddressV = WRAP;
};

struct VertexIn
{
	float3 PosL     : POSITION;
	float3 NormalL  : NORMAL;
	float2 Tex      : TEXCOORD;
	float3 TangentL : TANGENT;
};

struct VertexOut
{
	float4 PosH		  : SV_POSITION;
    float3 PosW       : POSITION;
    float3 NormalW    : NORMAL;

	float3 Direction  : TEXCOORD;
	float4 c0		  : COLOR0;
	float4 c1		  : COLOR1;
};

// The scale equation calculated by Vernier's Graphical Analysis
float scale(float fCos)
{
	float x = 1.0 - fCos;
	return fScaleDepth * exp(-0.00287 + x*(0.459 + x*(3.83 + x*(-6.80 + x*5.25))));
}

// Calculates the Mie phase function
float getMiePhase(float fCos, float fCos2, float g, float g2)
{
	return 1.5 * ((1.0 - g2) / (2.0 + g2)) * (1.0 + fCos2) / pow(1.0 + g2 - 2.0*g*fCos, 1.5);
}

// Calculates the Rayleigh phase function
float getRayleighPhase(float fCos2)
{
	return 1.0;
	//return 0.75 + 0.75*fCos2;
}

// Returns the near intersection point of a line and a sphere
float getNearIntersection(float3 v3Pos, float3 v3Ray, float fDistance2, float fRadius2)
{
	float B = 2.0 * dot(v3Pos, v3Ray);
	float C = fDistance2 - fRadius2;
	float fDet = max(0.0, B*B - 4.0 * C);
	return 0.5 * (-B - sqrt(fDet));
}

// Returns the far intersection point of a line and a sphere
float getFarIntersection(float3 v3Pos, float3 v3Ray, float fDistance2, float fRadius2)
{
	//float B = 2.0 * dot(v3Pos, v3Ray);
	//float C = fDistance2 - fRadius2;
	//float fDet = max(0.0, B*B - 4.0 * C);
	//return 0.5 * (-B + sqrt(fDet));
	
	float3 dist = v3CameraPos - gPlanetPosW;
	float A = dot(v3Ray,v3Ray);
	float B = 2*dot(dist,v3Ray);
	float C = dot(dist,dist) - fOuterRadius2;
	float fDet = max(0.0, B*B - 4.0f * C * A);
	float bac = sqrt(fDet);
	float t0 = (B + bac)/(2*A);
	float t1 = (B - bac)/(2*A);
	if (t0 < t1)
		return t0;
	else
		return t1;
}

VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	
	// Transform to world space space.
	vout.PosW    = mul(float4(vin.PosL, 1.0f), gWorld).xyz;
	vout.NormalW = mul(vin.NormalL, (float3x3)gWorldInvTranspose);
		
	// Transform to homogeneous clip space.
	vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);

	
	// Get the ray from the camera to the vertex and its length (which is the far point of the ray passing through the atmosphere)
	float3 v3Pos = vout.PosW;
	float3 v3Ray = v3Pos - v3CameraPos;
	float fFar = length(v3Ray);
	v3Ray /= fFar;

	// Calculate the closest intersection of the ray with the outer atmosphere (which is the near point of the ray passing through the atmosphere)
	float fNear = getNearIntersection(v3CameraPos, v3Ray, fCameraHeight2, fOuterRadius2);

	// Calculate the ray's start and end positions in the atmosphere, then calculate its scattering offset
	float3 v3Start = v3CameraPos + v3Ray * fNear;
	fFar -= fNear;
	float fStartAngle = dot(v3Ray, v3Start) / fOuterRadius;
	float fStartDepth = exp(-fInvScaleDepth);
	float fStartOffset = fStartDepth*scale(fStartAngle);

	// Initialize the scattering loop variables
	float fSampleLength = fFar / fSamples;
	float fScaledLength = fSampleLength * fScale;
	float3 v3SampleRay = v3Ray * fSampleLength;
	float3 v3SamplePoint = v3Start + v3SampleRay * 0.5;

	// Now loop through the sample rays
	float3 v3FrontColor = float3(0.0, 0.0, 0.0);
	for(int i=0; i<nSamples; i++)
	{
		float fHeight = length(v3SamplePoint);
		float fDepth = exp(fScaleOverScaleDepth * (fInnerRadius - fHeight));
		float fLightAngle = dot(v3LightPos, v3SamplePoint) / fHeight;
		float fCameraAngle = dot(v3Ray, v3SamplePoint) / fHeight;
		float fScatter = (fStartOffset + fDepth*(scale(fLightAngle) - scale(fCameraAngle)));
		float3 v3Attenuate = exp(-fScatter * (v3InvWavelength * fKr4PI + fKm4PI));
		v3FrontColor += v3Attenuate * (fDepth * fScaledLength);
		v3SamplePoint += v3SampleRay;
	}

	// Finally, scale the Mie and Rayleigh colors and set up the varying variables for the pixel shader
	vout.c0.rgb = v3FrontColor * (v3InvWavelength * fKrESun);
	vout.c1.rgb = v3FrontColor * fKmESun;
	vout.Direction = v3CameraPos - v3Pos;
	return vout;

	/*

	// Get the ray from the camera to the vertex and its length
	// (which is the far point of the ray passing thru the atmo)
	float3 v3Ray = vout.PosW - gEyePosW;
	float fFar = length(v3Ray);
	v3Ray /= fFar;

	// Calculate the closest intersection of the ray with
	// the outer atmo (point A in Fig 16-3 GPU gems 2 ch 16)
	//float fNear = getNearIntersection(gEyePosW, v3Ray, gEyeHeight2, gOuterRadius2);
	float fNear;
	float3 dist = gEyePosW - gPlanetPosW;
	float A = dot(v3Ray,v3Ray);
	float B = 2*dot(dist,v3Ray);
	float C = dot(dist,dist) - gOuterRadius2;
	float bac = sqrt(B*B - 4*A*C);
	float t0 = (B + bac)/(2*A);
	float t1 = (B - bac)/(2*A);
	if ( t0 <= t1 )
		fNear = t0;
	else
		fNear = t1;
	// Calculate the ray's start and end positions in the atmo
	// then calculate its scattering offset
	float3 v3Start = gEyePosW + v3Ray * fNear;
	fFar -= fNear;

	float fStartAngle = dot(v3Ray, v3Start) / gOuterRadius;
	float fStartDepth = exp(-gInvScaleDepth);
	float fStartOffset = fStartDepth * scale(fStartAngle);
	
	// Initialize the scattering loop variables
	float fSampleLength = fFar / gNSamples;
	float fScaledLength = fSampleLength * gScale;
	float v3SampleRay = v3Ray * fSampleLength;
	float v3SamplePoint = v3Start + v3SampleRay * 0.5;

	// Now Loop through the sample points
	float v3FrontColor = float3(0.0,0.0,0.0);
	for (int i=0; i<gNSamples; i++) {
		float fHeight = length(v3SamplePoint);
		float fDepth = exp(gScaleOverScaleDepth * ( gInnerRadius - fHeight ));
		float fLightAngle = dot(gLightDir,v3SamplePoint) / fHeight;
		float fCameraAngle = dot(v3Ray, v3SamplePoint) / fHeight;
		float fScatter = (fStartOffset + fDepth  * (scale(fLightAngle) - scale(fCameraAngle)));
		float v3Attenuate = exp(-fScatter * (gInvWaveLength * gKr4PI + gKm4PI));
		v3FrontColor += v3Attenuate * (fDepth * fScaledLength);
		v3SamplePoint += v3SampleRay;
	}

	vout.c0.rgb = v3FrontColor * (gInvWaveLength * gKrESun);
	vout.c1.rgb = v3FrontColor * gKmESun;
	
	return vout;
	*/	
}

float4 PS(VertexOut pin) : SV_Target
{
	//return float4(0,0,1,0.5);
	float fCos = dot(v3LightPos, pin.Direction) / length(pin.Direction);
	float fCos2 = fCos*fCos;
	float4 color = getRayleighPhase(fCos2) * pin.c0 + getMiePhase(fCos, fCos2, g, g2) * pin.c1;
	color.a = color.b;
	return color;
}

BlendState blend
{

};

technique11 SkyTech
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_5_0, VS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader( ps_5_0, PS() ) );
    }
}
