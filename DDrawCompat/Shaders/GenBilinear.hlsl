sampler2D s_texture : register(s0);
float2 g_textureRes : register(c0);
float2 g_scaleFactor : register(c1);

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
	float2 coord = texCoord * g_textureRes - 0.5f;
	float2 fracPart = frac(coord);
	float2 intPart = coord - fracPart;
	coord = (intPart + saturate(g_scaleFactor * (fracPart - 0.5f) + 0.5f) + 0.5f) / g_textureRes;
	return tex2D(s_texture, coord);
}
