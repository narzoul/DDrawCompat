sampler2D s_texture : register(s0);
float4 g_colorKey : register(c31);

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
	float4 color = tex2D(s_texture, texCoord);
	float4 diff = abs(color - g_colorKey);
	if (all(diff.rgb < 0.5f / 255))
	{
		color.a = 0;
	}
	return color;
}
