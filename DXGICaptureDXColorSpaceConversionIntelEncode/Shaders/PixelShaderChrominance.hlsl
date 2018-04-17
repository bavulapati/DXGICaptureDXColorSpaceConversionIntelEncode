// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//----------------------------------------------------------------------

Texture2D tx : register( t0 );
SamplerState samLinear : register( s0 );

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

// Derived from https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
// Section: Converting 8-bit YUV to RGB888

static const float3x2 RGBtoUVCoeffMatrix =
{
	// These values are calculated from (16 / 255) and (128 / 255)

	-0.148223f,  0.439216f,
	-0.290993f, -0.367788f,
	 0.439216f, -0.071427f
};

float2 CalculateUV(float3 rgb)
{
	float2 uv = mul(rgb, RGBtoUVCoeffMatrix);
	uv += float2(0.501960f, 0.501960f);
	return saturate(uv);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float2 PS_UV(PS_INPUT input) : SV_Target
{
	float4 pixel = tx.Sample(samLinear, input.Tex);
	return CalculateUV(pixel.xyz);
}