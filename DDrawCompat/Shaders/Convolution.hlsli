sampler2D s_texture : register(s0);
int g_sampleCountX : register(i0);
int g_sampleCountY : register(i1);

float4 c[32] : register(c0);
float4 g_extraParams[4] : register(c5);

static const float2 g_textureSize       = c[0].xy;
static const float2 g_textureSizeRcp    = c[0].zw;
static const float4 g_firstCoordOffset  = c[1];
static const float4 g_coordStep         = c[2];
static const float2 g_sampleCoordOffset = c[3].xy;
static const float2 g_halfTexelOffset   = c[3].zw;
static const float  g_support           = c[4].x;
static const float  g_supportRcp        = c[4].y;

float kernel(float x);

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
	const float2 sampleCoord = texCoord * g_textureSize + g_sampleCoordOffset;
	const float2 sampleCoordFrac = frac(sampleCoord);
	const float2 sampleCoordInt = sampleCoord - sampleCoordFrac;
	const float2 centeredTexCoord = sampleCoordInt * g_textureSizeRcp + g_halfTexelOffset;

	float4 coord = float4(centeredTexCoord, -sampleCoordFrac * g_coordStep.zw) + g_firstCoordOffset;
	float4 coordStep = g_coordStep;
	float4 colorSum = 0;

	if (0 != coordStep.x)
	{
		for (int i = 0; i < g_sampleCountX; ++i)
		{
			coordStep.w = kernel(coord.z);
			float4 color = tex2Dlod(s_texture, coord);
			colorSum += coordStep.w * color;
			coord += coordStep;
		}
		return colorSum / coord.w;
	}
	else
	{
		for (int i = 0; i < g_sampleCountY; ++i)
		{
			coordStep.z = kernel(coord.w);
			float4 color = tex2Dlod(s_texture, coord);
			colorSum += coordStep.z * color;
			coord += coordStep;
		}
		return colorSum / coord.z;
	}
}
