sampler2D s_texture : register(s0);

static const int4 bitShift = { dot(1, bitCount.gb), dot(1, bitCount.b), 0, 0 };
static const int4 cumulativeBitCount = { dot(1, bitCount.rgb), dot(1, bitCount.gb), dot(1, bitCount.b), 0 };
static const int4 maxColor = (1 << bitCount) - 1;
static const float maxDepth = (1 << cumulativeBitCount.r) - 1;

float colorToDepth(float4 color)
{
    float depth = dot(1, round(color * maxColor) * (1 << bitShift));
    if (16 == cumulativeBitCount.r)
    {
        return depth / maxDepth;
    }
    depth += (depth < (maxDepth / 2) ? 1 : 2) / 3.0f;
    return depth / (maxDepth + 1);
}

float4 depthToColor(float depth)
{
    float4 color = depth * maxDepth;
    color /= 1 << cumulativeBitCount;
    color = frac(color);
    color *= 1 << bitCount;
    color = floor(color);
    color *= float4(1.0f / ((1 << bitCount) - 1).rgb, 0);
    return color;
}

float depthReadPcf(float2 texCoord : TEXCOORD0, float offset)
{
    float depth = 0;
    float bitValue = 1 << (cumulativeBitCount.r - 1);
    for (int i = 0; i < cumulativeBitCount.r; ++i)
    {
        depth += bitValue * tex2Dlod(s_texture, float4(texCoord, (depth + bitValue) / maxDepth + offset / maxDepth, 0)).r;
        bitValue /= 2;
    }
    return depth / maxDepth;
}
