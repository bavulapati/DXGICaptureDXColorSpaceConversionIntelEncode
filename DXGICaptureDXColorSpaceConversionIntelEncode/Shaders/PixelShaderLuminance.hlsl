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

static const float3x1 RGBtoYCoeffVector =
{
	// These values are calculated from (16 / 255) and (128 / 255)

	0.256788f, 
	0.504129f, 
	0.097906f
};

float CalculateY(float3 rgb)
{
	float y = mul(rgb, RGBtoYCoeffVector);
	y += 0.062745f;
	return saturate(y);
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
float PS_Y(PS_INPUT input) : SV_Target
{
	float4 pixel = tx.Sample(samLinear, input.Tex);
	return CalculateY(pixel.xyz);
}