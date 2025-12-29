#include "DepthConvert.hlsli"

float4 main(float2 texCoord : TEXCOORD0, out float depth : DEPTH) : COLOR0
{
    depth = colorToDepth(tex2D(s_texture, texCoord));
    return 0;
}
