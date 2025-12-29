#include "DepthConvert.hlsli"

sampler2D s_lockRef : register(s1);

float4 main(float2 texCoord : TEXCOORD0, out float depth : DEPTH) : COLOR0
{
    float4 texColor = tex2D(s_texture, texCoord);
    float4 refColor = tex2D(s_lockRef, texCoord);
    clip(all(texColor == refColor) ? -1 : 1);
    depth = colorToDepth(texColor);
    return 0;
}
