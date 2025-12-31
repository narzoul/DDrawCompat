sampler2D s_texture : register(s0);
sampler2D s_gammaRamp : register(s1);
sampler2D s_dither : register(s2);
int g_sampleCountHalfMinusOne : register(i0);

float4 c[9] : register(c200);
float4 g_extraParams[4] : register(c209);

bool g_useSrgbRead : register(b0);
bool g_useSrgbReadNeg : register(b1);
bool g_useSrgbWrite : register(b2);
bool g_useGammaRamp : register(b3);
bool g_useDithering : register(b4);

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
static const float4 g_maxRgb              = c[6];
static const float4 g_maxRgbRcp           = c[7];
static const float  g_ditherScale         = c[8].x;
static const float  g_ditherOffset        = c[8].y;

#ifdef NONNEGATIVE
float4 kernel(float4 x);
#else
float kernel(float x);
#endif

float4 srgb2lin(float4 color)
{
    return float4(color.rgb < 0.04045f ? color.rgb / 12.92f : pow((abs(color.rgb) + 0.055f) / 1.055f, 2.4f), 1);
}

void addSamples(inout float4 color, float4 textureCoord, float4 weights)
{
#ifdef NONNEGATIVE
	[branch]
	if (g_useSrgbRead)
	{
#endif
    float4 color1 = tex2Dlod(s_texture, textureCoord.xyxy);
    float4 color2 = tex2Dlod(s_texture, textureCoord.zwzw);
	[branch]
    if (g_useSrgbRead)
    {
        color1 = srgb2lin(color1);
        color2 = srgb2lin(color2);
    }
    color += weights.x * color1;
    color += weights.z * color2;
#ifdef NONNEGATIVE
	}
	else
	{
		const float weightSum = weights.x + weights.z;
		color += weightSum * tex2Dlod(s_texture, lerp(textureCoord.xyxy, textureCoord.zwzw, weights.z / weightSum));
	}
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

float4 normalize(float4 color)
{
    return color * 255.0f / 256.0f + 0.5f / 256.0f;
}

float4 main(float2 texCoord : TEXCOORD0, float2 ditherCoord : TEXCOORD2) : COLOR0
{
#ifdef NOFILTER
	float4 color = float4(tex2D(s_texture, texCoord).rgb, 1);
#else
	const float2 sampleCoord = mad(texCoord, g_textureSize, g_sampleCoordOffset);
	const float2 sampleCoordFrac = frac(sampleCoord);
	const float2 sampleCoordInt = sampleCoord - sampleCoordFrac;

	float4 textureCoord = mad(sampleCoordInt.xyxy, g_textureCoordStep.xyxy, g_textureCoordOffset);
	float4 kernelCoord = mad(-sampleCoordFrac.xyxy, g_kernelCoordStep.xyxy, g_kernelCoordOffset);
	kernelCoord = g_textureCoordStepPri.x > 0 ? kernelCoord : kernelCoord.yxwz;

	const float4 weights = getWeights(kernelCoord);

#ifdef NONNEGATIVE
	[branch]
	if (g_useSrgbReadNeg)
	{
		[branch]
		if (g_sampleCoordOffset.x == g_sampleCoordOffset.y)
		{
			textureCoord = lerp(textureCoord, textureCoord + g_textureCoordStepSec.xyxy, weights.w / (weights.y + weights.w));
		}
	}
#endif // NONNEGATIVE

	float4 color = 0;
	addSamples(color, textureCoord, weights);

	for (int i = 0; i < g_sampleCountHalfMinusOne; ++i)
	{
		textureCoord += g_textureCoordStepPri.xyxy;
		kernelCoord += g_kernelCoordStepPri.xyxy;
		addSamples(color, textureCoord, getWeights(kernelCoord));
	}

	color /= color.a;

	[branch]
    if (g_useSrgbWrite)
    {
        color = saturate(color);
        color.rgb = color.rgb < 0.0031308f ? 12.92f * color.rgb : 1.055f * pow(color.rgb, 1.0f / 2.4f) - 0.055f;
#endif // NOFILTER

		[branch]
        if (g_useGammaRamp)
        {
            color = normalize(color);
            color.r = tex2Dlod(s_gammaRamp, float4(color.r, 0.0f, 0, 0)).r;
            color.g = tex2Dlod(s_gammaRamp, float4(color.g, 0.5f, 0, 0)).r;
            color.b = tex2Dlod(s_gammaRamp, float4(color.b, 1.0f, 0, 0)).r;
        }

		[branch]
        if (g_useDithering)
        {
            const float dither = tex2D(s_dither, ditherCoord).r * g_ditherScale + g_ditherOffset;
            color = floor(color * g_maxRgb + dither) * g_maxRgbRcp;
        }
#ifndef NOFILTER
    }
#endif

    return color;
}
