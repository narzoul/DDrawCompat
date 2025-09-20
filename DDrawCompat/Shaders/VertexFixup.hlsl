bool g_usePerspective : register(b14);
bool g_useTexCoordAdj : register(b15);
float4 g_texCoordAdj : register(c253);
float4 g_offset : register(c254);
float4 g_multiplier : register(c255);

struct VS
{
    float4 pos : POSITION;
    float fog : FOG;
    float4 color[2] : COLOR;
    float4 tex[8] : TEXCOORD;
};

VS main(const VS i)
{
    const float max_rhw = 1U << 31;
    const float min_rhw = 1.0f / max_rhw;
    const float rhw = clamp(i.pos.w, min_rhw, max_rhw);
    const float w = 1.0f / rhw;

    VS o = i;
    o.pos = (i.pos + g_offset) * g_multiplier;
    o.pos.z = saturate(o.pos.z);
    o.pos.xyz *= w;
    o.pos.w = w;
    o.fog = i.color[1].a;
    [branch] if (g_useTexCoordAdj)
    {
        o.tex[0].xy = round(o.tex[0].xy * g_texCoordAdj.xy + g_texCoordAdj.zw) / g_texCoordAdj.xy;
    }
    return o;
}
