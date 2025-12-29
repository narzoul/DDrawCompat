#include "DepthConvert.hlsli"

float4 main(float2 texCoord : TEXCOORD0) : COLOR0
{
    return depthToColor(depthReadPcf(texCoord, offset));
}
