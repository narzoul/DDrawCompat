#include "Convolution.hlsli"

float kernel(float x)
{
	x = min(abs(x), g_support);

	float4 weights = g_extraParams[LOBES - 1];
	[unroll] for (int i = LOBES - 1; i != 0; --i)
	{
		if (x < i)
		{
			weights = g_extraParams[i - 1];
		}
	}

	const float4 powers = float4(pow(x, 3), pow(x, 2), x, 1);
	return dot(weights, powers);
}
