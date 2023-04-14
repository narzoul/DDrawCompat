sampler2D s_texture : register(s0);
int g_sampleCountHalfMinusOne : register(i0);

float4 c[32] : register(c0);
float4 g_extraParams[4] : register(c6);

static const float2 g_textureSize         = c[0].xy;
static const float2 g_sampleCoordOffset   = c[0].zw;
static const float4 g_textureCoordOffset  = c[1];
static const float4 g_kernelCoordOffset   = c[2];
static const float2 g_textureCoordStep    = c[3].xy;
static const float2 g_kernelCoordStep     = c[3].zw;
static const float2 g_textureCoordStepPri = c[4].xy;
static const float2 g_textureCoordStepSec = c[4].zw;
static const float2 g_kernelCoordStepPri  = c[5].xy;
static const float  g_support             = c[5].z;
static const float  g_supportRcp          = c[5].w;

#ifdef NONNEGATIVE
float4 kernel(float4 x);
#else
float kernel(float x);
#endif

void addSamples(inout float4 colorSum, float4 textureCoord, float4 weights)
{
#ifdef NONNEGATIVE
	const float weightSum = weights.x + weights.z;
	colorSum += weightSum * tex2Dlod(s_texture, lerp(textureCoord.xyxy, textureCoord.zwzw, weights.z / weightSum));
#else
	colorSum += weights.x * tex2Dlod(s_texture, textureCoord.xyxy);
	colorSum += weights.z * tex2Dlod(s_texture, textureCoord.zwzw);
#endif
}

float4 getWeights(float4 kernelCoord)
{
#ifdef NONNEGATIVE
	return kernel(kernelCoord);
#else
	return float4(kernel(kernelCoord.x), kernel(kernelCoord.y), kernel(kernelCoord.z), kernel(kernelCoord.w));
#endif
}

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
	const float2 sampleCoord = mad(texCoord, g_textureSize, g_sampleCoordOffset);
	const float2 sampleCoordFrac = frac(sampleCoord);
	const float2 sampleCoordInt = sampleCoord - sampleCoordFrac;

	float4 textureCoord = mad(sampleCoordInt.xyxy, g_textureCoordStep.xyxy, g_textureCoordOffset);
	float4 kernelCoord = mad(-sampleCoordFrac.xyxy, g_kernelCoordStep.xyxy, g_kernelCoordOffset);
	kernelCoord = g_textureCoordStepPri.x > 0 ? kernelCoord : kernelCoord.yxwz;

	const float4 weights = getWeights(kernelCoord);

#ifdef NONNEGATIVE
	[branch] if (g_sampleCoordOffset.x == g_sampleCoordOffset.y)
	{
		textureCoord = lerp(textureCoord, textureCoord + g_textureCoordStepSec.xyxy, weights.w / (weights.y + weights.w));
	}
#endif

	float4 colorSum = 0;
	addSamples(colorSum, textureCoord, weights);

	for (int i = 0; i < g_sampleCountHalfMinusOne; ++i)
	{
		textureCoord += g_textureCoordStepPri.xyxy;
		kernelCoord += g_kernelCoordStepPri.xyxy;
		addSamples(colorSum, textureCoord, getWeights(kernelCoord));
	}

	return colorSum / colorSum.a;
}
