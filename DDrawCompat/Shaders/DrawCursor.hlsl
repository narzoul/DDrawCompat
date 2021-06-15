sampler2D s_dstTexture : register(s0);
sampler2D s_maskTexture : register(s1);
sampler2D s_colorTexture : register(s2);
sampler2D s_xorTexture : register(s3);

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
	float4 dst = tex2D(s_dstTexture, texCoord);
	float4 mask = tex2D(s_maskTexture, texCoord);
	float4 color = tex2D(s_colorTexture, texCoord);

	float4 maskedDst = dst * mask;

	return float4(
		tex2D(s_xorTexture, float2(maskedDst.r, color.r)).r,
		tex2D(s_xorTexture, float2(maskedDst.g, color.g)).r,
		tex2D(s_xorTexture, float2(maskedDst.b, color.b)).r,
		1);
}
