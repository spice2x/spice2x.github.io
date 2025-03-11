/*
 * original source
 *  vs.1.1 //Shader version 1.1
 *  dcl_position v0;
 *  dcl_color v1;
 *  dcl_texcoord0 v2;
 *  m4x4 oPos, v0, c0
 *  mul oD0, v1, c4
 *  mov oT0.xy, v2
 *
 * build command
 *  fxc.exe /Vi vertex-shader.hlsl /Fh vertex-shader.h /T vs_1_1 /E vs_main
 */

float4x4 WorldViewProjection : register(c0);
float4 ColorMultiply : register(c4);
float2 ConstantHalfTexelFixupOffset : register(c63);

struct VS {
    float4 Position : POSITION;  // dcl_position v0;
    float4 Color    : COLOR;     // dcl_color v1;
    float2 TexCoord : TEXCOORD0; // dcl_texcoord0 v2;
};

VS vs_main(VS input)
{
    VS output;

    output.Position = mul(input.Position, WorldViewProjection); // m4x4 oPos, v0, c0
    output.Color.x  = mul(input.Color.x, ColorMultiply.x);      // mul oD0, v1, c4
    output.Color.y  = mul(input.Color.y, ColorMultiply.y);
    output.Color.z  = mul(input.Color.z, ColorMultiply.z);
    output.Color.w  = mul(input.Color.w, ColorMultiply.w);
    output.TexCoord = input.TexCoord;                           // mov oT0.xy, v2

    // fix texture position
    output.Position.xy += ConstantHalfTexelFixupOffset.xy * output.Position.w;

    return output;
}
