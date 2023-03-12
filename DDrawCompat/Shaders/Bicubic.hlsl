#include "Convolution.hlsli"

float kernel(float x)
{
	x = min(abs(x), g_support);
	const float4 weights = x < 1 ? g_extraParams[0] : g_extraParams[1];
	const float4 powers = float4(pow(x, 3), pow(x, 2), x, 1);
	return dot(weights, powers);
}
