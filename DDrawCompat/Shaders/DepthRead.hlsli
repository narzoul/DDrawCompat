#include "DepthConvert.hlsli"

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
    return depthToColor(tex2D(s_texture, texCoord).r);
}
