sampler2D s_texture : register(s0);
sampler2D s_gammaRamp : register(s1);

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
	const float4 color = tex2D(s_texture, texCoord);
	return float4(
		tex2D(s_gammaRamp, float2(color.r, 0.0f)).r,
		tex2D(s_gammaRamp, float2(color.g, 0.5f)).r,
		tex2D(s_gammaRamp, float2(color.b, 1.0f)).r,
		0);
}
