sampler2D s_texture : register(s0);
sampler2D s_lockRef : register(s1);

float4 main(float2 texCoord : TEXCOORD0, float2 refCoord : TEXCOORD1) : COLOR0
{
	float4 texColor = tex2D(s_texture, texCoord);
	float4 refColor = tex2D(s_lockRef, refCoord);
	clip(all(texColor == refColor) ? -1 : 1);
	return texColor;
}
