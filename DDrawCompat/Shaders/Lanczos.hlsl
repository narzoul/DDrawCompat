#include "Convolution.hlsli"

float kernel(float x)
{
	x = min(abs(x), g_support);
	const float PI = radians(180);
	const float pi_x = PI * x;
	const float pi_x_2 = pi_x * pi_x;
	return 0 == pi_x_2 ? 1 : (g_support * sin(pi_x) * sin(pi_x * g_supportRcp) / pi_x_2);
}
