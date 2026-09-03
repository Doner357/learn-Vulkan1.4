static const float2 positions[3] = {
    { 0.0, -0.5 },
    { 0.5, 0.5 },
    { -0.5, 0.5 }
};

static const float3 colors[3] = {
    { 1.0, 0.0, 0.0 },
    { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 1.0 }
};

struct VertexOutput {
    float3 color : COLOR0 = float3(0.0);
    float4 sv_position : SV_Position = float4(0.0);
};

[shader("vertex")]
VertexOutput vertMain(uint vid: SV_VertexID)
{
    VertexOutput output = {};

    output.sv_position = float4(positions[vid], 0.0, 1.0);
    output.color = colors[vid];

    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput in_vert) : SV_Target
{
    float3 color = in_vert.color;

    return float4(color, 1.0);
}