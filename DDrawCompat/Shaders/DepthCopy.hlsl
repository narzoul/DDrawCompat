sampler2D s_texture : register(s0);

float4 main(float2 texCoord : TEXCOORD0, out float depth : DEPTH) : COLOR0
{
    depth = tex2D(s_texture, texCoord).r;
    return 0;
}
