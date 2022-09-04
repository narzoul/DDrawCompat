sampler2D s_texture : register(s0);

struct PS_OUT
{
    float4 color : COLOR0;
    float depth : DEPTH0;
};

PS_OUT main(float2 texCoord : TEXCOORD0)
{
    PS_OUT result;
    result.color = 0;
    result.depth = tex2D(s_texture, texCoord).r;
    return result;
}
