#include "DepthConvert.hlsli"

float4 main(float2 texCoord : TEXCOORD0, out float depth : DEPTH) : COLOR0
{
    depth = depthReadPcf(texCoord, offset);
    return 0;
}
