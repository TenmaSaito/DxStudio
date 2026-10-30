// The engine supplies row-vector matrices and a tint through root constants.
// Keep these names, registers and signatures when authoring a replacement.
cbuffer ObjectConstants : register(b0) {
    row_major float4x4 mvp;
    float4 color;
};

struct VertexInput { float3 position : POSITION; };
struct PixelInput { float4 position : SV_POSITION; };

PixelInput VSMain(VertexInput input) {
    PixelInput output;
    output.position = mul(float4(input.position, 1.0f), mvp);
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET {
    return color;
}
