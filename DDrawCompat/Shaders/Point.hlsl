#define NONNEGATIVE
#include "Convolution.hlsli"

float4 kernel(float4 x)
{
	return abs(x) <= 0.5f;
}
