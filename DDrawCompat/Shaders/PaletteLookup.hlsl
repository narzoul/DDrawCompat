sampler2D s_texture : register(s0);
sampler2D s_palette : register(s1);

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
	return tex2D(s_palette, float2(tex2D(s_texture, texCoord).r, 0));
}
