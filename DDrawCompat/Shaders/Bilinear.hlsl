#define NONNEGATIVE
#include "Convolution.hlsli"

float4 kernel(float4 x)
{
	return saturate(mad(abs(x), g_extraParams[0].xyxy, g_extraParams[0].zwzw));
}
